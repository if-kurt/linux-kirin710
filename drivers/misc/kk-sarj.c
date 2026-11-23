// SPDX-License-Identifier: GPL-2.0
/*
 * Kara Kutu G39 (Tur 5a, B): şarj politikası — durum makinesi (WDT katmanından bağımsız).
 *
 * Onaylı kurallar (G38.P + G39 ek koşulları):
 *  - Yazma kümesi yalnız CHG_EN (0x90 b5): oku-değiştir-yaz + geri okuma; başka hiçbir yazmaç yazılmaz.
 *  - CHG_EN=1 her yazılmadan hemen önce VTERM <= 4400 mV, ICHG <= 500 mA, IIN <= 470 mA okunarak doğrulanır;
 *    değilse şarj açılmaz ve loglanır (parametre kapısı). Şarj sürerken de her tikte denetlenir.
 *  - Bant dinlenik voltajla (OCV): alt = %50 (4.9 pil profili desaysony_3240_110k_4400_25, 25 °C: 3844 mV),
 *    üst = %60 (4.9 ölçümü 3929 mV). OCV örneği yalnız dinlenmeden sonra ve |ibat| küçükken alınır.
 *    Şarj pencereli: en çok "pencere" açık, sonra kapalı "dinlenme", sonra OCV.
 *  - G40 (pencere taşması, Furkan kararı seçenek 1): pencere boyu OCV'nin üst eşiğe uzaklığına göre.
 *    Taze OCV ile üst - OCV >= yakin_mv (45 mV) ise tam pencere (1200 sn); daha yakınsa ya da taze OCV yoksa
 *    (yük altında başlatma) kısa pencere (300 sn). En kötü durum (ICHG 500 mA, FCC 3291 mAh): kısa pencere
 *    41,7 mAh = %1,27 -> eşiğin hemen altından başlasa bile taşma <= ~%1,3; tam pencere 166,7 mAh = %5,06 ve
 *    profilde %55-65 arası 8,6-9,6 mV/% olduğundan 45 mV = %4,7-5,2 -> taşma <= ~%0,4.
 *  - Pil sıcaklığı >= sıcak (42 °C) -> kapalı; < soğuk (38 °C) olunca dinlenmeye dönülür.
 *    Sıcaklık geçersizse şarj kapalı.
 *  - Sigorta: kk_sarj_sigorta() reboot'tan önce CHG_EN=0'ı zaman aşımlı dener; reboot gecikmez.
 *  - G43 kapanış kancası (Furkan kararı): reboot notifier; politika SARJ'daysa sigorta yoluyla aynı iş
 *    (CHG_EN=0 + geri okuma, 300 ms zaman aşımı; tutmazsa reboot gecikmez). SARJ'da değilse yazma yok.
 *    Her durumda politika durdurulur (artık CHG_EN=1 yazılmaz, beslenmez). Panikte çalışmaz (emergency_restart
 *    notifier çağırmaz); orada WDT <= 40 sn'de keser.
 *  - sysfs eşikleri yalnız daraltılabilir; her geçiş ve her eşik değişikliği loglanır.
 *  - G42 (Faz 3, Furkan kararı): WDT katmanı AÇIK + besleme. Yazma kümesi: 0x90 (yalnız CHG_EN), 0xE9 (yalnız [1:0]),
 *    0xE8 (yalnız besleme, 4.9 hi6523_reset_watchdog_timer birebir: oku -> b0=1 -> yaz).
 *    Pencere başı: 0xE9=3 -> geri okuma -> CHG_EN=1 -> geri okuma -> hemen ilk besleme. WDT kurulamazsa CHG_EN yazılmaz.
 *    Besleme <= besleme_sn (10, en çok 20) sn'de bir ve YALNIZ sağlıklıyken: SARJ durumu; geçerli ve eşik altı sıcaklık;
 *    parametre kapısı; bu turda I2C hatası yok; vbat tavan altında; pencere süresi içinde; çip CHG_EN=1 ve 0xE9=3.
 *    Biri tutmazsa CHG_EN=0 denenir (okuma da tutmazsa son bilinen 0x90 ile doğrudan yazma) ve besleme bırakılır;
 *    donanım <= 40 sn'de keser. Pencere sonu: CHG_EN=0 -> geri okuma; 0xE9'a 0 yazılmaz, kendiliğinden düşer
 *    (izlenir ve loglanır). Çip kendiliğinden Default'a düşerse (CHG_EN 0) algılanır ve dinlenmeye geçilir.
 *  - Yalnız CONFIG_KK_SARJ_TEST (test imajı): cmdline kk_sarj.test_* ile bant/pencere/dinlenme ve sysfs
 *    "test_besleme_kes" (CHG_EN'e dokunmadan beslemeyi durdurur). Üretim imajında derlenmez.
 *  - Yük altında (|ibat| büyük) OCV alınamazsa: yüklü vbat <= alt ise şarj açılır (yüklü vbat <= OCV olduğundan
 *    bu yalnız erken şarj demektir; üst sınır yine dinlenik OCV ile denetlenir).
 */
#include <linux/completion.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/reboot.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include "kk-hi6523.h"
#include "kk-sarj.h"

enum kk_durum { D_BASLA, D_DINLENME, D_BEKLE, D_SARJ, D_ENGEL, D_SIGORTA };
static const char * const kk_durum_ad[] = { "BASLA", "DINLENME", "BEKLE", "SARJ", "ENGEL", "SIGORTA" };

enum kk_engel { E_YOK, E_ETKIN_DEGIL, E_SIC_GECERSIZ, E_SICAK, E_PARAM, E_OKUMA, E_YAZMA, E_WDT, E_BESLEME };
static const char * const kk_engel_ad[] = { "-", "etkin-degil", "sicaklik-gecersiz", "sicak", "parametre",
					    "okuma-hatasi", "yazma-hatasi", "wdt-kurulamadi", "besleme-hatasi" };

#if IS_ENABLED(CONFIG_KK_SARJ_TEST)
/* YALNIZ test imajı: cmdline kk_sarj.test_alt_mv=... (0 = DT/üretim değeri) */
static unsigned int test_alt_mv, test_ust_mv, test_pencere_sn, test_dinlenme_sn;
module_param(test_alt_mv, uint, 0444);
module_param(test_ust_mv, uint, 0444);
module_param(test_pencere_sn, uint, 0444);
module_param(test_dinlenme_sn, uint, 0444);
#endif

