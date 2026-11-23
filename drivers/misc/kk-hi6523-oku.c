// SPDX-License-Identifier: GPL-2.0
/*
 * Kara Kutu G39 (Tur 5a, Faz 1): hi6523 şarj çipi — YALNIZ OKUMA.
 *
 * i2c6 0x6B'deki hi6523'ten yalnız 4.9'un da olağan işleyişte okuduğu ve okuması yan etkisiz yazmaçlar
 * okunur: STATUS1 (0x07), IIN (0x60), OTG (0x80), CHG_REG0..5 (0x90-0x95), HiZ (0xA3), WDT (0xE9).
 * Kesme/hata yazmaçları (0x0C-0x0F, temizleme yazması ister) ve ADC (yazma ister) OKUNMAZ.
 * Okuma 4.9 hi6523_read_block ile aynı aktarım: yazmaç adresi + tekrarlı başlatmalı 1 bayt okuma
 * (i2c_smbus_read_byte_data). Bu sürücüde yazmaç YAZAN hiçbir kod yolu yoktur.
 *
 * Zamanlama: probe'da bir okuma; ardından 60 sn hiç I2C yok (okuma çipin watchdog'unu besliyorsa gözlem
 * bozulmasın); sonra 30 sn'de bir. Bir turda ilk hatada tur bırakılır (tekrar deneme yok); 3 ardışık hatalı
 * turdan sonra okuma tamamen durur. Değerler log'a ve salt okunur sysfs "durum"a (önbellek; okuma I2C
 * trafiği üretmez) yazılır ve 4.9'un beklenen değerleriyle (VTERM 4400, ICHG 500, IIN 470, WDT 40 sn,
 * CHG_EN 0) karşılaştırılır.
 *
 * G40: Faz 2 hazırlığı. YALNIZ CONFIG_KK_HI6523_DENEY ile (Faz 2 imajı) bu sürücü kk-hi6523-deney'e bir I2C arka
 * ucu verir: yazmalar yalnız i2c_smbus_write_byte_data(0x90) ve (G41) (0xE9). Faz 1 imajında bu kod derlenmez (nm ile gösterilir).
 * Deney sürerken periyodik okuma duraklar.
 *
 * G42 (Faz 3): YALNIZ CONFIG_KK_SARJ_POLITIKA ile ve DT'de "karakutu,sarj-politika" varsa şarj politikası bu
 * I2C arka ucuyla başlatılır. Yazmalar yalnız: 0x90 (chg0_yaz), 0xE9 (wdt_yaz), 0xE8 (besle_yaz). Pil girdileri
 * kk-sensor-okuma'dan (kk_sensor_pil). Politika I2C'yi kendi tikinde okur; bu sürücünün 30 sn'lik okuması sürer.
 */
#include <linux/i2c.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/workqueue.h>
#include "kk-hi6523.h"
#include "kk-sarj.h"

#define KK_ILK_BEKLEME	(60 * HZ)
#define KK_HATA_SINIR	3

/* G44: okuma periyodu (sn); varsayılan 30. Yalnız test imajı cmdline'da kısaltır (en az 5 sn). */
static unsigned int periyot_sn = 30;
module_param(periyot_sn, uint, 0444);
#define KK_PERIYOT	(max(periyot_sn, 5U) * HZ)

struct kk_hi {
	struct i2c_client *c;
	struct delayed_work iz;
	struct mutex kilit;
	u8 v[KK_HI_SAYI];
	bool gecerli, durdu;
	struct kk_sarj *s;	/* G42: politika (yalnız CONFIG_KK_SARJ_POLITIKA) */
	unsigned int n_tur, n_hata, ardisik;
	unsigned long son_ms;
};

static u8 kk_deger(struct kk_hi *h, u8 reg)
{
	unsigned int i;

	for (i = 0; i < KK_HI_SAYI; i++)
		if (kk_hi_okunan[i] == reg)
			return h->v[i];
	return 0;
}

