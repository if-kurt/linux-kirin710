// SPDX-License-Identifier: GPL-2.0
/*
 * Kara Kutu G40/G41 (Tur 5): Faz 2 — elle tetiklenen TEK SEFERLİK CHG_EN deneyi.
 * Politika yok, WDT beslemesi (0xE8) yok. Yalnız CONFIG_KK_HI6523_DENEY ile derlenir.
 *
 * sysfs <aygıt>/deney/baslat: "1" -> deney başlar (açılış başına bir kez); "0" -> sürüyorsa erken bitirir.
 * G41 sırası (Furkan onayı; Faz 1'de çip WDT'si ~60 sn'de kendiliğinden 0'a düştü):
 *   t=0  parametre kapısı (taze 0x60/0x91/0x92: VTERM <= 4400, ICHG <= 500, IIN <= 470). CHG_EN zaten 1 ise
 *        ya da kapı tutmazsa deney YAPILMAZ (yazma yok).
 *        1. 0x90 oku-değiştir-yaz CHG_EN=1 -> geri okuma (farklıysa hemen 0, biter).
 *        2. 0xE9 okunur (CHG_EN yazması WDT'yi kurdu mu?). [1:0] hâlâ 0 ise oku-değiştir-yaz ile 3 (40 sn; 4.9
 *           chip_init değeri) yazılır -> geri okuma (farklıysa CHG_EN=0, biter). Deneyin onaylı TEK ek yazması.
 *        3. Okunan 11 yazmacın başlangıç fotoğrafı alınır.
 *   gözlem: en az 60 sn I2C YOK; okumalar t=65/95/125/155 sn: 11 yazmacın hepsi. CHG_EN kendiliğinden döndü
 *        mü (hangi değere), başka hangi yazmaç değişti loglanır. Parametreler sınır dışına çıkmış ve CHG_EN hâlâ
 *        1 ise HEMEN 0 yazılır.
 *   t=170 (en çok ~3 dk) sürücü kendisi CHG_EN=0 yazar (oku-değiştir-yaz) -> geri okuma.
 * Yazılan yazmaçlar yalnız 0x90 ve (gerekirse) 0xE9. Okuma hatasında deneyin başındaki 0x90'ın CHG_EN'siz hâli
 * doğrudan yazılır (tek deneme). Sigorta: kk_deney_sigorta() CHG_EN=0'ı zaman aşımlı dener; reboot gecikmez.
 * Deney sürerken kk-hi6523-oku'nun periyodik okumaları duraklar (kk_deney_aktif()).
 */
#include <linux/completion.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include "kk-hi6523.h"
#include "kk-sarj.h"

#define KK_SURE_SN	170
static const unsigned int kk_gozlem_sn[] = { 65, 95, 125, 155 };

enum { DY_HAZIR, DY_SURUYOR, DY_BITTI };

struct kk_deney {
	struct device *dev;
	struct kk_sarj_arka a;
	struct mutex kilit;
	struct delayed_work is;
	struct work_struct sigorta_isi;
	struct completion sigorta_bitti;
	int durum, adim;
	unsigned long t0;
	u8 ilk_c0;
	u8 baz[KK_HI_SAYI];	/* t=0 fotoğrafı (yazmalardan sonra) */
	int kendiliginden_sn;	/* CHG_EN'in kendiliğinden 0 görüldüğü ilk gözlem (sn), yoksa -1 */
	bool wdt_yazildi;
	char sonuc[192];
};

static struct kk_deney *kk_d;

static unsigned int kk_t(struct kk_deney *d)
{
	return jiffies_to_msecs(jiffies - d->t0) / 1000;
}

static int kk_hepsi(struct kk_deney *d, u8 *v)
{
	unsigned int i;
	int r;

	for (i = 0; i < KK_HI_SAYI; i++) {
		r = d->a.oku(d->a.ctx, kk_hi_okunan[i], &v[i]);
		if (r)
			return r;
	}
	return 0;
}

