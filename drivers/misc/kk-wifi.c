// SPDX-License-Identifier: GPL-2.0
/*
 * Kara Kutu G52 M3-0.1 (Wi-Fi M3 iskeleti): Hi1102 SDIO fonksiyon sürücüsü — YALNIZ BAĞLANMA, G/Ç YOK.
 *
 * PLAN-M3.md M3-0 madde 2 (kilitler) + madde 9 (`#error` listesi) + M3a'nın ön iskeleti:
 *   kilit 1  Kconfig CONFIG_KK_HI1102_PORT (varsayılan n; KK_HI1102_YUKLE ile birlikte seçilemez);
 *   kilit 2  cmdline kk_wifi.ac=1 (yoksa yalnız bir log satırı; sysfs bile oluşmaz);
 *   kilit 3  sysfs /sys/kernel/kk_wifi/baslat ← "1" (açılış başına 1; ikincisi -EBUSY) → sdio_register_driver.
 * Açılışta SDIO erişimi 0: sürücü kaydı yalnız tetikle. Probe yalnız çekirdek yapılarını okur (vendor/device/num,
 * kartın fonksiyon sayısı, host DT düğümünde `karakutu,sdio-yalniz`) ve bağlanır ya da -ENODEV döner;
 * sdio_claim_host / sdio_enable_func / sdio_readb / sdio_writeb / sdio_memcpy_* ÇAĞRILMAZ (build denetimi: nesnede yok).
 * Kart açma (Faz B) kk-sdio-kapi'nin işi olarak kalır (tek yazan). İndirme ve HCC (kk-hcc.h) M3a'da bağlanır.
 *
 * G54 M3a (CONFIG_KK_HI1102_M3A; Furkan onayı G54 (i)): probe bağlandıktan sonra (kimlik + id_table yeniden denetimi)
 * fn1'i kk-hi1102-yukle motoruna verir (kk_hi1102_m3a_baslat: A0-A9 = P-D/M2c kip 2 + A9 kesme servisi, A10 = hcc_enable
 * eşdeğeri + HCC alımı). Bu dosya yine SDIO G/Ç yapmaz (motor kk_h_yaz/okuma ilkellerinde). Not: 6.12 sdio_bus_probe
 * drv->probe'dan ÖNCE sdio_set_block_size(func, 0) çağırır (sdio_bus.c): min(func->max_blksize, host, 512) = 512 (P-D A9
 * satırı: tek bayt kipi tavanı 512) -> FBR1 0x110 <- 0x00, 0x111 <- 0x02 = O9 Y2 (H2) ile AYNI değer, +2 CMD52.
 * Yalnız QEMU (KK_HI1102_YUKLE_SAHTE): SDIO kartı yok; tetikte sahte kart kimliğiyle aynı karar işlevi çağrılır ve motor
 * func = NULL ile başlatılır (cihaz imajında derlenmez).
 */
#include <linux/bitops.h>
#include <linux/init.h>
#include <linux/kobject.h>
#include <linux/mmc/card.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sdio_func.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/sysfs.h>
#include "kk-wifi-karar.h"
#include "kk-hcc.h"	/* M3-0.1: yalnız derleme denetimi (saf işlevler; bu dilimde çağrılmaz) */

#if !IS_ENABLED(CONFIG_KK_SDIO_KAPI)
#error "kk-wifi: kart acma (Faz B) kk-sdio-kapi'de; o olmadan bu surucu anlamsiz"
#endif
#if IS_ENABLED(CONFIG_KK_HI1102_YUKLE) && !IS_ENABLED(CONFIG_KK_HI1102_M3A)
#error "kk-wifi: M3 imajlarinda kk-hi1102-yukle yalniz M3a motoru olarak (PLAN-M3 madde 9; ayni fn1'e iki sahip olmaz)"
#endif
#if IS_ENABLED(CONFIG_WILC1000_SDIO)
#error "kk-wifi: WILC1000_SDIO 0x0296/0x5347'ye baglanir; kapali kalmali"
#endif
#if (IS_ENABLED(CONFIG_CFG80211) || IS_ENABLED(CONFIG_FW_LOADER)) && !IS_ENABLED(CONFIG_KK_HI1102_M3B)
#error "kk-wifi (M3-0.1/M3a): cfg80211 ve FW_LOADER bu dilimde yok (M3b / hic)"
#endif
#if IS_ENABLED(CONFIG_KK_HI1102_M3A)
#include "kk-hi1102-yukle.h"
#endif

