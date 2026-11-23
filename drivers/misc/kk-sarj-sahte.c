// SPDX-License-Identifier: GPL-2.0
/*
 * Kara Kutu G39 (Tur 5a, B): SAHTE hi6523 + sahte pil — YALNIZ QEMU.
 *
 * Politikanın (kk-sarj.c) tek arka ucu. "Çip" bellekte 256 baytlık bir dizidir; hiçbir donanıma, I2C'ye,
 * PMIC'e dokunulmaz. Başlangıç değerleri 4.9 chip_init'in SDP için bıraktığı değerlerdir (IIN 470, ICHG 500,
 * VTERM 4400, CHG_EN 0, BATFET 1). Kconfig: yalnız KK_SARJ_SAHTE ile derlenir; cihaz fragment'ında kapalı.
 *
 * Pil modeli: SOC (mAh) -> OCV (4.9 pil profili desaysony_3240_110k_4400_25, 25 °C sütunu); CHG_EN=1 iken
 * ibat = yük - ICHG (+ = deşarj), vbat = OCV - ibat * R. Zaman ölçeği ile hızlandırılır (QEMU).
 *
 * Hata enjeksiyonu (sysfs, yalnız bu sahte aygıtta): okuma_hatasi (N tur / -1 sürekli), takil_ms (0x90
 * yazması takılır), vterm_mv / ichg_ma / iin_kod (parametre bozma), varsayilan (çipin watchdog Default
 * moduna düşmesi: CHG_EN -> karakutu,sahte-varsayilan-chg-en), chg_en_zorla (beklenmedik CHG_EN=1),
 * sic_mc / sic_gecerli / yuk_ma / soc_yuzde (girdiler).
 *
 * G40: çip watchdog modeli: 0xE9 != 0 iken son YAZMA'dan (okuma beslemez — modelin varsayımı) WDT süresi
 * geçince Default mod: CHG_EN -> karakutu,sahte-varsayilan-chg-en, WDT -> 0, STATUS1 wdt_time_out_n -> 0.
 * "karakutu,sahte-hedef-deney" varsa arka uç politikaya değil Faz 2 deneyine (kk-hi6523-deney) bağlanır.
 * G41: 0xE9 yazması (wdt_yaz); "karakutu,sahte-chg-wdt-kurar": CHG_EN=1 yazması WDT 0 ise 3 kurar;
 * "karakutu,sahte-varsayilan-param-boz": Default modda VTERM/ICHG/IIN de sınır dışına (4500/1000/2000) döner.
 * G42: 0xE8 besleme yazması (besle_yaz; b0 darbe biti, okunduğunda 0) — her yazma gibi WDT'yi besler.
 * Hata enjeksiyonu: wdt_yaz_hatasi (0xE9 yazması -EIO), wdt_tutmaz (0xE9 yazması yok sayılır: geri okuma farklı),
 * besle_hatasi (N besleme yazması -EIO / -1 sürekli), wdt_saniye (WDT süresini kısaltır; yalnız hızlı sınama).
 * "yazmalar" sysfs: son 12 yazma sırası (pencere başı sırasının denetimi).
 */
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include "kk-hi6523.h"
#include "kk-sarj.h"
#include "kk-pil-koruma.h"	/* G54 G-6: sahte pil = düşük pil korumasının QEMU kaynağı */

/* desaysony_3240_110k_4400_25, 25 °C: yüzde -> OCV (mV) */
static const struct { u8 p; u16 mv; } kk_ocv[] = {
	{ 0, 3200 }, { 1, 3358 }, { 2, 3447 }, { 3, 3510 }, { 4, 3563 }, { 5, 3607 }, { 10, 3666 },
	{ 15, 3695 }, { 20, 3726 }, { 25, 3747 }, { 30, 3767 }, { 35, 3784 }, { 40, 3801 }, { 45, 3820 },
	{ 50, 3844 }, { 55, 3875 }, { 60, 3918 }, { 65, 3966 }, { 70, 4012 }, { 75, 4060 }, { 80, 4112 },
	{ 85, 4168 }, { 90, 4225 }, { 95, 4284 }, { 100, 4349 },
};
#define KK_FCC_MAH	3291
#define KK_R_MOHM	150