static u8 kk_al(const u8 *v, u8 reg)
{
	unsigned int i;

	for (i = 0; i < KK_HI_SAYI; i++)
		if (kk_hi_okunan[i] == reg)
			return v[i];
	return 0;
}

/* deneyin sonu: CHG_EN=0 (oku-değiştir-yaz; okuma olmazsa ilk_c0'ın CHG_EN'siz hâli) + geri okuma */
static void kk_bitir(struct kk_deney *d, const char *neden)
{
	u8 c0 = 0, y, g = 0;
	int r, rg;

	r = d->a.oku(d->a.ctx, KK_HI_CHG0, &c0);
	if (r) {
		y = d->ilk_c0 & ~KK_HI_CHG_EN;
		dev_warn(d->dev, "kk: deney: 0x90 okunamadi (%d); dogrudan 0x%02x yaziliyor (tek deneme)\n", r, y);
		r = d->a.chg0_yaz(d->a.ctx, y);
	} else if (c0 & KK_HI_CHG_EN) {
		y = c0 & ~KK_HI_CHG_EN;
		r = d->a.chg0_yaz(d->a.ctx, y);
	} else {
		y = c0;	/* zaten 0: yazma yok */
	}
	rg = d->a.oku(d->a.ctx, KK_HI_CHG0, &g);
	snprintf(d->sonuc, sizeof(d->sonuc), "%s t=%u sn: CHG_EN=0 %s (0x90 0x%02x -> 0x%02x, geri 0x%02x, r=%d/%d); kendiliginden 0: %d sn; WDT yazildi: %s",
		 neden, kk_t(d), !r && !rg && g == y && !(g & KK_HI_CHG_EN) ? "DOGRULANDI" : "DOGRULANAMADI", c0, y, g,
		 r, rg, d->kendiliginden_sn, d->wdt_yazildi ? "evet (0 -> 3)" : "hayir");
	dev_info(d->dev, "kk: deney: BITTI: %s\n", d->sonuc);
	d->durum = DY_BITTI;
}

static void kk_is(struct work_struct *w)
{
	struct kk_deney *d = container_of(to_delayed_work(w), struct kk_deney, is);
	u8 v[KK_HI_SAYI], c0, s1, wd;
	char fark[160];
	unsigned int i;
	int r, o = 0;

	mutex_lock(&d->kilit);
	if (d->durum != DY_SURUYOR)
		goto cik;
	if (d->adim >= ARRAY_SIZE(kk_gozlem_sn)) {
		kk_bitir(d, "sure doldu");
		goto cik;
	}
	r = kk_hepsi(d, v);
	if (r) {
		dev_warn(d->dev, "kk: deney: t=%u sn gozlem okuma hatasi %d\n", kk_t(d), r);
		kk_bitir(d, "okuma hatasi");
		goto cik;
	}
	c0 = kk_al(v, KK_HI_CHG0);
	s1 = kk_al(v, KK_HI_STATUS1);
	wd = kk_al(v, KK_HI_WDT);
	fark[0] = 0;
	for (i = 0; i < KK_HI_SAYI; i++)
		if (v[i] != d->baz[i])
			o += scnprintf(fark + o, sizeof(fark) - o, " %02x:%02x->%02x", kk_hi_okunan[i], d->baz[i], v[i]);
	if (!(c0 & KK_HI_CHG_EN) && d->kendiliginden_sn < 0)
		d->kendiliginden_sn = kk_t(d);
	dev_info(d->dev, "kk: deney: t=%u sn (beslemesiz): CHG_EN=%u VTERM=%d ICHG=%d IIN=%d WDT=%d sn STATUS1=0x%02x wdt_dolmadi=%u | t=0'a gore degisen:%s%s\n",
		 kk_t(d), !!(c0 & KK_HI_CHG_EN), kk_hi_vterm_mv(kk_al(v, KK_HI_CHG2)), kk_hi_ichg_ma(kk_al(v, KK_HI_CHG1)),
		 kk_hi_iin_ma(kk_al(v, KK_HI_BUCK0)), kk_hi_wdt_sn(wd), s1, !!(s1 & BIT(1)), fark[0] ? fark : " yok",
		 (c0 & KK_HI_CHG_EN) ? "" : " -> CHG_EN KENDILIGINDEN 0");
	if ((c0 & KK_HI_CHG_EN) &&
	    !kk_hi_param_uygun(kk_al(v, KK_HI_BUCK0), kk_al(v, KK_HI_CHG1), kk_al(v, KK_HI_CHG2))) {
		kk_bitir(d, "parametre sinir disi ve CHG_EN hala 1");
		goto cik;
	}
	d->adim++;
	{
		long hedef = (d->adim < ARRAY_SIZE(kk_gozlem_sn) ? kk_gozlem_sn[d->adim] : KK_SURE_SN) * 1000L;
		long gecen = jiffies_to_msecs(jiffies - d->t0);

		schedule_delayed_work(&d->is, msecs_to_jiffies(hedef > gecen ? hedef - gecen : 0));
	}
cik:
	mutex_unlock(&d->kilit);
}