struct kk_sarj {
	struct device *dev;
	struct kk_sarj_arka a;
	struct delayed_work tik;
	struct work_struct sigorta_isi;
	struct completion sigorta_bitti;
	struct mutex kilit;
	atomic_t sigorta;
	unsigned long sigorta_t0;	/* G42: sigorta başlangıcı (jiffies) */
	const char *kes_kaynak;		/* G43: "sigorta" ya da "kapanis kancasi" (zaman aşımlı CHG_EN=0 logu) */
	struct notifier_block kapanis_nb;	/* G43: kapanış kancası */
	bool kapanista;			/* G43: politika kapanış kancasıyla durdu */

	/* parametreler (DT; sysfs'ten yalnız daraltılır) */
	u32 tik_ms, pencere_sn, kisa_pencere_sn, yakin_mv, dinlenme_sn, ust_mv, alt_mv, ocv_ibat_ma, vbat_tavan_mv;
	u32 bu_pencere_sn;	/* sürmekte olan pencerenin boyu */
	u32 besleme_sn;		/* G42: WDT besleme aralığı (<= 20) */
	s32 sicak_mc, soguk_mc;
	bool etkin;

	/* durum */
	enum kk_durum d;
	enum kk_engel e;
	bool dolduruyor;
	unsigned long d_bas;	/* jiffies: durumun başı (dinlenme/pencere) */
	int vbat, ibat, pil_mc, ocv;
	bool sic_gecerli, ocv_var;
	u8 c0, c1, c2, b0, wdt;
	bool c0_bilinen;
	unsigned int ardisik_okuma, ardisik_yazma, n_gecis, n_pencere, n_yazma, n_okuma_hata, n_yazma_hata;

	/* G42: WDT katmanı */
	unsigned long son_yazma, son_besleme, kapanis;	/* jiffies */
	bool wdt_izle, wdt_uyarildi, besleme_kes;
	unsigned int n_besleme, n_besleme_hata, n_wdt_kur, n_dusus, p_besleme, p_besleme_azami_ms;
	unsigned int ardisik_besleme;	/* pencereler arası; yalnız başarılı beslemede sıfırlanır */
};

static struct kk_sarj *kk_tek;	/* sigorta için; tek örnek */

static bool kk_gecti(struct kk_sarj *s, u32 sn)
{
	return time_after_eq(jiffies, s->d_bas + msecs_to_jiffies(sn * 1000));
}

static void kk_gec(struct kk_sarj *s, enum kk_durum d, enum kk_engel e, const char *neden)
{
	if (s->d == d && s->e == e)
		return;
	if (s->d == D_SARJ && d != D_SARJ) {
		/* pencere kapandı (her nedenle): besleme bırakıldı; 0xE9'un kendiliğinden 0'a inişi izlenir */
		dev_info(s->dev, "kk: sarj: pencere ozeti: %u ms acik, besleme %u (en uzun aralik %u ms), 0xE9=0x%02x; besleme birakildi\n",
			 jiffies_to_msecs(jiffies - s->d_bas), s->p_besleme, s->p_besleme_azami_ms, s->wdt);
		s->kapanis = jiffies;
		s->wdt_izle = true;
		s->wdt_uyarildi = false;
		s->besleme_kes = false;
	}
	dev_info(s->dev, "kk: sarj: %s%s%s -> %s%s%s (%s; vbat=%d ibat=%d pil=%d%s ocv=%d CHG_EN=%u dolduruyor=%u)\n",
		 kk_durum_ad[s->d], s->d == D_ENGEL ? ":" : "", s->d == D_ENGEL ? kk_engel_ad[s->e] : "",
		 kk_durum_ad[d], d == D_ENGEL ? ":" : "", d == D_ENGEL ? kk_engel_ad[e] : "", neden,
		 s->vbat, s->ibat, s->pil_mc, s->sic_gecerli ? "" : "(gecersiz)", s->ocv_var ? s->ocv : -1,
		 !!(s->c0 & KK_HI_CHG_EN), s->dolduruyor);
	s->d = d;
	s->e = e;
	s->d_bas = jiffies;
	s->n_gecis++;
}

/* CHG_EN'i değiştir: oku-değiştir-yaz (yalnız b5) + geri okuma. 0 = başarılı. */
static int kk_chg_en(struct kk_sarj *s, bool acik, const char *neden)
{
	u8 eski, yeni, geri;
	int r;

	if (acik && atomic_read(&s->sigorta))
		return -EPERM;	/* G42: sigorta kilitsiz çalışır; süren bir tik artık şarj açamaz */
	r = s->a.oku(s->a.ctx, KK_HI_CHG0, &eski);
	if (r)
		goto hata;
	if (acik) {
		u8 b0, c1, c2;

		/* parametre kapısı: yazmadan HEMEN önce taze okuma */
		r = s->a.oku(s->a.ctx, KK_HI_BUCK0, &b0);
		if (!r)
			r = s->a.oku(s->a.ctx, KK_HI_CHG1, &c1);
		if (!r)
			r = s->a.oku(s->a.ctx, KK_HI_CHG2, &c2);
		if (r)
			goto hata;
		if (!kk_hi_param_uygun(b0, c1, c2)) {
			dev_warn(s->dev, "kk: sarj: parametre kapisi: VTERM=%d (<=%d) ICHG=%d (<=%d) IIN=%d (<=%d) -> sarj ACILMADI\n",
				 kk_hi_vterm_mv(c2), KK_HI_VTERM_TAVAN, kk_hi_ichg_ma(c1), KK_HI_ICHG_TAVAN,
				 kk_hi_iin_ma(b0), KK_HI_IIN_TAVAN);
			return -EPERM;
		}
	}
	yeni = acik ? (eski | KK_HI_CHG_EN) : (eski & ~KK_HI_CHG_EN);
	if (yeni != eski) {
		r = s->a.chg0_yaz(s->a.ctx, yeni);
		if (r)
			goto hata;
		s->n_yazma++;
		s->son_yazma = jiffies;
	}
	r = s->a.oku(s->a.ctx, KK_HI_CHG0, &geri);
	if (r)
		goto hata;
	s->c0 = geri;
	s->c0_bilinen = true;
	if (geri != yeni) {
		r = -EIO;
		dev_warn(s->dev, "kk: sarj: CHG0 geri okuma FARKLI: yazilan 0x%02x okunan 0x%02x\n", yeni, geri);
		goto hata;
	}
	dev_info(s->dev, "kk: sarj: CHG_EN %u -> %u (%s): 0x90 0x%02x -> 0x%02x, geri 0x%02x AYNI\n",
		 !!(eski & KK_HI_CHG_EN), acik, neden, eski, yeni, geri);
	s->ardisik_yazma = 0;
	return 0;
hata:
	s->n_yazma_hata++;
	s->ardisik_yazma++;
	dev_warn(s->dev, "kk: sarj: CHG_EN=%u yazilamadi (%s): %d (ardisik %u)\n", acik, neden, r, s->ardisik_yazma);
	return r ? r : -EIO;
}