static bool kk_ac;
module_param_named(ac, kk_ac, bool, 0444);
MODULE_PARM_DESC(ac, "1 = sysfs tetigi olusur (varsayilan 0: hicbir sey)");

static DEFINE_MUTEX(kk_kilit);
static unsigned long kk_tetik;		/* bit 0: bu açılışta tetiklendi */
static bool kk_bir_kez;			/* bu açılışta bir fonksiyona bağlandı (ayrılsa da yeniden bağlanmaz) */
static struct sdio_func *kk_fn;
static char kk_durum[160] = "KAPALI";
static struct kobject *kk_kobj;

static const char *const kk_red_ad[KK_WIFI_RED_SAYI] = {
	[KK_WIFI_TAMAM] = "-",
	[KK_WIFI_RED_KIMLIK] = "kimlik",
	[KK_WIFI_RED_FONKSIYON] = "fonksiyon-1-degil",
	[KK_WIFI_RED_FONK_SAYI] = "fonksiyon-sayisi",
	[KK_WIFI_RED_HOST] = "host-sdio-yalniz-degil",
	[KK_WIFI_RED_IKINCI] = "ikinci-baglanma",
};

#if IS_ENABLED(CONFIG_KK_HI1102_M3A)
/* G54 M3a: bağlanan fn1'i motora ver (bir kez; kk_bir_kez probe'da set). Motorun dönüşü yalnız durum metnine yazılır. */
static void kk_wifi_motor(struct sdio_func *f)
{
	int r = kk_hi1102_m3a_baslat(f);

	mutex_lock(&kk_kilit);
	if (r)
		snprintf(kk_durum, sizeof(kk_durum), "BAGLANDI %s; M3a motoru BASLAMADI (%d: %s); G/C YOK", f ? sdio_func_id(f) : "sahte",
			 r, r == -ENODEV ? "kk_hi1102_yukle.ac=1 yok" : r == -EPERM ? "kip 2 izinli degil" : r == -EBUSY ? "zaten basladi" : "?");
	else
		snprintf(kk_durum, sizeof(kk_durum), "BAGLANDI %s; M3a motoru BASLADI (durum: kk-sdio-kapi yukle_durum)",
			 f ? sdio_func_id(f) : "sahte");
	mutex_unlock(&kk_kilit);
	pr_info("kk: hi1102 port: M3a motoru: %s (%d)\n", r ? "BASLAMADI" : "kuyrukta", r);
}
#endif