static int kk_baslat(struct kk_deney *d)
{
	u8 b0, c1, c2, c0, s1, wd, y, g, wy, wg;
	int r;

	r = d->a.oku(d->a.ctx, KK_HI_BUCK0, &b0);
	if (!r)
		r = d->a.oku(d->a.ctx, KK_HI_CHG1, &c1);
	if (!r)
		r = d->a.oku(d->a.ctx, KK_HI_CHG2, &c2);
	if (!r)
		r = d->a.oku(d->a.ctx, KK_HI_STATUS1, &s1);
	if (!r)
		r = d->a.oku(d->a.ctx, KK_HI_WDT, &wd);
	if (!r)
		r = d->a.oku(d->a.ctx, KK_HI_CHG0, &c0);
	if (r) {
		snprintf(d->sonuc, sizeof(d->sonuc), "baslamadi: okuma hatasi %d (yazma yok)", r);
		goto yok;
	}
	dev_info(d->dev, "kk: deney: baslangic: VTERM=%d ICHG=%d IIN=%d CHG_EN=%u 0x90=0x%02x STATUS1=0x%02x wdt_dolmadi=%u WDT=%d sn (0xE9=0x%02x)\n",
		 kk_hi_vterm_mv(c2), kk_hi_ichg_ma(c1), kk_hi_iin_ma(b0), !!(c0 & KK_HI_CHG_EN), c0, s1,
		 !!(s1 & BIT(1)), kk_hi_wdt_sn(wd), wd);
	if (!kk_hi_param_uygun(b0, c1, c2)) {
		snprintf(d->sonuc, sizeof(d->sonuc), "baslamadi: parametre kapisi (VTERM %d<=%d ICHG %d<=%d IIN %d<=%d) - yazma yok",
			 kk_hi_vterm_mv(c2), KK_HI_VTERM_TAVAN, kk_hi_ichg_ma(c1), KK_HI_ICHG_TAVAN, kk_hi_iin_ma(b0),
			 KK_HI_IIN_TAVAN);
		goto yok;
	}
	dev_info(d->dev, "kk: deney: parametre kapisi GECTI (VTERM %d<=%d ICHG %d<=%d IIN %d<=%d)\n",
		 kk_hi_vterm_mv(c2), KK_HI_VTERM_TAVAN, kk_hi_ichg_ma(c1), KK_HI_ICHG_TAVAN, kk_hi_iin_ma(b0),
		 KK_HI_IIN_TAVAN);
	if (c0 & KK_HI_CHG_EN) {
		snprintf(d->sonuc, sizeof(d->sonuc), "baslamadi: CHG_EN zaten 1 (0x90=0x%02x) - yazma yok", c0);
		goto yok;
	}
	d->ilk_c0 = c0;
	y = c0 | KK_HI_CHG_EN;
	d->t0 = jiffies;
	d->kendiliginden_sn = -1;
	d->wdt_yazildi = false;
	d->adim = 0;
	d->durum = DY_SURUYOR;

	/* 1. CHG_EN=1 */
	r = d->a.chg0_yaz(d->a.ctx, y);
	if (!r)
		r = d->a.oku(d->a.ctx, KK_HI_CHG0, &g);
	dev_info(d->dev, "kk: deney: t=0 CHG_EN=1: 0x90 0x%02x -> 0x%02x, geri 0x%02x %s (r=%d)\n",
		 c0, y, g, !r && g == y ? "AYNI" : "FARKLI", r);
	if (r || g != y) {
		kk_bitir(d, "t=0 CHG_EN geri okuma beklenen degil");
		return 0;
	}

	/* 2. CHG_EN yazması WDT'yi kurdu mu? Kurmadıysa 3 (40 sn) */
	r = d->a.oku(d->a.ctx, KK_HI_WDT, &wd);
	if (r) {
		kk_bitir(d, "t=0 0xE9 okunamadi");
		return 0;
	}
	dev_info(d->dev, "kk: deney: t=0 CHG_EN yazmasi sonrasi 0xE9=0x%02x WDT=%d sn (%s)\n", wd, kk_hi_wdt_sn(wd),
		 (wd & 3) ? "CHG_EN yazmasi WDT'yi KURDU ya da zaten kuruluydu; 0xE9 yazilmayacak" : "WDT kapali; 3 yazilacak");
	if (!(wd & 3)) {
		wy = wd | 3;
		r = d->a.wdt_yaz ? d->a.wdt_yaz(d->a.ctx, wy) : -EOPNOTSUPP;
		if (!r)
			r = d->a.oku(d->a.ctx, KK_HI_WDT, &wg);
		dev_info(d->dev, "kk: deney: t=0 WDT kuruldu: 0xE9 0x%02x -> 0x%02x, geri 0x%02x %s (r=%d); 0xE8 (besleme) yazilmaz\n",
			 wd, wy, wg, !r && wg == wy ? "AYNI" : "FARKLI", r);
		if (r || wg != wy) {
			kk_bitir(d, "t=0 0xE9 geri okuma beklenen degil");
			return 0;
		}
		d->wdt_yazildi = true;
	}

	/* 3. başlangıç fotoğrafı; sonra en az 60 sn I2C yok */
	r = kk_hepsi(d, d->baz);
	if (r) {
		kk_bitir(d, "t=0 fotograf okunamadi");
		return 0;
	}
	dev_info(d->dev, "kk: deney: t=0 fotograf: 07=%02x 60=%02x 80=%02x 90=%02x 91=%02x 92=%02x 93=%02x 94=%02x 95=%02x a3=%02x e9=%02x; %u sn'ye kadar I2C yok, %u sn'de kendisi 0 yazar\n",
		 d->baz[0], d->baz[1], d->baz[2], d->baz[3], d->baz[4], d->baz[5], d->baz[6], d->baz[7], d->baz[8],
		 d->baz[9], d->baz[10], kk_gozlem_sn[0], KK_SURE_SN);
	schedule_delayed_work(&d->is, msecs_to_jiffies(kk_gozlem_sn[0] * 1000));
	return 0;
yok:
	d->durum = DY_BITTI;
	dev_warn(d->dev, "kk: deney: %s\n", d->sonuc);
	return 0;
}