static int kk_satir(struct kk_hi *h, char *b, size_t n)
{
	u8 s1 = kk_deger(h, KK_HI_STATUS1), b0 = kk_deger(h, KK_HI_BUCK0), c0 = kk_deger(h, KK_HI_CHG0);
	u8 c1 = kk_deger(h, KK_HI_CHG1), c2 = kk_deger(h, KK_HI_CHG2), w = kk_deger(h, KK_HI_WDT);
	u8 lg = kk_deger(h, KK_HI_LOGIC), otg = kk_deger(h, KK_HI_OTG0);
	int o = 0, i;

	if (!h->gecerli)
		return scnprintf(b, n, "gecerli okuma yok (tur %u, hata %u%s)\n", h->n_tur, h->n_hata,
				 h->durdu ? ", DURDU" : "");
	o += scnprintf(b + o, n - o,
		       "CHG_EN=%u BATFET=%u VTERM=%d mV ICHG=%d mA IIN=%d mA WDT=%d sn HiZ=%u OTG=%u wdt_dolmadi=%u",
		       !!(c0 & KK_HI_CHG_EN), !!(c0 & BIT(4)), kk_hi_vterm_mv(c2), kk_hi_ichg_ma(c1),
		       kk_hi_iin_ma(b0), kk_hi_wdt_sn(w), lg & 1, !!(otg & BIT(6)), !!(s1 & BIT(1)));
#if IS_ENABLED(CONFIG_KK_HI6523_O1)
	{
		u8 s0 = kk_deger(h, KK_HI_STATUS0), r2 = kk_deger(h, KK_HI_OTG_RES2);
		static const char * const cs[] = { "sarj-yok", "on-sarj", "hizli", "bitti" };

		o += scnprintf(b + o, n - o,
			       " | O1 STATUS0=0x%02x buck_ok=%u chg=%s ACL=%u DPM=%u TERM=%u rechg=%u otg_on=%u; 0x8E=0x%02x vdpm_sec=%u%s ocp_yari=%u",
			       s0, !!(s0 & BIT(7)), cs[(s0 >> 4) & 3], !!(s0 & BIT(3)), !!(s0 & BIT(2)),
			       !!(s0 & BIT(1)), s0 & 1, !!(s0 & BIT(6)), r2, (r2 >> 1) & 0xf,
			       ((r2 >> 1) & 0xf) == 3 ? "(4460mV)" : ((r2 >> 1) & 0xf) == 4 ? "(4675mV)" : "",
			       !!(r2 & BIT(5)));
	}
#endif
	o += scnprintf(b + o, n - o, " | ham:");
	for (i = 0; i < KK_HI_SAYI; i++)
		o += scnprintf(b + o, n - o, " %02x=%02x", kk_hi_okunan[i], h->v[i]);
	o += scnprintf(b + o, n - o, " | 4.9 beklenen (VTERM 4400 ICHG 500 IIN 470 WDT 40 CHG_EN 0):%s%s%s%s%s%s",
		       kk_hi_vterm_mv(c2) != 4400 ? " VTERM-FARKLI" : "",
		       kk_hi_ichg_ma(c1) != 500 ? " ICHG-FARKLI" : "",
		       kk_hi_iin_ma(b0) != 470 ? " IIN-FARKLI" : "",
		       kk_hi_wdt_sn(w) != 40 ? " WDT-FARKLI" : "",
		       (c0 & KK_HI_CHG_EN) ? " CHG_EN-FARKLI" : "",
		       (kk_hi_vterm_mv(c2) == 4400 && kk_hi_ichg_ma(c1) == 500 && kk_hi_iin_ma(b0) == 470 &&
			kk_hi_wdt_sn(w) == 40 && !(c0 & KK_HI_CHG_EN)) ? " AYNI" : "");
	o += scnprintf(b + o, n - o, " | tur %u hata %u%s\n", h->n_tur, h->n_hata, h->durdu ? " DURDU" : "");
	return o;
}