struct kk_sahte {
	struct device *dev;
	struct mutex kilit;
	struct delayed_work model;
	struct kk_sarj *s;
	u8 r[256];
	s64 soc_uah;			/* µAh */
	int sic_mc, yuk_ma, okuma_hatasi, takil_ms, olcek, varsayilan_chg_en;
	unsigned long wdt_son;	/* son yazma (jiffies) */
	int wdt_yaz_hatasi, wdt_tutmaz, besle_hatasi;
	unsigned int n_besle;
	char yaz_iz[12][10];	/* G42: son yazmalar ("E9=03") halka */
	unsigned int yaz_n;
	bool deney, chg_wdt_kurar, param_boz;
	bool sic_gecerli;
	unsigned int n_oku, n_yaz;
};

static int kk_ocv_mv(s64 soc_uah)
{
	int p1000 = (int)div_s64(soc_uah * 1000, KK_FCC_MAH * 1000), i;	/* binde */

	if (p1000 <= 0)
		return kk_ocv[0].mv;
	for (i = 1; i < ARRAY_SIZE(kk_ocv); i++)
		if (p1000 <= kk_ocv[i].p * 10) {
			int a = kk_ocv[i - 1].p * 10, b = kk_ocv[i].p * 10;

			return kk_ocv[i - 1].mv + (kk_ocv[i].mv - kk_ocv[i - 1].mv) * (p1000 - a) / (b - a);
		}
	return kk_ocv[ARRAY_SIZE(kk_ocv) - 1].mv;
}

/* G42: yazma izi (kilit altında çağrılır) */
static void kk_iz_ekle(struct kk_sahte *k, u8 reg, u8 v, int r)
{
	snprintf(k->yaz_iz[k->yaz_n % ARRAY_SIZE(k->yaz_iz)], sizeof(k->yaz_iz[0]), "%02X=%02x%s", reg, v, r ? "!" : "");
	k->yaz_n++;
}

static int kk_ibat(struct kk_sahte *k)
{
	return k->yuk_ma - ((k->r[KK_HI_CHG0] & KK_HI_CHG_EN) ? kk_hi_ichg_ma(k->r[KK_HI_CHG1]) : 0);
}

static void kk_model(struct work_struct *w)
{
	struct kk_sahte *k = container_of(to_delayed_work(w), struct kk_sahte, model);

	mutex_lock(&k->kilit);
	/* 1 sn * ölçek; ibat mA -> µAh: ibat * 1000 * sn / 3600 */
	k->soc_uah -= div_s64((s64)kk_ibat(k) * 1000 * k->olcek, 3600);
	k->soc_uah = clamp_t(s64, k->soc_uah, 0, (s64)KK_FCC_MAH * 1000);
	if ((k->r[KK_HI_WDT] & 3) &&
	    time_after(jiffies, k->wdt_son + kk_hi_wdt_sn(k->r[KK_HI_WDT]) * HZ)) {
		dev_info(k->dev, "kk: sarj-sahte: son yazmadan %u ms\n", jiffies_to_msecs(jiffies - k->wdt_son));
		dev_info(k->dev, "kk: sarj-sahte: cip WDT (%d sn) yazmasiz doldu -> varsayilan mod: CHG_EN %u -> %d, WDT -> 0\n",
			 kk_hi_wdt_sn(k->r[KK_HI_WDT]), !!(k->r[KK_HI_CHG0] & KK_HI_CHG_EN), k->varsayilan_chg_en);
		k->r[KK_HI_CHG0] = (k->r[KK_HI_CHG0] & ~KK_HI_CHG_EN) | (k->varsayilan_chg_en ? KK_HI_CHG_EN : 0);
		k->r[KK_HI_WDT] &= ~3;
		k->r[KK_HI_STATUS1] &= ~BIT(1);
		if (k->param_boz) {
			k->r[KK_HI_CHG2] = (k->r[KK_HI_CHG2] & 0x87) | (14 << 3);
			k->r[KK_HI_CHG1] = (k->r[KK_HI_CHG1] & 0xe0) | 9;
			k->r[KK_HI_BUCK0] = (k->r[KK_HI_BUCK0] & 0x83) | (14 << 2);
			dev_info(k->dev, "kk: sarj-sahte: varsayilan modda parametreler de dondu (4500 mV / 1000 mA / 2000 mA)\n");
		}
	}
	mutex_unlock(&k->kilit);
	schedule_delayed_work(&k->model, HZ);
}