static int kk_wifi_probe(struct sdio_func *f, const struct sdio_device_id *id)
{
	struct mmc_host *h = f->card->host;
	bool yalniz = of_property_read_bool(mmc_dev(h)->of_node, "karakutu,sdio-yalniz");
	enum kk_wifi_red r;

	mutex_lock(&kk_kilit);
	r = kk_wifi_kimlik(f->vendor, f->device, f->num, f->card->sdio_funcs, yalniz, kk_bir_kez);
	if (r != KK_WIFI_TAMAM) {
		snprintf(kk_durum, sizeof(kk_durum), "RED %s: %s fn%u 0x%04x/0x%04x fonksiyon %u", kk_red_ad[r],
			 sdio_func_id(f), f->num, f->vendor, f->device, f->card->sdio_funcs);
		mutex_unlock(&kk_kilit);
		pr_warn("kk: hi1102 port: %s; G/C yok\n", kk_durum);
		return -ENODEV;
	}
#if IS_ENABLED(CONFIG_KK_HI1102_M3A)
	/* G54 M3a: id_table eşleşmesinin yeniden denetimi (sdio_match_device'ın verdiği girdi tam bizimki mi) */
	if (!id || id->vendor != KK_WIFI_VENDOR || id->device != KK_WIFI_DEVICE || id->class != (u8)SDIO_ANY_ID) {
		snprintf(kk_durum, sizeof(kk_durum), "RED id_table girdisi beklenen degil (0x%04x/0x%04x)",
			 id ? id->vendor : 0, id ? id->device : 0);
		mutex_unlock(&kk_kilit);
		pr_warn("kk: hi1102 port: %s; G/C yok\n", kk_durum);
		return -ENODEV;
	}
#endif
	kk_bir_kez = true;
	kk_fn = f;
	snprintf(kk_durum, sizeof(kk_durum), "BAGLANDI %s fn%u 0x%04x/0x%04x host %s (sdio-yalniz); G/C YOK",
		 sdio_func_id(f), f->num, f->vendor, f->device, mmc_hostname(h));
	mutex_unlock(&kk_kilit);
#if IS_ENABLED(CONFIG_KK_HI1102_M3A)
	pr_info("kk: hi1102 port: %s (M3a: motor kk-hi1102-yukle; blok 512 sdio_bus_probe'da)\n", kk_durum);
	kk_wifi_motor(f);
#else
	pr_info("kk: hi1102 port: %s (M3-0.1 iskelet; indirme + HCC M3a)\n", kk_durum);
#endif
	return 0;
}

static void kk_wifi_remove(struct sdio_func *f)
{
#if IS_ENABLED(CONFIG_KK_HI1102_M3A)
	kk_hi1102_m3a_ayril(f);		/* çalışan motoru durdur ve bekle (kart non-removable; olağan akışta çağrılmaz) */
#endif
	mutex_lock(&kk_kilit);
	if (kk_fn == f) {
		kk_fn = NULL;
		snprintf(kk_durum, sizeof(kk_durum), "AYRILDI %s (bu acilista yeniden baglanmaz)", sdio_func_id(f));
	}
	mutex_unlock(&kk_kilit);
	pr_info("kk: hi1102 port: ayrildi %s\n", sdio_func_id(f));
}

static const struct sdio_device_id kk_wifi_idler[] = {
	{ SDIO_DEVICE(KK_WIFI_VENDOR, KK_WIFI_DEVICE) },
	{ }
};

static struct sdio_driver kk_wifi_surucu = {
	.name = "kk-hi1102",
	.id_table = kk_wifi_idler,
	.probe = kk_wifi_probe,
	.remove = kk_wifi_remove,
};

#if IS_ENABLED(CONFIG_KK_HI1102_M3A) && IS_ENABLED(CONFIG_KK_HI1102_YUKLE_SAHTE)
/* YALNIZ QEMU (cihaz imajında derlenmez): sahte kartın kimliğiyle AYNI karar işlevi (kk_wifi_kimlik; host sdio-yalnız
 * sayılır) + id_table girdisiyle karşılaştırma; geçerse motor func = NULL ile başlar. */