/* G42: CHG_EN=0; oku-değiştir-yaz tutmazsa son bilinen 0x90 ile bir doğrudan yazma (G40 deneyindeki yol).
 * Her durumda besleme bu noktada bırakılmış olur (SARJ dışında beslenmez); donanım <= 40 sn'de keser. */
static int kk_kapat(struct kk_sarj *s, const char *neden)
{
	int r = kk_chg_en(s, false, neden), r2;
	u8 v;

	if (!r)
		return 0;
	if (!s->c0_bilinen) {
		dev_warn(s->dev, "kk: sarj: CHG_EN=0 (%s) yazilamadi ve 0x90 hic okunmadi; besleme birakildi, donanim <= 40 sn'de keser\n",
			 neden);
		return r;
	}
	v = s->c0 & ~KK_HI_CHG_EN;
	r2 = s->a.chg0_yaz(s->a.ctx, v);
	if (!r2) {
		s->n_yazma++;
		s->son_yazma = jiffies;
	}
	dev_warn(s->dev, "kk: sarj: CHG_EN=0 (%s): oku-degistir-yaz tutmadi (%d); son bilinen 0x90 ile dogrudan yazma 0x%02x: r=%d; besleme birakildi, donanim <= 40 sn'de keser\n",
		 neden, r, v, r2);
	return r2 ? r2 : r;
}

/* G42: 0xE9 [1:0] = 3 (40 sn): oku-değiştir-yaz + geri okuma; zaten 3 ise yazılmaz. 0 = kurulu. */
static int kk_wdt_kur(struct kk_sarj *s)
{
	u8 eski, yeni, geri;
	int r;

	r = s->a.oku(s->a.ctx, KK_HI_WDT, &eski);
	if (r)
		goto hata;
	yeni = eski | 3;
	geri = eski;
	if (yeni != eski) {
		r = s->a.wdt_yaz(s->a.ctx, yeni);
		if (r)
			goto hata;
		s->n_yazma++;
		s->n_wdt_kur++;
		s->son_yazma = jiffies;
		r = s->a.oku(s->a.ctx, KK_HI_WDT, &geri);
		if (r)
			goto hata;
	}
	s->wdt = geri;
	if (geri != yeni) {
		r = -EIO;
		dev_warn(s->dev, "kk: sarj: WDT geri okuma FARKLI: yazilan 0x%02x okunan 0x%02x\n", yeni, geri);
		goto hata;
	}
	dev_info(s->dev, "kk: sarj: WDT: 0xE9 0x%02x -> 0x%02x, geri 0x%02x AYNI (40 sn)%s\n", eski, yeni, geri,
		 yeni == eski ? " (zaten kurulu; yazilmadi)" : "");
	s->ardisik_yazma = 0;
	return 0;
hata:
	s->n_yazma_hata++;
	s->ardisik_yazma++;
	dev_warn(s->dev, "kk: sarj: WDT kurulamadi: %d (ardisik %u) -> CHG_EN YAZILMADI\n", r, s->ardisik_yazma);
	return r ? r : -EIO;
}

/* G42: besleme (4.9 birebir): 0xE8 oku -> b0=1 -> yaz. Geri okuma yok (wd_rst_n darbe biti; 4.9 da okumaz);
 * etkisi her tikte CHG_EN=1 ve 0xE9=3 okunarak denetlenir. */
static int kk_besle(struct kk_sarj *s, const char *neden)
{
	unsigned int ara = s->p_besleme ? jiffies_to_msecs(jiffies - s->son_besleme) : 0;
	u8 v = 0;
	int r;

	if (atomic_read(&s->sigorta))
		return -EPERM;	/* G42: sigortadan sonra besleme yok */
#if IS_ENABLED(CONFIG_KK_SARJ_TEST)
	if (s->besleme_kes)
		return 0;	/* TEST: besleme kesildi; CHG_EN'e dokunulmaz */
#endif
	r = s->a.oku(s->a.ctx, KK_HI_BESLE, &v);
	if (!r)
		r = s->a.besle_yaz(s->a.ctx, v | KK_HI_BESLE_BIT);
	if (r) {
		s->n_besleme_hata++;
		s->ardisik_besleme++;
		dev_warn(s->dev, "kk: sarj: besleme (%s) BASARISIZ: %d (ardisik %u)\n", neden, r, s->ardisik_besleme);
		return r;
	}
	s->n_besleme++;
	s->n_yazma++;
	s->son_besleme = s->son_yazma = jiffies;
	if (!s->p_besleme)
		dev_info(s->dev, "kk: sarj: ilk besleme (%s): 0xE8 0x%02x -> 0x%02x\n", neden, v, (u8)(v | KK_HI_BESLE_BIT));
	s->p_besleme++;
	s->p_besleme_azami_ms = max(s->p_besleme_azami_ms, ara);
	s->ardisik_besleme = 0;
	return 0;
}