static int kk_oku(void *ctx, u8 reg, u8 *val)
{
	struct kk_sahte *k = ctx;
	int r = 0;

	mutex_lock(&k->kilit);
	if (k->okuma_hatasi) {
		if (k->okuma_hatasi > 0)
			k->okuma_hatasi--;
		r = -EIO;
	} else {
		*val = k->r[reg];
		k->n_oku++;
	}
	mutex_unlock(&k->kilit);
	return r;
}

static int kk_chg0_yaz(void *ctx, u8 val)
{
	struct kk_sahte *k = ctx;
	int t = READ_ONCE(k->takil_ms);

	if (t)
		msleep(t);	/* takılan I2C yazması benzetimi (kilit dışında: sigorta zaman aşımı sınanır) */
	mutex_lock(&k->kilit);
	kk_iz_ekle(k, KK_HI_CHG0, val, 0);
	k->r[KK_HI_CHG0] = val;
	k->n_yaz++;
	k->wdt_son = jiffies;	/* yazma çipi besler (model) */
	if (k->chg_wdt_kurar && (val & KK_HI_CHG_EN) && !(k->r[KK_HI_WDT] & 3))
		k->r[KK_HI_WDT] |= 3;
	mutex_unlock(&k->kilit);
	return 0;
}

static int kk_wdt_yaz(void *ctx, u8 val)
{
	struct kk_sahte *k = ctx;

	int r = 0;

	mutex_lock(&k->kilit);
	if (k->wdt_yaz_hatasi) {
		k->wdt_yaz_hatasi--;
		r = -EIO;
	} else if (k->wdt_tutmaz) {
		k->wdt_tutmaz--;		/* aktarım başarılı ama yazmaç değişmedi (geri okuma farklı olmalı) */
		k->n_yaz++;
	} else {
		k->r[KK_HI_WDT] = val;
		k->n_yaz++;
		k->wdt_son = jiffies;
	}
	kk_iz_ekle(k, KK_HI_WDT, val, r);
	mutex_unlock(&k->kilit);
	return r;
}

/* G42: 0xE8 b0 wd_rst_n: besleme (yazma olayı WDT'yi sıfırlar); bit okunduğunda 0 */
static int kk_besle_yaz(void *ctx, u8 val)
{
	struct kk_sahte *k = ctx;
	int r = 0;

	mutex_lock(&k->kilit);
	if (k->besle_hatasi) {
		if (k->besle_hatasi > 0)
			k->besle_hatasi--;
		r = -EIO;
	} else {
		k->r[KK_HI_BESLE] = val & ~KK_HI_BESLE_BIT;
		k->n_yaz++;
		k->n_besle++;
		k->wdt_son = jiffies;
	}
	kk_iz_ekle(k, KK_HI_BESLE, val, r);
	mutex_unlock(&k->kilit);
	return r;
}