static ssize_t baslat_show(struct device *dev, struct device_attribute *at, char *buf)
{
	struct kk_deney *d = READ_ONCE(kk_d);
	static const char * const ad[] = { "hazir", "suruyor", "bitti" };
	int o;

	if (!d)
		return -ENODEV;
	mutex_lock(&d->kilit);
	o = sysfs_emit(buf, "%s%s%s (arka uc %s)\n", ad[d->durum], d->sonuc[0] ? ": " : "", d->sonuc, d->a.ad);
	mutex_unlock(&d->kilit);
	return o;
}

static ssize_t baslat_store(struct device *dev, struct device_attribute *at, const char *buf, size_t n)
{
	struct kk_deney *d = READ_ONCE(kk_d);
	bool v;

	if (!d)
		return -ENODEV;
	if (kstrtobool(buf, &v))
		return -EINVAL;
	mutex_lock(&d->kilit);
	if (v) {
		if (d->durum != DY_HAZIR) {
			mutex_unlock(&d->kilit);
			dev_warn(dev, "kk: deney: ikinci baslatma REDDEDILDI (acilis basina bir kez)\n");
			return -EPERM;
		}
		kk_baslat(d);
	} else if (d->durum == DY_SURUYOR) {
		cancel_delayed_work(&d->is);
		kk_bitir(d, "elle durduruldu");
	}
	mutex_unlock(&d->kilit);
	return n;
}
static DEVICE_ATTR_RW(baslat);