/* ocv_taze: karar bu tikte alınan dinlenik OCV'ye dayanıyor */
static void kk_sarj_baslat_pencere(struct kk_sarj *s, const char *neden, bool ocv_taze)
{
	char n[128];
	int r;

	bool tam = ocv_taze && s->ocv_var && (int)s->ust_mv - s->ocv >= (int)s->yakin_mv;

	s->bu_pencere_sn = tam ? s->pencere_sn : min(s->kisa_pencere_sn, s->pencere_sn);
	snprintf(n, sizeof(n), "%s; pencere %u sn (%s)", neden, s->bu_pencere_sn,
		 tam ? "tam" : (ocv_taze ? "kisa: ust esige yakin" : "kisa: taze OCV yok"));
	neden = n;
	/* G42 pencere başı sırası: 0xE9=3 -> geri okuma -> CHG_EN=1 -> geri okuma -> hemen ilk besleme */
	if (kk_wdt_kur(s)) {
		kk_gec(s, D_ENGEL, E_WDT, "WDT kurulamadi; CHG_EN yazilmadi");
		return;
	}
	r = kk_chg_en(s, true, neden);
	if (r == -EPERM) {
		kk_gec(s, D_ENGEL, E_PARAM, "parametre kapisi");
		return;
	}
	if (r) {
		kk_kapat(s, "acma basarisiz; guvenli kapat");
		kk_gec(s, D_ENGEL, E_YAZMA, "CHG_EN=1 yazilamadi");
		return;
	}
	s->p_besleme = 0;
	s->p_besleme_azami_ms = 0;
	if (kk_besle(s, "pencere basi")) {
		kk_kapat(s, "ilk besleme basarisiz");
		kk_gec(s, D_ENGEL, E_BESLEME, "ilk besleme basarisiz");
		return;
	}
	s->n_pencere++;
	kk_gec(s, D_SARJ, E_YOK, neden);
}

static void kk_karar(struct kk_sarj *s, const char *kaynak)
{
	char n[64];

	if (s->ocv >= s->ust_mv) {
		s->dolduruyor = false;
		snprintf(n, sizeof(n), "%s ocv %d >= ust %u", kaynak, s->ocv, s->ust_mv);
		kk_gec(s, D_BEKLE, E_YOK, n);
	} else if (s->ocv <= s->alt_mv) {
		s->dolduruyor = true;
		snprintf(n, sizeof(n), "%s ocv %d <= alt %u", kaynak, s->ocv, s->alt_mv);
		kk_sarj_baslat_pencere(s, n, true);
	} else if (s->dolduruyor) {
		snprintf(n, sizeof(n), "%s bant ici ocv %d, dolduruluyor", kaynak, s->ocv);
		kk_sarj_baslat_pencere(s, n, true);
	} else {
		snprintf(n, sizeof(n), "%s bant ici ocv %d", kaynak, s->ocv);
		kk_gec(s, D_BEKLE, E_YOK, n);
	}
}