static void kk_wifi_sahte_probe(void)
{
	struct kk_h_kart k;
	enum kk_wifi_red r;
	int e = kk_hs_kart(&k);

	mutex_lock(&kk_kilit);
	if (e) {
		snprintf(kk_durum, sizeof(kk_durum), "KAYITLI (SAHTE kart yok: %d)", e);
		mutex_unlock(&kk_kilit);
		pr_info("kk: hi1102 port: SAHTE probe: kart yok (%d); G/C yok\n", e);
		return;
	}
	r = kk_wifi_kimlik(k.fn_vendor, k.fn_device, k.fn_no, k.fonksiyon, true, kk_bir_kez);
	if (r == KK_WIFI_TAMAM && (k.fn_vendor != kk_wifi_idler[0].vendor || k.fn_device != kk_wifi_idler[0].device))
		r = KK_WIFI_RED_KIMLIK;
	if (r != KK_WIFI_TAMAM) {
		snprintf(kk_durum, sizeof(kk_durum), "RED %s: SAHTE fn%u 0x%04x/0x%04x fonksiyon %u", kk_red_ad[r], k.fn_no,
			 k.fn_vendor, k.fn_device, k.fonksiyon);
		mutex_unlock(&kk_kilit);
		pr_warn("kk: hi1102 port: %s; G/C yok\n", kk_durum);
		return;
	}
	kk_bir_kez = true;
	snprintf(kk_durum, sizeof(kk_durum), "BAGLANDI SAHTE fn%u 0x%04x/0x%04x (yalniz QEMU)", k.fn_no, k.fn_vendor, k.fn_device);
	mutex_unlock(&kk_kilit);
	pr_info("kk: hi1102 port: %s (M3a: motor kk-hi1102-yukle)\n", kk_durum);
	kk_wifi_motor(NULL);
}
#endif

static ssize_t baslat_store(struct kobject *ko, struct kobj_attribute *a, const char *b, size_t n)
{
	int r = kk_wifi_tetik_coz(b, n);

	if (r)
		return r;
	if (test_and_set_bit(0, &kk_tetik))
		return -EBUSY;
	mutex_lock(&kk_kilit);
	snprintf(kk_durum, sizeof(kk_durum), "KAYITLI (kart bekleniyor; Faz B kk-sdio-kapi ile)");
	mutex_unlock(&kk_kilit);
	r = sdio_register_driver(&kk_wifi_surucu);	/* eşleşen fn1 varsa probe burada çağrılır */
	if (r) {
		mutex_lock(&kk_kilit);
		snprintf(kk_durum, sizeof(kk_durum), "KAYIT HATASI %d", r);
		mutex_unlock(&kk_kilit);
		pr_warn("kk: hi1102 port: sdio_register_driver %d\n", r);
		return r;
	}
	pr_info("kk: hi1102 port: tetik: sdio surucusu kaydedildi (acilis basina 1)\n");
#if IS_ENABLED(CONFIG_KK_HI1102_M3A) && IS_ENABLED(CONFIG_KK_HI1102_YUKLE_SAHTE)
	kk_wifi_sahte_probe();		/* YALNIZ QEMU: SDIO kartı yok */
#endif
	return n;
}

static ssize_t durum_show(struct kobject *ko, struct kobj_attribute *a, char *b)
{
	ssize_t n;

	mutex_lock(&kk_kilit);
	n = sysfs_emit(b, "%s tetik=%d\n", kk_durum, test_bit(0, &kk_tetik) ? 1 : 0);
	mutex_unlock(&kk_kilit);
	return n;
}

static struct kobj_attribute kk_a_baslat = __ATTR_WO(baslat);
static struct kobj_attribute kk_a_durum = __ATTR_RO(durum);
static struct attribute *kk_attrs[] = { &kk_a_baslat.attr, &kk_a_durum.attr, NULL };
static const struct attribute_group kk_grup = { .attrs = kk_attrs };

static int __init kk_wifi_init(void)
{
	int r;

	if (!kk_ac) {
		pr_info("kk: hi1102 port: kapali (kk_wifi.ac yok); SDIO erisimi 0, sysfs yok\n");
		return 0;
	}
	kk_kobj = kobject_create_and_add("kk_wifi", kernel_kobj);
	if (!kk_kobj)
		return -ENOMEM;
	r = sysfs_create_group(kk_kobj, &kk_grup);
	if (r) {
		kobject_put(kk_kobj);
		kk_kobj = NULL;
		return r;
	}
	snprintf(kk_durum, sizeof(kk_durum), "HAZIR (tetik bekleniyor)");
	pr_info("kk: hi1102 port: hazir; acilista SDIO erisimi 0; tetik /sys/kernel/kk_wifi/baslat\n");
	return 0;
}
late_initcall(kk_wifi_init);