static struct attribute *kk_attrs[] = { &dev_attr_baslat.attr, NULL };
static const struct attribute_group kk_grup = { .name = "deney", .attrs = kk_attrs };

static void kk_sigorta_isi(struct work_struct *w)
{
	struct kk_deney *d = container_of(w, struct kk_deney, sigorta_isi);

	kk_bitir(d, "sigorta");	/* deney kilidi alınmaz: bir gözlem takılı olabilir */
	complete(&d->sigorta_bitti);
}

void kk_deney_sigorta(unsigned int zaman_ms)
{
	struct kk_deney *d = READ_ONCE(kk_d);

	if (!d || READ_ONCE(d->durum) != DY_SURUYOR)
		return;
	reinit_completion(&d->sigorta_bitti);
	queue_work(system_unbound_wq, &d->sigorta_isi);
	if (!wait_for_completion_timeout(&d->sigorta_bitti, msecs_to_jiffies(zaman_ms)))
		dev_emerg(d->dev, "kk: deney: sigorta: CHG_EN=0 %u ms'de tamamlanmadi; reboot GECIKTIRILMIYOR\n", zaman_ms);
	else
		dev_emerg(d->dev, "kk: deney: sigorta: CHG_EN=0 islemi bitti (%s)\n", d->sonuc);
}

bool kk_deney_aktif(void)
{
	struct kk_deney *d = READ_ONCE(kk_d);

	return d && READ_ONCE(d->durum) == DY_SURUYOR;
}

int kk_deney_kaydet(struct device *dev, const struct kk_sarj_arka *a)
{
	struct kk_deney *d;
	int r;

	if (kk_d)
		return -EBUSY;
	d = devm_kzalloc(dev, sizeof(*d), GFP_KERNEL);
	if (!d)
		return -ENOMEM;
	d->dev = dev;
	d->a = *a;
	mutex_init(&d->kilit);
	INIT_DELAYED_WORK(&d->is, kk_is);
	INIT_WORK(&d->sigorta_isi, kk_sigorta_isi);
	init_completion(&d->sigorta_bitti);
	d->kendiliginden_sn = -1;
	r = devm_device_add_group(dev, &kk_grup);
	if (r)
		return r;
	WRITE_ONCE(kk_d, d);
	dev_info(dev, "kk: deney: Faz 2 CHG_EN deneyi hazir (arka uc %s); elle: echo 1 > deney/baslat; en cok %u sn; yazmalar yalniz 0x90 ve gerekirse 0xE9; WDT beslemesi (0xE8) YOK, politika YOK\n",
		 a->ad, KK_SURE_SN);
	return 0;
}

void kk_deney_birak(void)
{
	struct kk_deney *d = READ_ONCE(kk_d);

	if (!d)
		return;
	mutex_lock(&d->kilit);
	if (d->durum == DY_SURUYOR) {
		cancel_delayed_work(&d->is);
		kk_bitir(d, "surucu kaldiriliyor");
	}
	mutex_unlock(&d->kilit);
	cancel_delayed_work_sync(&d->is);
	WRITE_ONCE(kk_d, NULL);
}