/* Bir okuma turu: ilk hatada bırakılır (tekrar deneme yok). */
static void kk_tur(struct kk_hi *h, const char *neden)
{
	u8 t[KK_HI_SAYI];
	unsigned int i;
	char *b;
	int r = 0;

	mutex_lock(&h->kilit);
	if (h->durdu) {
		mutex_unlock(&h->kilit);
		return;
	}
	h->n_tur++;
	for (i = 0; i < KK_HI_SAYI; i++) {
		r = i2c_smbus_read_byte_data(h->c, kk_hi_okunan[i]);
		if (r < 0)
			break;
		t[i] = r;
	}
	if (r < 0) {
		h->n_hata++;
		h->ardisik++;
		dev_warn(&h->c->dev, "kk: hi6523: %s okuma HATASI yazmac 0x%02x: %d (ardisik %u/%u); tur birakildi\n",
			 neden, kk_hi_okunan[i], r, h->ardisik, KK_HATA_SINIR);
		if (h->ardisik >= KK_HATA_SINIR) {
			h->durdu = true;
			dev_warn(&h->c->dev, "kk: hi6523: %u ardisik hatali tur; okuma DURDURULDU (artik I2C yok)\n",
				 KK_HATA_SINIR);
		}
		mutex_unlock(&h->kilit);
		return;
	}
	memcpy(h->v, t, sizeof(t));
	h->gecerli = true;
	h->ardisik = 0;
	h->son_ms = jiffies_to_msecs(jiffies);
	b = kmalloc(768, GFP_KERNEL);
	if (b) {
		kk_satir(h, b, 768);
		dev_info(&h->c->dev, "kk: hi6523: %s: %s", neden, b);
		kfree(b);
	}
	mutex_unlock(&h->kilit);
}

static void kk_iz(struct work_struct *w)
{
	struct kk_hi *h = container_of(to_delayed_work(w), struct kk_hi, iz);

	if (kk_deney_aktif())
		dev_info(&h->c->dev, "kk: hi6523: periyodik okuma atlandi (Faz 2 deneyi suruyor)\n");
	else
		kk_tur(h, "periyodik");
	if (!READ_ONCE(h->durdu))
		schedule_delayed_work(&h->iz, KK_PERIYOT);
}

static ssize_t durum_show(struct device *dev, struct device_attribute *a, char *buf)
{
	struct kk_hi *h = dev_get_drvdata(dev);
	int o;

	mutex_lock(&h->kilit);
	o = kk_satir(h, buf, PAGE_SIZE);
	mutex_unlock(&h->kilit);
	return o;
}
static DEVICE_ATTR_RO(durum);

static struct attribute *kk_attrs[] = { &dev_attr_durum.attr, NULL };
ATTRIBUTE_GROUPS(kk);

#if IS_ENABLED(CONFIG_KK_HI6523_DENEY) || IS_ENABLED(CONFIG_KK_SARJ_POLITIKA)
/* Faz 2/3 I2C arka ucu: okuma 4.9 ile aynı; yazma yolları yalnız 0x90, 0xE9 ve (Faz 3) 0xE8 */
static int kk_d_oku(void *ctx, u8 reg, u8 *v)
{
	int r = i2c_smbus_read_byte_data(ctx, reg);

	if (r < 0)
		return r;
	*v = r;
	return 0;
}

static int kk_d_chg0_yaz(void *ctx, u8 v)
{
	return i2c_smbus_write_byte_data(ctx, KK_HI_CHG0, v);
}

/* G41: deneyin onaylı tek ek yazması: yalnız 0xE9 (G42: politikanın pencere başı da) */
static int kk_d_wdt_yaz(void *ctx, u8 v)
{
	return i2c_smbus_write_byte_data(ctx, KK_HI_WDT, v);
}
#endif

#if IS_ENABLED(CONFIG_KK_SARJ_POLITIKA)
/* G42: WDT beslemesi, yalnız 0xE8 */
static int kk_d_besle_yaz(void *ctx, u8 v)
{
	return i2c_smbus_write_byte_data(ctx, KK_HI_BESLE, v);
}