static int kk_pil(void *ctx, int *vbat_mv, int *ibat_ma, int *pil_mc, bool *gecerli)
{
	struct kk_sahte *k = ctx;

	mutex_lock(&k->kilit);
	*ibat_ma = kk_ibat(k);
	*vbat_mv = kk_ocv_mv(k->soc_uah) - *ibat_ma * KK_R_MOHM / 1000;
	*pil_mc = k->sic_mc;
	*gecerli = k->sic_gecerli;
	mutex_unlock(&k->kilit);
	return 0;
}

/* ---------------- sysfs (yalnız sahte aygıt) ---------------- */

static ssize_t durum_show(struct device *dev, struct device_attribute *a, char *buf)
{
	struct kk_sahte *k = dev_get_drvdata(dev);
	int o;

	mutex_lock(&k->kilit);
	o = sysfs_emit(buf, "sahte cip: 0x90=0x%02x CHG_EN=%u VTERM=%d ICHG=%d IIN=%d | pil soc=%lld.%01lld%% ocv=%d ibat=%d sic=%d%s yuk=%d | okuma_hatasi=%d takil=%d oku %u yaz %u | 0xE9=0x%02x besle %u\n",
		       k->r[KK_HI_CHG0], !!(k->r[KK_HI_CHG0] & KK_HI_CHG_EN), kk_hi_vterm_mv(k->r[KK_HI_CHG2]),
		       kk_hi_ichg_ma(k->r[KK_HI_CHG1]), kk_hi_iin_ma(k->r[KK_HI_BUCK0]),
		       div_s64(k->soc_uah * 100, KK_FCC_MAH * 1000),
		       div_s64(k->soc_uah * 1000, KK_FCC_MAH * 1000) % 10, kk_ocv_mv(k->soc_uah), kk_ibat(k),
		       k->sic_mc, k->sic_gecerli ? "" : "(gecersiz)", k->yuk_ma, k->okuma_hatasi, k->takil_ms,
		       k->n_oku, k->n_yaz, k->r[KK_HI_WDT], k->n_besle);
	mutex_unlock(&k->kilit);
	return o;
}
static DEVICE_ATTR_RO(durum);

/* G42: son yazmalar, eskiden yeniye */
static ssize_t yazmalar_show(struct device *dev, struct device_attribute *a, char *buf)
{
	struct kk_sahte *k = dev_get_drvdata(dev);
	unsigned int i, n = ARRAY_SIZE(k->yaz_iz), bas;
	int o = 0;

	mutex_lock(&k->kilit);
	bas = k->yaz_n > n ? k->yaz_n - n : 0;
	for (i = bas; i < k->yaz_n; i++)
		o += sysfs_emit_at(buf, o, "%s ", k->yaz_iz[i % n]);
	o += sysfs_emit_at(buf, o, "(toplam %u)\n", k->yaz_n);
	mutex_unlock(&k->kilit);
	return o;
}
static DEVICE_ATTR_RO(yazmalar);