static void kk_adim(struct kk_sarj *s)
{
	enum kk_engel e = E_YOK;
	bool acik, param_ok, dinlenik;
	int r;

	if (atomic_read(&s->sigorta)) {
		kk_gec(s, D_SIGORTA, E_YOK, s->kapanista ? "kapanis kancasi; politika durdu" : "sigorta atti; politika durdu");
		return;
	}
	/* girdiler */
	r = s->a.pil(s->a.ctx, &s->vbat, &s->ibat, &s->pil_mc, &s->sic_gecerli);
	if (r)
		s->sic_gecerli = false;	/* girdi yoksa güvenli taraf: şarj kapalı */
	r = s->a.oku(s->a.ctx, KK_HI_CHG0, &s->c0);
	if (!r)
		r = s->a.oku(s->a.ctx, KK_HI_CHG1, &s->c1);
	if (!r)
		r = s->a.oku(s->a.ctx, KK_HI_CHG2, &s->c2);
	if (!r)
		r = s->a.oku(s->a.ctx, KK_HI_BUCK0, &s->b0);
	if (!r)
		r = s->a.oku(s->a.ctx, KK_HI_WDT, &s->wdt);
	if (r) {
		s->n_okuma_hata++;
		s->ardisik_okuma++;
		if (s->ardisik_okuma == 1 || s->ardisik_okuma == 3)
			dev_warn(s->dev, "kk: sarj: cip okuma hatasi %d (ardisik %u)\n", r, s->ardisik_okuma);
		/* G42: şarj sürerken I2C hatası = sağlıksız: hemen CHG_EN=0 denenir, besleme bırakılır */
		if (s->d == D_SARJ) {
			kk_kapat(s, "sarj sirasinda okuma hatasi");
			kk_gec(s, D_ENGEL, E_OKUMA, "sarj sirasinda okuma hatasi; besleme birakildi");
			return;
		}
		/* okunamayan çipe yazılmaz (oku-değiştir-yaz yapılamaz); 3 ardışık hatada engel */
		if (s->ardisik_okuma >= 3)
			kk_gec(s, D_ENGEL, E_OKUMA, "3 ardisik okuma hatasi; cip durumu bilinmiyor");
		return;
	}
	if (s->ardisik_okuma)
		dev_info(s->dev, "kk: sarj: cip okuma duzeldi (%u hatadan sonra)\n", s->ardisik_okuma);
	s->ardisik_okuma = 0;
	s->c0_bilinen = true;
	acik = s->c0 & KK_HI_CHG_EN;
	/* G42: pencere sonundan sonra 0xE9'un kendiliğinden 0'a inişi (yazılmaz) */
	if (s->wdt_izle && s->d != D_SARJ) {
		if (!(s->wdt & 3)) {
			dev_info(s->dev, "kk: sarj: WDT kendiliginden 0 (0xE9=0x%02x): pencere sonundan %u ms, son yazmadan %u ms\n",
				 s->wdt, jiffies_to_msecs(jiffies - s->kapanis), jiffies_to_msecs(jiffies - s->son_yazma));
			s->wdt_izle = false;
		} else if (!s->wdt_uyarildi && time_after(jiffies, s->son_yazma + 60 * HZ)) {
			dev_warn(s->dev, "kk: sarj: WDT son yazmadan 60 sn sonra hala 0x%02x\n", s->wdt);
			s->wdt_uyarildi = true;
		}
	}
	param_ok = kk_hi_param_uygun(s->b0, s->c1, s->c2);
	dinlenik = abs(s->ibat) <= (int)s->ocv_ibat_ma;

	/* 1. güvenlik engelleri (her durumda önce) */
	if (!s->etkin)
		e = E_ETKIN_DEGIL;
	else if (!s->sic_gecerli)
		e = E_SIC_GECERSIZ;
	else if (s->pil_mc >= s->sicak_mc)
		e = E_SICAK;
	else if (s->d == D_ENGEL && s->e == E_SICAK && s->pil_mc >= s->soguk_mc)
		e = E_SICAK;		/* histerezis: soğuk eşiğin altına inene kadar */
	else if (s->d == D_ENGEL && s->e == E_PARAM && !param_ok)
		e = E_PARAM;
	else if (s->d == D_ENGEL && s->e == E_YAZMA && s->ardisik_yazma >= 3)
		e = E_YAZMA;		/* 3 ardışık yazma hatası: kalıcı */
	else if (s->d == D_ENGEL && ((s->e == E_WDT && s->ardisik_yazma >= 3) ||
				      (s->e == E_BESLEME && s->ardisik_besleme >= 3)))
		e = s->e;		/* G42: 3 ardışık WDT kurma / besleme hatası: kalıcı */
	if (e != E_YOK) {
		if (acik || s->d == D_SARJ)
			kk_kapat(s, kk_engel_ad[e]);
		kk_gec(s, D_ENGEL, e, "guvenlik engeli");
		return;
	}
	if (s->d == D_ENGEL || s->d == D_BASLA)
		kk_gec(s, D_DINLENME, E_YOK, s->d == D_BASLA ? "baslangic" : "engel kalkti");

	/* 2. çip durumu tutarlılığı */
	if (s->d != D_SARJ && acik) {
		dev_warn(s->dev, "kk: sarj: beklenmedik CHG_EN=1 (durum %s); kapatiliyor\n", kk_durum_ad[s->d]);
		if (kk_kapat(s, "beklenmedik CHG_EN=1"))
			kk_gec(s, D_ENGEL, E_YAZMA, "beklenmedik CHG_EN=1 kapatilamadi");
		return;
	}
	if (s->d == D_SARJ && !acik) {
		/* G42: çip Default moduna düştü (WDT doldu): donanım kesti */
		s->n_dusus++;
		dev_warn(s->dev, "kk: sarj: DONANIM KESTI: CHG_EN 0, 0xE9=0x%02x (cip Default modu); son yazmadan %u ms, son beslemeden %u ms%s\n",
			 s->wdt, jiffies_to_msecs(jiffies - s->son_yazma), jiffies_to_msecs(jiffies - s->son_besleme),
			 s->besleme_kes ? " (TEST: besleme kesilmisti)" : "");
		kk_gec(s, D_DINLENME, E_YOK, "CHG_EN beklenmedik 0 (cip Default modu; donanim kesti)");
		return;
	}
	if (s->d == D_SARJ && (s->wdt & 3) != 3) {
		kk_kapat(s, "WDT beklenmedik");
		kk_gec(s, D_DINLENME, E_YOK, "sarj sirasinda 0xE9 != 3");
		return;
	}

	/* 3. durumlar */
	switch (s->d) {
	case D_DINLENME:
		if (!kk_gecti(s, s->dinlenme_sn))
			break;
		if (dinlenik) {
			s->ocv = s->vbat;
			s->ocv_var = true;
			kk_karar(s, "dinlenme sonrasi");
		} else if (s->vbat <= (int)s->alt_mv) {
			s->dolduruyor = true;
			kk_sarj_baslat_pencere(s, "yuk altinda vbat <= alt", false);
		}
		break;
	case D_BEKLE:
		if (dinlenik) {
			s->ocv = s->vbat;
			s->ocv_var = true;
			if (s->ocv <= (int)s->alt_mv)
				kk_karar(s, "bekleme");
		} else if (s->vbat <= (int)s->alt_mv) {
			s->dolduruyor = true;
			kk_sarj_baslat_pencere(s, "yuk altinda vbat <= alt", false);
		}
		break;
	case D_SARJ:
		if (!param_ok) {
			kk_kapat(s, "parametre sinir disi");
			kk_gec(s, D_ENGEL, E_PARAM, "sarj sirasinda parametre sinir disi");
		} else if (s->vbat >= (int)s->vbat_tavan_mv) {
			kk_kapat(s, "vbat tavan");
			kk_gec(s, D_DINLENME, E_YOK, "sarj altinda vbat tavana ulasti");
		} else if (kk_gecti(s, s->bu_pencere_sn)) {
			kk_kapat(s, "pencere bitti");
			kk_gec(s, D_DINLENME, E_YOK, "pencere bitti");
		} else if (time_after_eq(jiffies, s->son_besleme + msecs_to_jiffies(s->besleme_sn * 1000))) {
			/* bütün sağlık koşulları bu tikte tuttu: besle */
			if (kk_besle(s, "periyodik")) {
				kk_kapat(s, "besleme basarisiz");
				kk_gec(s, D_ENGEL, E_BESLEME, "besleme basarisiz");
			}
		}
		break;
	default:
		break;
	}
}

static void kk_tik(struct work_struct *w)
{
	struct kk_sarj *s = container_of(to_delayed_work(w), struct kk_sarj, tik);

	mutex_lock(&s->kilit);
	kk_adim(s);
	mutex_unlock(&s->kilit);
	if (!atomic_read(&s->sigorta))
		schedule_delayed_work(&s->tik, msecs_to_jiffies(s->tik_ms));
}

/* ---------------- sigorta ---------------- */

static void kk_sigorta_isi(struct work_struct *w)
{
	struct kk_sarj *s = container_of(w, struct kk_sarj, sigorta_isi);
	u8 c0;
	int r;

	/* politika kilidi ALINMAZ (bir tik takılı olabilir); yalnız CHG_EN=0 */
	r = s->a.oku(s->a.ctx, KK_HI_CHG0, &c0);
	if (!r && (c0 & KK_HI_CHG_EN))
		r = s->a.chg0_yaz(s->a.ctx, c0 & ~KK_HI_CHG_EN);
	if (!r)
		r = s->a.oku(s->a.ctx, KK_HI_CHG0, &c0);
	dev_emerg(s->dev, "kk: sarj: %s: CHG_EN=0 %s (0x90=0x%02x, r=%d); yazma+geri okuma %s %u ms sonra bitti\n",
		  s->kes_kaynak, !r && !(c0 & KK_HI_CHG_EN) ? "DOGRULANDI" : "DOGRULANAMADI", c0, r,
		  s->kapanista ? "kancadan" : "sigortadan", jiffies_to_msecs(jiffies - s->sigorta_t0));
	complete(&s->sigorta_bitti);
}