static int kk_d_pil(void *ctx, int *vbat_mv, int *ibat_ma, int *pil_mc, bool *gecerli)
{
	return kk_sensor_pil(vbat_mv, ibat_ma, pil_mc, gecerli);
}
#endif

static int kk_probe(struct i2c_client *c)
{
	struct kk_hi *h;

	if (c->addr != KK_HI_ADRES)	/* yalnız 0x6B */
		return dev_err_probe(&c->dev, -EINVAL, "kk: hi6523: adres 0x%02x (yalniz 0x%02x)\n", c->addr,
				     KK_HI_ADRES);
	h = devm_kzalloc(&c->dev, sizeof(*h), GFP_KERNEL);
	if (!h)
		return -ENOMEM;
	h->c = c;
	mutex_init(&h->kilit);
	INIT_DELAYED_WORK(&h->iz, kk_iz);
	i2c_set_clientdata(c, h);
	dev_info(&c->dev, "kk: hi6523: YALNIZ OKUMA (%zu yazmac); probe'da 1 tur, sonra %d sn I2C yok, sonra %d sn'de bir; %d ardisik hatada durur\n",
		 KK_HI_SAYI, KK_ILK_BEKLEME / HZ, KK_PERIYOT / HZ, KK_HATA_SINIR);
	kk_tur(h, "probe");
	if (!h->durdu)
		schedule_delayed_work(&h->iz, KK_ILK_BEKLEME);
#if IS_ENABLED(CONFIG_KK_SARJ_POLITIKA)
	if (of_property_read_bool(c->dev.of_node, "karakutu,sarj-politika")) {
		struct kk_sarj_arka a = { .ad = "hi6523 i2c6 0x6B", .ctx = c, .oku = kk_d_oku, .chg0_yaz = kk_d_chg0_yaz,
					  .wdt_yaz = kk_d_wdt_yaz, .besle_yaz = kk_d_besle_yaz, .pil = kk_d_pil };

		h->s = kk_sarj_baslat(&c->dev, &a);
		if (IS_ERR(h->s)) {
			dev_warn(&c->dev, "kk: hi6523: sarj politikasi baslatilamadi: %ld\n", PTR_ERR(h->s));
			h->s = NULL;
		}
	}
#endif
#if IS_ENABLED(CONFIG_KK_HI6523_DENEY)
	{
		struct kk_sarj_arka a = { .ad = "hi6523 i2c6 0x6B", .ctx = c, .oku = kk_d_oku,
					  .chg0_yaz = kk_d_chg0_yaz, .wdt_yaz = kk_d_wdt_yaz };

		if (kk_deney_kaydet(&c->dev, &a))
			dev_warn(&c->dev, "kk: hi6523: Faz 2 deneyi kaydedilemedi\n");
	}
#endif
	return 0;
}

static void kk_remove(struct i2c_client *c)
{
	struct kk_hi *h = i2c_get_clientdata(c);

#if IS_ENABLED(CONFIG_KK_HI6523_DENEY)
	kk_deney_birak();
#endif
#if IS_ENABLED(CONFIG_KK_SARJ_POLITIKA)
	kk_sarj_durdur(h->s);
#endif
	WRITE_ONCE(h->durdu, true);
	cancel_delayed_work_sync(&h->iz);
}

static const struct of_device_id kk_of[] = {
	{ .compatible = "karakutu,hi6523-oku" },
	{ }
};
MODULE_DEVICE_TABLE(of, kk_of);

static struct i2c_driver kk_drv = {
	.driver = { .name = "kk-hi6523-oku", .of_match_table = kk_of, .dev_groups = kk_groups },
	.probe = kk_probe,
	.remove = kk_remove,
};
module_i2c_driver(kk_drv);
MODULE_DESCRIPTION("Kara Kutu: hi6523 yalniz okuma (Faz 1)");
MODULE_LICENSE("GPL");