#define KK_AYAR(ad, ifade)								\
static ssize_t ad##_store(struct device *dev, struct device_attribute *a, const char *buf, size_t n) \
{											\
	struct kk_sahte *k = dev_get_drvdata(dev);					\
	int v;										\
	if (kstrtoint(buf, 0, &v))							\
		return -EINVAL;								\
	mutex_lock(&k->kilit);								\
	ifade;										\
	mutex_unlock(&k->kilit);							\
	dev_info(dev, "kk: sarj-sahte: " #ad " = %d\n", v);				\
	return n;									\
}											\
static DEVICE_ATTR_WO(ad)

KK_AYAR(okuma_hatasi, k->okuma_hatasi = v);
KK_AYAR(takil_ms, k->takil_ms = v);
KK_AYAR(sic_mc, k->sic_mc = v);
KK_AYAR(sic_gecerli, k->sic_gecerli = !!v);
KK_AYAR(yuk_ma, k->yuk_ma = v);
KK_AYAR(soc_yuzde, k->soc_uah = (s64)v * KK_FCC_MAH * 10);
KK_AYAR(vterm_mv, k->r[KK_HI_CHG2] = (k->r[KK_HI_CHG2] & 0x87) | ((((v - 3800) / 50) & 0xf) << 3));
KK_AYAR(ichg_ma, k->r[KK_HI_CHG1] = (k->r[KK_HI_CHG1] & 0xe0) | ((v <= 1000 ? (v - 100) / 100 : 9) & 0x1f));
KK_AYAR(iin_kod, k->r[KK_HI_BUCK0] = (k->r[KK_HI_BUCK0] & 0x83) | ((v & 0x1f) << 2));
KK_AYAR(chg_en_zorla, k->r[KK_HI_CHG0] |= v ? KK_HI_CHG_EN : 0);
KK_AYAR(wdt_yaz_hatasi, k->wdt_yaz_hatasi = v);
KK_AYAR(wdt_tutmaz, k->wdt_tutmaz = v);
KK_AYAR(besle_hatasi, k->besle_hatasi = v);
/* çip watchdog Default modu benzetimi: CHG_EN ve WDT varsayılana döner; v=2 iken parametreler de bozulur */
KK_AYAR(varsayilan, {
	k->r[KK_HI_CHG0] = (k->r[KK_HI_CHG0] & ~KK_HI_CHG_EN) | (k->varsayilan_chg_en ? KK_HI_CHG_EN : 0);
	k->r[KK_HI_WDT] &= ~3;
	k->r[KK_HI_STATUS1] &= ~BIT(1);
	if (v == 2) {
		k->r[KK_HI_CHG2] = (k->r[KK_HI_CHG2] & 0x87) | (14 << 3);	/* 4500 mV */
		k->r[KK_HI_CHG1] = (k->r[KK_HI_CHG1] & 0xe0) | 9;		/* 1000 mA */
		k->r[KK_HI_BUCK0] = (k->r[KK_HI_BUCK0] & 0x83) | (14 << 2);	/* 2000 mA */
	}
});

static struct attribute *kk_attrs[] = {
	&dev_attr_durum.attr, &dev_attr_okuma_hatasi.attr, &dev_attr_takil_ms.attr, &dev_attr_sic_mc.attr,
	&dev_attr_sic_gecerli.attr, &dev_attr_yuk_ma.attr, &dev_attr_soc_yuzde.attr, &dev_attr_vterm_mv.attr,
	&dev_attr_ichg_ma.attr, &dev_attr_iin_kod.attr, &dev_attr_chg_en_zorla.attr, &dev_attr_varsayilan.attr,
	&dev_attr_yazmalar.attr, &dev_attr_wdt_yaz_hatasi.attr, &dev_attr_wdt_tutmaz.attr, &dev_attr_besle_hatasi.attr,
	NULL
};
static const struct attribute_group kk_grup = { .name = "sahte", .attrs = kk_attrs };

static int kk_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct kk_sarj_arka a = { .ad = "SAHTE-hi6523 (QEMU)", .oku = kk_oku, .chg0_yaz = kk_chg0_yaz,
				   .wdt_yaz = kk_wdt_yaz, .besle_yaz = kk_besle_yaz, .pil = kk_pil };
	struct kk_sahte *k;
	u32 v = 45;
	int r;

	k = devm_kzalloc(dev, sizeof(*k), GFP_KERNEL);
	if (!k)
		return -ENOMEM;
	k->dev = dev;
	mutex_init(&k->kilit);
	/* 4.9 chip_init (SDP): IIN kod 2 = 470, ICHG kod 4 = 500, VTERM kod 12 = 4400 / ITERM 5,
	 * CHG_EN 0, BATFET 1, ön şarj 200 mA (kod 1); STATUS1 batfet + wdt_dolmadi; WDT 0 (model) */
	k->r[KK_HI_BUCK0] = 2 << 2;
	k->r[KK_HI_CHG1] = 4;
	k->r[KK_HI_CHG2] = (12 << 3) | 5;
	k->r[KK_HI_CHG0] = BIT(4) | (1 << 2);
	k->r[KK_HI_STATUS1] = 0x03;
	of_property_read_u32(dev->of_node, "karakutu,sahte-soc-yuzde", &v);
	k->soc_uah = (s64)v * KK_FCC_MAH * 10;
	k->olcek = 100;
	of_property_read_u32(dev->of_node, "karakutu,sahte-zaman-olcek", (u32 *)&k->olcek);
	of_property_read_u32(dev->of_node, "karakutu,sahte-varsayilan-chg-en", (u32 *)&k->varsayilan_chg_en);
	v = 0;
	of_property_read_u32(dev->of_node, "karakutu,sahte-wdt-kod", &v);
	k->r[KK_HI_WDT] = v & 3;
	k->wdt_son = jiffies;
	k->deney = of_property_read_bool(dev->of_node, "karakutu,sahte-hedef-deney");
	k->chg_wdt_kurar = of_property_read_bool(dev->of_node, "karakutu,sahte-chg-wdt-kurar");
	k->param_boz = of_property_read_bool(dev->of_node, "karakutu,sahte-varsayilan-param-boz");
	k->sic_mc = 25000;
	k->sic_gecerli = true;
	platform_set_drvdata(pdev, k);
	r = devm_device_add_group(dev, &kk_grup);
	if (r)
		return r;
	INIT_DELAYED_WORK(&k->model, kk_model);
	schedule_delayed_work(&k->model, HZ);
	dev_info(dev, "kk: sarj-sahte: SAHTE hi6523 + pil (QEMU): soc %lld%%, zaman olcegi %dx, varsayilan CHG_EN %d, WDT %d sn, hedef %s; donanima dokunulmaz\n",
		 div_s64(k->soc_uah, KK_FCC_MAH * 10), k->olcek, k->varsayilan_chg_en, kk_hi_wdt_sn(k->r[KK_HI_WDT]),
		 k->deney ? "Faz 2 deneyi" : "politika");
	a.ctx = k;
	kk_pil_koruma_kaynak(kk_pil, k, "SAHTE pil (kk-sarj-sahte, QEMU)");	/* G54 G-6; KK_PIL_KORUMA yoksa boş satır içi */
	if (k->deney) {
#if IS_ENABLED(CONFIG_KK_HI6523_DENEY)
		r = kk_deney_kaydet(dev, &a);
#else
		r = -ENODEV;
#endif
		if (r)
			cancel_delayed_work_sync(&k->model);
		return r;
	}
	k->s = kk_sarj_baslat(dev, &a);
	if (IS_ERR(k->s)) {
		cancel_delayed_work_sync(&k->model);
		return PTR_ERR(k->s);
	}
	return 0;
}

static void kk_remove(struct platform_device *pdev)
{
	struct kk_sahte *k = platform_get_drvdata(pdev);

#if IS_ENABLED(CONFIG_KK_HI6523_DENEY)
	if (k->deney)
		kk_deney_birak();
#endif
	if (!k->deney)
		kk_sarj_durdur(k->s);
	cancel_delayed_work_sync(&k->model);
}

static const struct of_device_id kk_of[] = {
	{ .compatible = "karakutu,sahte-hi6523" },
	{ }
};
MODULE_DEVICE_TABLE(of, kk_of);

static struct platform_driver kk_drv = {
	.probe = kk_probe,
	.remove = kk_remove,
	.driver = { .name = "kk-sarj-sahte", .of_match_table = kk_of },
};
module_platform_driver(kk_drv);
MODULE_DESCRIPTION("Kara Kutu: SAHTE hi6523 + pil (yalniz QEMU) ve sarj politikasi arka ucu");
MODULE_LICENSE("GPL");