/* G42/G43: sigorta ve kapanış kancasının ortak yolu. Çağıran s->sigorta'yı 1 yapmış olmalı (artık CHG_EN=1 yok,
 * besleme yok). CHG_EN=0 + geri okuma ayrı işte; en çok zaman_ms beklenir, sonra her koşulda dönülür. */
static void kk_zamanli_kes(struct kk_sarj *s, unsigned int zaman_ms, const char *kaynak)
{
	unsigned long t0 = jiffies;

	reinit_completion(&s->sigorta_bitti);
	s->sigorta_t0 = t0;
	s->kes_kaynak = kaynak;
	queue_work(system_unbound_wq, &s->sigorta_isi);
	if (!wait_for_completion_timeout(&s->sigorta_bitti, msecs_to_jiffies(zaman_ms)))
		dev_emerg(s->dev, "kk: sarj: %s: CHG_EN=0 %u ms'de tamamlanmadi; reboot GECIKTIRILMIYOR\n", kaynak, zaman_ms);
	else
		dev_emerg(s->dev, "kk: sarj: %s: CHG_EN=0 islemi %u ms'de bitti\n", kaynak,
			  jiffies_to_msecs(jiffies - t0));
}

void kk_sarj_sigorta(unsigned int zaman_ms)
{
	struct kk_sarj *s = READ_ONCE(kk_tek);

	if (!s || atomic_xchg(&s->sigorta, 1))
		return;
	kk_zamanli_kes(s, zaman_ms, "sigorta");
}

/* ---------------- G43: kapanış kancası ---------------- */

#define KK_KANCA_MS 300

/* reboot/halt/poweroff (kernel_restart_prepare vb.; aygıtlar kapanmadan, I2C hâlâ çalışırken). Panikte çağrılmaz. */
static int kk_kapanis_kancasi(struct notifier_block *nb, unsigned long olay, void *veri)
{
	struct kk_sarj *s = container_of(nb, struct kk_sarj, kapanis_nb);
	const char *ad = olay == SYS_RESTART ? "restart" : olay == SYS_HALT ? "halt" :
			 olay == SYS_POWER_OFF ? "power-off" : "?";
	enum kk_durum d;

	if (atomic_xchg(&s->sigorta, 1)) {
		dev_emerg(s->dev, "kk: sarj: kapanis kancasi (%s): sigorta zaten atmis / politika durmus; atlandi\n", ad);
		return NOTIFY_DONE;
	}
	s->kapanista = true;
	/* kilit alınmaz (bir tik I2C'de takılı olabilir). Bayrak kalktıktan sonra süren bir tik CHG_EN=1 yazamaz ve
	 * besleyemez; pencere tam bu anda açılıyorsa ilk besleme -EPERM alır ve tik kendisi CHG_EN=0 yazar. */
	d = data_race(READ_ONCE(s->d));
	if (d != D_SARJ) {
		dev_emerg(s->dev, "kk: sarj: kapanis kancasi (%s): durum %s, SARJ degil -> yazma yok; politika durduruldu\n",
			  ad, kk_durum_ad[d]);
		return NOTIFY_DONE;
	}
	dev_emerg(s->dev, "kk: sarj: kapanis kancasi (%s): durum SARJ -> CHG_EN=0 (en cok %u ms; takilirsa reboot gecikmez)\n",
		  ad, KK_KANCA_MS);
	kk_zamanli_kes(s, KK_KANCA_MS, "kapanis kancasi");
	return NOTIFY_DONE;
}

/* ---------------- sysfs (yalnız daraltma) ---------------- */

static ssize_t durum_show(struct device *dev, struct device_attribute *at, char *buf)
{
	struct kk_sarj *s = READ_ONCE(kk_tek);
	int o;

	if (!s)
		return -ENODEV;
	mutex_lock(&s->kilit);
	o = sysfs_emit(buf, "durum=%s%s%s dolduruyor=%u vbat=%d ibat=%d pil=%d%s ocv=%d CHG_EN=%u VTERM=%d ICHG=%d IIN=%d | bant %u-%u mV sicak/soguk %d/%d pencere(tam/kisa/bu)/dinlenme %u/%u/%u/%u sn yakin %u mV tik %u ms etkin=%u | gecis %u pencere %u yazma %u okuma_hata %u yazma_hata %u sigorta=%d | WDT=0x%02x besleme %u/%u sn (hata %u, pencerede %u, en uzun %u ms) wdt_kur %u dusus %u%s (arka uc %s)\n",
		       kk_durum_ad[s->d], s->d == D_ENGEL ? ":" : "", s->d == D_ENGEL ? kk_engel_ad[s->e] : "",
		       s->dolduruyor, s->vbat, s->ibat, s->pil_mc, s->sic_gecerli ? "" : "(gecersiz)",
		       s->ocv_var ? s->ocv : -1, !!(s->c0 & KK_HI_CHG_EN), kk_hi_vterm_mv(s->c2), kk_hi_ichg_ma(s->c1),
		       kk_hi_iin_ma(s->b0), s->alt_mv, s->ust_mv, s->sicak_mc, s->soguk_mc, s->pencere_sn,
		       s->kisa_pencere_sn, s->bu_pencere_sn, s->dinlenme_sn, s->yakin_mv, s->tik_ms, s->etkin, s->n_gecis, s->n_pencere, s->n_yazma, s->n_okuma_hata,
		       s->n_yazma_hata, atomic_read(&s->sigorta), s->wdt, s->n_besleme, s->besleme_sn, s->n_besleme_hata,
		       s->p_besleme, s->p_besleme_azami_ms, s->n_wdt_kur, s->n_dusus, s->besleme_kes ? " BESLEME-KESIK(TEST)" : "",
		       s->a.ad);
	mutex_unlock(&s->kilit);
	return o;
}
static DEVICE_ATTR_RO(durum);

/* yon: -1 yalnız düşürülebilir, +1 yalnız yükseltilebilir; alt/üst ek sınırla */
static ssize_t kk_daralt(struct device *dev, const char *ad, s32 *p, int yon, s32 alt, s32 ust,
			 const char *buf, size_t n)
{
	struct kk_sarj *s = READ_ONCE(kk_tek);
	s32 v, eski;

	if (!s)
		return -ENODEV;
	if (kstrtos32(buf, 0, &v))
		return -EINVAL;
	mutex_lock(&s->kilit);
	eski = *p;
	if ((yon < 0 && v >= eski) || (yon > 0 && v <= eski) || v < alt || v > ust) {
		mutex_unlock(&s->kilit);
		dev_warn(dev, "kk: sarj: %s %d -> %d REDDEDILDI (yalniz daraltilabilir; izinli %d..%d)\n", ad, eski,
			 v, alt, ust);
		return -EPERM;
	}
	*p = v;
	mutex_unlock(&s->kilit);
	dev_info(dev, "kk: sarj: %s %d -> %d (daraltma)\n", ad, eski, v);
	return n;
}

#define KK_ESIK(ad, alan, yon, altf, ustf)							\
static ssize_t ad##_show(struct device *dev, struct device_attribute *at, char *buf)		\
{												\
	struct kk_sarj *s = READ_ONCE(kk_tek);							\
	return s ? sysfs_emit(buf, "%d\n", (int)s->alan) : -ENODEV;				\
}												\
static ssize_t ad##_store(struct device *dev, struct device_attribute *at, const char *buf, size_t n) \
{												\
	struct kk_sarj *s = READ_ONCE(kk_tek);							\
	if (!s)											\
		return -ENODEV;									\
	return kk_daralt(dev, #ad, (s32 *)&s->alan, yon, altf, ustf, buf, n);			\
}												\
static DEVICE_ATTR_RW(ad)

/* üst yalnız düşer (alt+20'nin altına inemez); alt yalnız yükselir (üst-20'yi geçemez) */
KK_ESIK(ust_mv, ust_mv, -1, (s32)s->alt_mv + 20, 4400);
KK_ESIK(alt_mv, alt_mv, +1, 3000, (s32)s->ust_mv - 20);
/* sıcak yalnız düşer (soğuk+1000'in altına inemez); soğuk yalnız düşer */
KK_ESIK(sicak_mc, sicak_mc, -1, s->soguk_mc + 1000, 60000);
KK_ESIK(soguk_mc, soguk_mc, -1, 0, s->sicak_mc - 1000);
/* pencere yalnız kısalır; dinlenme yalnız uzar */
KK_ESIK(pencere_sn, pencere_sn, -1, 1, 7200);
KK_ESIK(dinlenme_sn, dinlenme_sn, +1, 1, 7200);
/* G40: kısa pencere yalnız kısalır; "yakın" eşiği yalnız büyür (daha sık kısa pencere = daha az taşma) */
KK_ESIK(kisa_pencere_sn, kisa_pencere_sn, -1, 1, (s32)s->pencere_sn);
KK_ESIK(yakin_mv, yakin_mv, +1, 0, 300);

static ssize_t etkin_show(struct device *dev, struct device_attribute *at, char *buf)
{
	struct kk_sarj *s = READ_ONCE(kk_tek);

	return s ? sysfs_emit(buf, "%u\n", s->etkin) : -ENODEV;
}
static ssize_t etkin_store(struct device *dev, struct device_attribute *at, const char *buf, size_t n)
{
	struct kk_sarj *s = READ_ONCE(kk_tek);
	bool v;

	if (!s)
		return -ENODEV;
	if (kstrtobool(buf, &v))
		return -EINVAL;
	if (v) {
		dev_warn(dev, "kk: sarj: etkin 1 REDDEDILDI (yalniz kapatilabilir)\n");
		return -EPERM;
	}
	mutex_lock(&s->kilit);
	s->etkin = false;
	mutex_unlock(&s->kilit);
	dev_info(dev, "kk: sarj: etkin -> 0 (sarj kalici kapali; reboot'a kadar)\n");
	return n;
}
static DEVICE_ATTR_RW(etkin);

#if IS_ENABLED(CONFIG_KK_SARJ_TEST)
/* YALNIZ test imajı: sürmekte olan pencerede beslemeyi durdurur; CHG_EN'e dokunmaz. Pencere bitince kalkar. */
static ssize_t test_besleme_kes_store(struct device *dev, struct device_attribute *at, const char *buf, size_t n)
{
	struct kk_sarj *s = READ_ONCE(kk_tek);
	bool v;

	if (!s)
		return -ENODEV;
	if (kstrtobool(buf, &v) || !v)
		return -EINVAL;
	mutex_lock(&s->kilit);
	if (s->d != D_SARJ) {
		mutex_unlock(&s->kilit);
		return -EBUSY;
	}
	s->besleme_kes = true;
	dev_warn(dev, "kk: sarj: TEST: besleme KESILDI (CHG_EN'e dokunulmadi; 0x90=0x%02x 0xE9=0x%02x); son beslemeden %u ms, son yazmadan %u ms\n",
		 s->c0, s->wdt, jiffies_to_msecs(jiffies - s->son_besleme), jiffies_to_msecs(jiffies - s->son_yazma));
	mutex_unlock(&s->kilit);
	return n;
}
static DEVICE_ATTR_WO(test_besleme_kes);
#endif

static struct attribute *kk_sarj_attrs[] = {
	&dev_attr_durum.attr, &dev_attr_ust_mv.attr, &dev_attr_alt_mv.attr, &dev_attr_sicak_mc.attr,
	&dev_attr_soguk_mc.attr, &dev_attr_pencere_sn.attr, &dev_attr_dinlenme_sn.attr, &dev_attr_etkin.attr,
	&dev_attr_kisa_pencere_sn.attr, &dev_attr_yakin_mv.attr,
#if IS_ENABLED(CONFIG_KK_SARJ_TEST)
	&dev_attr_test_besleme_kes.attr,
#endif
	NULL
};
static const struct attribute_group kk_sarj_grup = { .name = "sarj", .attrs = kk_sarj_attrs };

/* ---------------- başlat / durdur ---------------- */

static u32 kk_dt(struct device *dev, const char *p, u32 vars)
{
	u32 v;

	return of_property_read_u32(dev->of_node, p, &v) ? vars : v;
}

struct kk_sarj *kk_sarj_baslat(struct device *dev, const struct kk_sarj_arka *a)
{
	struct kk_sarj *s;
	int r;

	if (kk_tek)
		return ERR_PTR(-EBUSY);
	s = devm_kzalloc(dev, sizeof(*s), GFP_KERNEL);
	if (!s)
		return ERR_PTR(-ENOMEM);
	s->dev = dev;
	s->a = *a;
	mutex_init(&s->kilit);
	init_completion(&s->sigorta_bitti);
	INIT_WORK(&s->sigorta_isi, kk_sigorta_isi);
	INIT_DELAYED_WORK(&s->tik, kk_tik);
	/* varsayılanlar cihaz değerleri; QEMU DT zaman ölçeğini kısaltır */
	s->tik_ms = kk_dt(dev, "karakutu,sarj-tik-ms", 5000);
	s->pencere_sn = kk_dt(dev, "karakutu,sarj-pencere-sn", 1200);
	s->dinlenme_sn = kk_dt(dev, "karakutu,sarj-dinlenme-sn", 180);
	s->kisa_pencere_sn = kk_dt(dev, "karakutu,sarj-kisa-pencere-sn", 300);
	s->yakin_mv = kk_dt(dev, "karakutu,sarj-yakin-mv", 45);
	s->bu_pencere_sn = s->kisa_pencere_sn;
	s->ust_mv = kk_dt(dev, "karakutu,sarj-ust-mv", 3929);
	s->alt_mv = kk_dt(dev, "karakutu,sarj-alt-mv", 3844);
	s->ocv_ibat_ma = kk_dt(dev, "karakutu,sarj-ocv-ibat-ma", 30);
	s->vbat_tavan_mv = kk_dt(dev, "karakutu,sarj-vbat-tavan-mv", 4150);
	s->sicak_mc = kk_dt(dev, "karakutu,sarj-sicak-mc", 42000);
	s->soguk_mc = kk_dt(dev, "karakutu,sarj-soguk-mc", 38000);
	s->besleme_sn = clamp_t(u32, kk_dt(dev, "karakutu,sarj-besleme-sn", 10), 1, 20);
#if IS_ENABLED(CONFIG_KK_SARJ_TEST)
	if (test_alt_mv || test_ust_mv || test_pencere_sn || test_dinlenme_sn) {
		u32 alt = test_alt_mv ? test_alt_mv : s->alt_mv, ust = test_ust_mv ? test_ust_mv : s->ust_mv;

		if (alt < 3700 || ust > 4100 || ust < alt + 10 || (test_pencere_sn && (test_pencere_sn < 30 || test_pencere_sn > 1200)) ||
		    (test_dinlenme_sn && (test_dinlenme_sn < 10 || test_dinlenme_sn > 600))) {
			dev_warn(dev, "kk: sarj: TEST parametreleri REDDEDILDI (alt %u ust %u pencere %u dinlenme %u); uretim degerleri kullaniliyor\n",
				 alt, ust, test_pencere_sn, test_dinlenme_sn);
		} else {
			s->alt_mv = alt;
			s->ust_mv = ust;
			if (test_pencere_sn)
				s->pencere_sn = test_pencere_sn;
			if (test_dinlenme_sn)
				s->dinlenme_sn = test_dinlenme_sn;
			dev_warn(dev, "kk: sarj: TEST IMAJI: cmdline ile bant %u-%u mV, pencere %u sn, dinlenme %u sn\n",
				 s->alt_mv, s->ust_mv, s->pencere_sn, s->dinlenme_sn);
		}
	}
#endif
	s->etkin = true;
	s->d = D_BASLA;
	s->d_bas = jiffies;
	s->son_yazma = s->son_besleme = s->kapanis = jiffies;
	r = devm_device_add_group(dev, &kk_sarj_grup);
	if (r)
		return ERR_PTR(r);
	WRITE_ONCE(kk_tek, s);
	s->kes_kaynak = "sigorta";
	s->kapanis_nb.notifier_call = kk_kapanis_kancasi;
	s->kapanis_nb.priority = 200;	/* erken: diğer reboot notifier'larından önce */
	register_reboot_notifier(&s->kapanis_nb);
	dev_info(dev, "kk: sarj: politika basladi (arka uc %s): bant %u-%u mV (OCV), pencere %u sn (ust esige < %u mV ya da taze OCV yoksa %u sn) / dinlenme %u sn, tik %u ms, sicak/soguk %d/%d m°C, OCV |ibat|<=%u mA, sarj altinda vbat tavan %u mV; parametre kapisi VTERM<=%d ICHG<=%d IIN<=%d; WDT katmani ACIK: pencere basi 0xE9=3, besleme 0xE8 b0 her %u sn; kapanis kancasi ACIK (SARJ'da CHG_EN=0, %u ms)%s\n",
		 a->ad, s->alt_mv, s->ust_mv, s->pencere_sn, s->yakin_mv, s->kisa_pencere_sn, s->dinlenme_sn, s->tik_ms, s->sicak_mc, s->soguk_mc,
		 s->ocv_ibat_ma, s->vbat_tavan_mv, KK_HI_VTERM_TAVAN, KK_HI_ICHG_TAVAN, KK_HI_IIN_TAVAN, s->besleme_sn, KK_KANCA_MS,
		 IS_ENABLED(CONFIG_KK_SARJ_TEST) ? "; TEST IMAJI (test_besleme_kes var)" : "");
	schedule_delayed_work(&s->tik, 0);
	return s;
}

void kk_sarj_durdur(struct kk_sarj *s)
{
	if (IS_ERR_OR_NULL(s))
		return;
	unregister_reboot_notifier(&s->kapanis_nb);
	atomic_set(&s->sigorta, 1);
	cancel_delayed_work_sync(&s->tik);
	cancel_work_sync(&s->sigorta_isi);
	WRITE_ONCE(kk_tek, NULL);
}
