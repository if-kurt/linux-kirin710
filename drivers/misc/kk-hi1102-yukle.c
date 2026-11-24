// SPDX-License-Identifier: GPL-2.0
/*
 * Kara Kutu G48 (Wi-Fi M2a): kk-hi1102-yukle — Hi1102 ROM'una en küçük firmware yükleyicisi.
 * Tasarım: work/g47/wifi-m2/TASARIM-M2.md (§3, §5). 4.9 akışı: refs/.../hisi/platform/pm/plat_firmware.c.
 *
 * KİLİTLER: (1) Kconfig KK_HI1102_YUKLE (varsayılan n); (2) cmdline kk_hi1102_yukle.ac=1 (varsayılan 0). Biri yoksa
 * sysfs dosyası yok, tek log satırı, hiçbir SDIO erişimi yok. Kip 2 (tam indirme) ayrıca (3) cmdline
 * kk_hi1102_yukle.kip_azami=2 ister (varsayılan 1) ve yalnız gerçek firmware (sha = 4.9) gömülüyse derlenir
 * (yer tutucu firmware'li cihaz derlemesinde kip 2 kodu YOK; QEMU sahtesinde var).
 *
 * AÇILIŞTA ERİŞİM YOK: kapı probe'u yalnız kk_hi1102_ekle()'yi çağırır -> iki sysfs özniteliği. SDIO fonksiyon sürücüsü
 * KAYDEDİLMEZ (kk-sdio-kapi.c #error listesi aynen; sdio_func kapının kart denetiminden alınır, get_device ile tutulur).
 * Firmware dosya sisteminden okunmaz (FW_LOADER=n): üç ikili imaja gömülüdür (kk-hi1102-fw.S), boy + CRC32 tabloyla
 * bağlıdır, sha256 derleme anında denetlenir (build.sh). Komut tablosu derleme anında üretilir (kk-hi1102-tablo.h);
 * çalışma anında metin ayrıştırma/biçimleme YOK.
 *
 * TETİK  /sys/bus/platform/devices/<kapi>/yukle  (0200): "1" = el sıkışma (READM efuse EC + VERSION), "2" = tam indirme.
 *   Başka değer -EINVAL, izinsiz kip 2 -EPERM (hak harcanmaz). Açılış başına 1 (ikinci tetik -EBUSY, erişim yok).
 *   İş bir iş kuyruğunda yürür; store hemen döner. DUR'da da hak harcanmış olur. GERİ ALMA YAZMASI YOK.
 * DURUM  .../yukle_durum (0444): yalnız bellekteki alanlar (donanıma erişim yok; kural 15a).
 *
 * TEK YAZMA KAPISI kk_h_yaz(): Hi1102'ye giden her CMD52/CMD53 yazması buradan geçer ve derleme anı listesiyle sınırlıdır
 * (TASARIM §5.2 H1-H6): H1 CCCR 0x02 fn1 etkin (1), H2 FBR1 blok 512 (1 çağrı = 2 CMD52), H3 F1 0x08 <- okunan maske
 * (⊆0x07; yanıt başına 1), H4 F1 0x09 <- 0x07 (1), H5 F0 0xF0 <- 0x00 (1), H6 F1 0x00 FIFO CMD53 yazma: tx tamponunun
 * içeriği tablodaki metin / gömülü ikilinin tablodaki dilimi / sıfır cali ile bayt bayt aynı olmalı, dolgu sıfır.
 * Yeni SoC yazması YOK (MMIO 0, PMIC 0, GPIO 0).
 *
 * G51 M2c-A (Furkan onayı "M2c ikinci deneme: A"): kip 2'de (1) A6'dan sonra 4.9 sırasıyla READM 0x50000744 64 (F3; yanıt
 * yalnız kaydedilir), (2) 6 × READM <adr> 64 RAM geri okuması, beklenen firmware/cali baytlarıyla memcmp (G52 M2c-A':
 * CALI'den sonra, R6'dan önce; 6/6 tutmazsa R6-R12 ve QUIT GÖNDERİLMEZ -> DUR), (3) A9 son tarihi ktime ile 20 sn
 * (4.9 HOST_WAIT_BOTTOM_INIT_TIMEOUT), yoklama aralığı ve döngü gövdesi G50 ile aynı, (4) A9 sonunda (HAZIR da DUR da)
 * teşhis: ext 80 B (ilk/ilk değişim/son), F1 0x08, CCCR 0x03, CCCR 0x05 (yalnız CMD52 okuma), süre ve yoklama sayısı.
 * Yeni türde yazma YOK.
 *
 * G53 P-D (Furkan onayı: "A9 -> 4.9 oal_sdio_do_isr eşdeğeri. Her ileti (msg != 0) için F1 0x2B okuma (ACK). DREADY'de F1
 * FIFO'dan xfer_count kadar CMD53 okuma (<= 512 B). Üst sınırlar: <= 8 ACK, <= 8 paket. RERROR, panik iletisi, tutarsız
 * uzunluk ya da sınır aşımında DUR. Çipe yeni yazma yok."): A9 her yoklamada ext'i okur; int_stat & 0x07 != 0 ise 4.9
 * sırasıyla MFARM + msg != 0 -> CMD52 OKUMA F1 0x2B (ACK), DREADY -> CMD53 OKUMA F1 0x00 (FIFO, sabit adres) xfer_count
 * bayt, sonra MFARM + msg b0 -> HAZIR. Bütün DUR denetimleri o turun okumalarından ÖNCE yapılır. kk_h_yaz çağrılmaz.
 *
 * G54 M3a (Furkan onayı G54 (i); CONFIG_KK_HI1102_M3A): motor kk-wifi.c'nin SDIO fonksiyon sürücüsüyle çalışır. Tetik
 * /sys/kernel/kk_wifi/baslat (tek; açılış başına 1) -> sdio_register_driver -> probe (0x0296/0x5347, fn1, sdio-yalnız host;
 * yeniden denetim) -> kk_hi1102_m3a_baslat(fn1) -> iş kuyruğu: A0-A8 (P-D/M2c kip 2, bayt bayt aynı) -> A9 (P-D kesme
 * servisi, aynı; ek: her FIFO paketinin HCC başlığı çözülür, OAM sayılır, ilk paketin yükü TAM, sonrakilerin ilk 128 B'ı
 * kayda, paket bırakılır) -> A10 (hcc_enable eşdeğeri: yalnız host durumu HCC_ON; ardından KK_H_A10_MS boyunca aynı kesme
 * servisiyle yoklama: ACK okuması + FIFO okuması + HCC çözümü; yazma YOK). "yukle" dosyası bu derlemede YOK (yalnız
 * yukle_durum). Port kodu (hi1102_plat/hi1102_wifi) bağlanmaz: cfg80211/wiphy/netdev/tx yolu DERLENMEZ. Yazmalar yine yalnız
 * kk_h_yaz (H1-H6 = O9-M3a Y1-Y6, Y5 dahil); Y7 (0xF0<-0) A5'te 1 kez; Y8 (HCC tx) 0; Y9-Y12 kodda yok.
 *
 * G49 M2b: CONFIG_KK_HI1102_YALNIZ_KIP1=y derlemesinde yalnız kip 1 (el sıkışma) vardır: indirme kodu (kk_h_mesaj,
 * kk_h_veri, A8, A9), gömülü firmware (kk-hi1102-fw.o), tam komut tablosu ve parça tabloları, WRITEM beyaz listesi
 * DERLENMEZ. Kapının H6 tablosu yalnız iki metindir (READM 0x50000770 4, VERSION); veri (FILES/CALI) yolu yoktur.
 * tx tamponu 512 B; ext tamponu ayrılmaz. "yukle" <- 2 her durumda -EPERM.
 */
#include <linux/crc32.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/ktime.h>
#include <linux/mmc/card.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sdio_func.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/unaligned.h>
#include <linux/workqueue.h>
#include "kk-hi1102-yukle.h"
#if IS_ENABLED(CONFIG_KK_HI1102_PORT) && !IS_ENABLED(CONFIG_KK_HI1102_M3A)
#error "kk-hi1102-yukle: kk-wifi (PORT) ile birlikte yalniz M3a derlemesinde (ayni fn1'e iki sahip olmaz)"
#endif
#if IS_ENABLED(CONFIG_KK_HI1102_M3A)
#if IS_ENABLED(CONFIG_KK_HI1102_YALNIZ_KIP1) || !IS_ENABLED(CONFIG_KK_HI1102_PORT)
#error "kk-hi1102-yukle (M3a): kip 2 derlemesi ve kk-wifi gerekli"
#endif
#if ((IS_ENABLED(CONFIG_CFG80211) || IS_ENABLED(CONFIG_FW_LOADER)) && !IS_ENABLED(CONFIG_KK_HI1102_M3B)) || IS_ENABLED(CONFIG_WILC1000_SDIO)
#error "kk-hi1102-yukle (M3a): cfg80211, FW_LOADER ve WILC1000 kapali kalir"
#endif
#include "kk-hcc.h"
#if IS_ENABLED(CONFIG_KK_HI1102_M3B)
#include "kk-hi1102-port/kk_inc/kk_m3b_kapi.h"
#endif		/* G54 M3a: D2H paketlerinin HCC başlık çözümü (saf işlevler, M3-0.1) */
#endif
#if IS_ENABLED(CONFIG_KK_HI1102_YALNIZ_KIP1)
#define KK_H_YALNIZ_KIP1 1	/* denetim.h: WRITEM beyaz listesi / RAM bölgeleri / tam tablo denetimi derlenmez */
#endif
#include "kk-hi1102-denetim.h"
#if IS_ENABLED(CONFIG_KK_HI1102_YALNIZ_KIP1)
#include "kk-hi1102-tablo-kip1.h"	/* yalnız READM + VERSION */
#define KK_H_KIP2_DERLI 0	/* G49 M2b: kip 2 kodu, firmware ve tam tablo derlenmez */
#define KK_H_FW_DURUM	"YOK"
#define KK_H_TX_KIP1	KK_H_BLK	/* en uzun kip 1 teli 20 B; 512 B tek blok */
#else
#include <crypto/sha2.h>		/* G50 M2c: gömülü ikililerin çalışma anı sha256 denetimi (lib/crypto) */
#include "kk-hi1102-tablo.h"
#include "kk-hi1102-fw-ozet.h"	/* CONFIG_KK_HI1102_FW_DIR'den (-I); üreteç: boy, CRC32, sha256, gerçek/yer tutucu */
#define KK_H_PARCA_ONAYLI 261632u	/* G50 M2c: onay (O4 genişlemesi) yalnız 261 632 B; yarılama/başka parça -> A0 DUR */
/* G50 M2c: kip 2 A8'e yalnız sürüm metni 4.9 (debian-dmesg.txt:5720) ve G49 M2b cihaz sonucuyla TAM aynıysa geçer */
#define KK_H_SURUM_TAM	"Hi1102V100R001C01B161 May 25 2015"

#if IS_ENABLED(CONFIG_KK_HI1102_YUKLE_SAHTE) || KK_H_FW_GERCEK
#define KK_H_KIP2_DERLI 1
#else
#define KK_H_KIP2_DERLI 0	/* yer tutucu firmware'li cihaz derlemesi: kip 2 kodu derlenmez */
#endif
#define KK_H_FW_DURUM	(KK_H_FW_GERCEK ? "gercek" : "YER-TUTUCU")
#endif

static bool kk_h_ac;
module_param_named(ac, kk_h_ac, bool, 0444);
MODULE_PARM_DESC(ac, "1 = yukle/yukle_durum dosyalari acilir (varsayilan 0: hicbir sey yok)");
static unsigned int kk_h_kip_azami = 1;
module_param_named(kip_azami, kk_h_kip_azami, uint, 0444);
MODULE_PARM_DESC(kip_azami, "izinli en yuksek kip (1 = yalniz el sikisma; 2 = tam indirme da)");
#if !IS_ENABLED(CONFIG_KK_HI1102_YALNIZ_KIP1)
static unsigned int kk_h_parca_param = 261632;
module_param_named(parca, kk_h_parca_param, uint, 0444);
MODULE_PARM_DESC(parca, "FILES parca boyu: 261632 (varsayilan, 4.9) | 32768 | 4096; baska deger -> 261632");

extern const u8 kk_h_fw_0[], kk_h_fw_0_son[], kk_h_fw_1[], kk_h_fw_1_son[], kk_h_fw_2[], kk_h_fw_2_son[];
#endif

/* yazma kapısı türleri (H1-H6) */
enum { KK_HY_ETKIN = 1, KK_HY_BLOK, KK_HY_TEMIZLE, KK_HY_INTAC, KK_HY_UYANDIR, KK_HY_FIFO };
static const char * const kk_hy_ad[] = { "?", "H1 CCCR 0x02 fn1 etkin", "H2 FBR1 blok", "H3 F1 0x08 temizle",
					 "H4 F1 0x09 kesme", "H5 F0 0xF0 uyandir", "H6 F1 FIFO CMD53" };

#define KK_H_RX		512u	/* xfer_count üst sınırı (F7) */
#define KK_H_EXT_ADR	0x30u
#define KK_H_EXT_BOY	80u	/* struct hisdio_extend_func (OIF:63-70) */
#define KK_H_KART_TUR	200	/* 100 ms × 200 = 20 sn */
#define KK_H_HAZIR_MS	20000	/* G51: A9 son tarihi = 4.9 HOST_WAIT_BOTTOM_INIT_TIMEOUT (plat_pm_wlan.h:22) */
/* G53 P-D: 4.9 func1 kesme bitleri (oal_sdio.h:37-39), func1_int_mask = DREADY|RERROR|MFARM (oal_sdio.h:43, OSH:2762) */
#define KK_H_INT_DREADY	0x01u
#define KK_H_INT_RERROR	0x02u
#define KK_H_INT_MFARM	0x04u
#define KK_H_INT_MASKE	0x07u
#define KK_H_MSG_READY	0x01u	/* D2H_MSG_WLAN_READY = 0 (oal_sdio_comm.h:37) */
#define KK_H_MSG_PANIK	0x80u	/* D2H_MSG_DEVICE_PANIC = 7 (oal_sdio_comm.h:44) */
#define KK_H_A9_ACK_UST	8	/* Furkan onayı: <= 8 ACK (F1 0x2B CMD52 okuma) */
#define KK_H_A9_VERI_UST 8	/* Furkan onayı: <= 8 paket (F1 FIFO CMD53 okuma) */
#define KK_H_A9_KESME_UST 16	/* eller (günlük sınırı): int_stat != 0 olan tur <= 16 (8 ACK + 8 paket ayrı turlarda) */
#define KK_H_A9_HEX	64u	/* beyin: her paketin ilk 64 B'ı kayda */
#if IS_ENABLED(CONFIG_KK_HI1102_M3A)
#if !KK_H_KIP2_DERLI
#error "kk-hi1102-yukle (M3a): kip 2 (gercek firmware ya da QEMU sahte) gerekli"
#endif
#define KK_H_A10_MS	30000	/* G54 M3a: A10 HCC alım evresi (READY'den sonra; oturum bütçesi 720 sn içinde) */
#endif

/* tablo adım indeksleri (G51: tam tabloda 1 = READM efuse, 2 = VERSION; kip 1 tablosunda 1 = VERSION) */
#define KK_H_AI_EC	0
#if IS_ENABLED(CONFIG_KK_HI1102_YALNIZ_KIP1)
#define KK_H_AI_SURUM	1
#else
#define KK_H_AI_EFUSE	1
#define KK_H_AI_SURUM	2
#define KK_H_AI_A8	3
#endif

struct kk_h {
	struct device *dev;
	struct work_struct is;
	struct mutex kilit;		/* durum metinleri */
	int kip;
	bool calisiyor;
	const char *adim, *sonuc;
	char hata[96], surum[65];
	int c52, c53, red, tetik_red, surum_deneme;
	u32 tel, sure_ms;
	int geri_tuttu, geri_n;		/* G51: RAM geri okuması (G52 A': CALI sonrası, R6 öncesi) */
	ktime_t quit_t;			/* G51: QUIT gönderildiği an (A9 teşhisi) */
	int pi;				/* parça seçeneği */
	u32 tx_kap;
	struct sdio_func *func;
	u8 *tx, *rx, *ext;
	/* kapı sayaçları ve üst sınırları */
	int n_etkin, n_blok, n_temizle, n_intac, n_uyandir, n_fifo, temizle_ust, fifo_ust;
	/* G53 P-D (yapının SONUNDA: önceki alanların ofsetleri G52 ile aynı kalır) */
	int a9_kesme, a9_ack, a9_veri;	/* int_stat != 0 turu, F1 0x2B okuması, FIFO okuması */
	u32 a9_veri_bayt;
#if IS_ENABLED(CONFIG_KK_HI1102_M3A)
	/* G54 M3a (yapının SONUNDA) */
	struct sdio_func *m3a_fn;	/* probe'da bağlanan fn1 (sahte: NULL); A0 kapının fn1'iyle karşılaştırır */
	bool durdur;			/* remove: A10 döngüsü durur */
	int hcc_on;			/* A10 hcc_enable eşdeğeri */
	int a10_kesme, a10_ack, a10_veri, a10_yoklama;
	u32 a10_veri_bayt, a10_ms, credit_son;
	int pk_n, pk_oam, pk_diger, pk_gecersiz;	/* HCC paketleri (A9 + A10) */
#endif
};

static unsigned long kk_h_tetik_bit;
static struct kk_h *kk_h_g;

#define HD(fmt, ...)	dev_info(h->dev, "kk: hi1102: " fmt "\n", ##__VA_ARGS__)
#define HU(fmt, ...)	dev_warn(h->dev, "kk: hi1102: " fmt "\n", ##__VA_ARGS__)

#if !IS_ENABLED(CONFIG_KK_HI1102_YALNIZ_KIP1)
static const u8 *kk_h_fw(int d, u32 *boy)
{
	static const u8 * const b[3] = { kk_h_fw_0, kk_h_fw_1, kk_h_fw_2 };
	static const u8 * const s[3] = { kk_h_fw_0_son, kk_h_fw_1_son, kk_h_fw_2_son };

	*boy = s[d] - b[d];
	return b[d];
}

static u32 kk_h_crc(const u8 *p, u32 n)
{
	return ~crc32_le(~0u, p, n);	/* = zlib crc32 */
}

/* G50 M2c: gömülü ikilinin sha256'sı (onaltılık, küçük harf) özetteki değerle aynı mı (özet build.sh'ta G16/B2 ile denetlenir) */
static bool kk_h_sha_tuttu(int d, const u8 *p, u32 n, char *hex)
{
	u8 o[SHA256_DIGEST_SIZE];

	sha256(p, n, o);
	bin2hex(hex, o, sizeof(o));
	hex[2 * SHA256_DIGEST_SIZE] = 0;
	if (kk_hs_sha_boz(d))
		hex[0] ^= 0x01;		/* yalnız QEMU sahte: uyuşmazlık dalı */
	return !strcmp(hex, kk_h_fw_sha256[d]);
}
#endif

static void kk_h_durum(struct kk_h *h, const char *adim)
{
	mutex_lock(&h->kilit);
	h->adim = adim;
	mutex_unlock(&h->kilit);
}

/* ---- ilkel katman: SDIO çağrıları YALNIZ burada (yazmalar yalnız kk_h_yaz içinden) ---- */
#if IS_ENABLED(CONFIG_KK_HI1102_YUKLE_SAHTE)
#define P_ENABLE(h)			kk_hs_enable_func()
#define P_BLOK(h, b)			kk_hs_set_block_size(b)
#define P_WRITEB(h, v, a, e)		kk_hs_writeb(1, v, a, e)
#define P_F0_WRITEB(h, v, a, e)		kk_hs_writeb(0, v, a, e)
#define P_WRITESB(h, a, b, n)		kk_hs_writesb(a, b, n)
#define P_READB(h, a, e)		kk_hs_readb(1, a, e)
#define P_READL(h, a, e)		kk_hs_readl(a, e)
#define P_READSB(h, d, a, n)		kk_hs_readsb(d, a, n)
#define P_FROMIO(h, d, a, n)		kk_hs_memcpy_fromio(d, a, n)
#define P_F0_READB(h, a, e)		kk_hs_f0_readb(a, e)
#define P_CLAIM(h)			do { } while (0)
#define P_RELEASE(h)			do { } while (0)
#else
#define P_ENABLE(h)			sdio_enable_func((h)->func)
#define P_BLOK(h, b)			sdio_set_block_size((h)->func, b)
#define P_WRITEB(h, v, a, e)		sdio_writeb((h)->func, v, a, e)
#define P_F0_WRITEB(h, v, a, e)		sdio_f0_writeb((h)->func, v, a, e)
#define P_WRITESB(h, a, b, n)		sdio_writesb((h)->func, a, b, n)
#define P_READB(h, a, e)		sdio_readb((h)->func, a, e)
#define P_READL(h, a, e)		sdio_readl((h)->func, a, e)
#define P_READSB(h, d, a, n)		sdio_readsb((h)->func, d, a, n)
#define P_FROMIO(h, d, a, n)		sdio_memcpy_fromio((h)->func, d, a, n)
#define P_F0_READB(h, a, e)		sdio_f0_readb((h)->func, a, e)	/* G51: yalnız A9 teşhisi, CCCR 0x03/0x05 */
#define P_CLAIM(h)			sdio_claim_host((h)->func)
#define P_RELEASE(h)			sdio_release_host((h)->func)
#endif

/* H6 içeriği: beklenen kaynak tablodan; tx[0..n) aynı ve tx[n..tel) sıfır olmalı */
static const char *kk_h_fifo_denetle(struct kk_h *h, int ai, int pi, int veri, u32 *tel)
{
	const struct kk_h_adim *a;
	const u8 *kaynak = NULL;
	u32 n, t, i;

	if (ai < 0 || ai >= KK_H_ADIM_SAYI)
		return "adim disi";
	a = &kk_h_tablo[ai];
	if (a->kip > h->kip)
		return "adim bu kipte yok";
	if (h->n_fifo >= h->fifo_ust)
		return "FIFO yazma sayisi asildi";
#if IS_ENABLED(CONFIG_KK_HI1102_YALNIZ_KIP1)
	/* G49 M2b: yalnız tablodaki iki metin (READM, VERSION); veri yolu, parça ve ikili YOK */
	(void)pi;
	if (veri)
		return "veri yolu yok (kip 1 derlemesi)";
	if (!a->metin)
		return "metinsiz adim";
	kaynak = (const u8 *)a->metin;
	n = a->metin_len;
	t = KK_H_HIZALA(n);
#else
	if (!veri) {
		if (a->tur == KK_HT_FILES) {
			if (pi < 0 || pi >= kk_h_parca_sayi[h->pi] || kk_h_parcalar[h->pi][pi].dosya != a->dosya)
				return "parca disi";
			kaynak = (const u8 *)kk_h_parcalar[h->pi][pi].metin;
			n = kk_h_parcalar[h->pi][pi].metin_len;
		} else if (a->metin) {
			kaynak = (const u8 *)a->metin;
			n = a->metin_len;
		} else {
			return "metinsiz adim";
		}
		t = KK_H_HIZALA(n);
	} else if (a->tur == KK_HT_FILES) {
		const struct kk_h_parca *p;
		u32 boy;

		if (pi < 0 || pi >= kk_h_parca_sayi[h->pi] || kk_h_parcalar[h->pi][pi].dosya != a->dosya)
			return "parca disi";
		p = &kk_h_parcalar[h->pi][pi];
		kaynak = kk_h_fw(p->dosya, &boy);
		if (p->ofs + p->n > boy)
			return "dilim ikili disi";
		kaynak += p->ofs;
		n = p->n;
		t = p->tel;
	} else if (a->tur == KK_HT_CALI) {
		n = KK_H_CALI_ANLAMLI;		/* kaynak NULL: hepsi sıfır */
		t = KK_H_CALI_TEL;
	} else {
		return "verisiz adim";
	}
#endif
	if (t > h->tx_kap || t != KK_H_HIZALA(n))
		return "tel boyu";
	if (kaynak ? memcmp(h->tx, kaynak, n) != 0 : memchr_inv(h->tx, 0, n) != NULL)
		return "icerik tablo/ikili ile ayni degil";
	for (i = n; i < t; i++)
		if (h->tx[i])
			return "dolgu sifir degil";
	*tel = t;
	return NULL;
}

/*
 * TEK YAZMA KAPISI. tur: KK_HY_*; deger: CMD52 değeri / blok boyu; ai/pi/veri: H6 için tablo adımı, parça, veri mi.
 * Dönüş 0 tamam, -EPERM kapı reddi (YAZILMADI), diğer: SDIO hatası.
 */
static noinline int kk_h_yaz(struct kk_h *h, int tur, u32 deger, int ai, int pi, int veri)
{
	const char *red = NULL;
	u32 tel = 0;
	int e = 0;

	kk_hs_kapi_boz(&tur, &deger, &ai, &veri, h->tx, h->tx_kap);	/* yalnız QEMU sahte: kapı mutasyon testi */
	if (!h->calisiyor)
		red = "tetik disi";
	else
		switch (tur) {
		case KK_HY_ETKIN:
			if (h->n_etkin)
				red = "ikinci kez";
			break;
		case KK_HY_BLOK:
			if (h->n_blok || deger != KK_H_BLK)
				red = h->n_blok ? "ikinci kez" : "blok 512 degil";
			break;
		case KK_HY_TEMIZLE:
			if (!deger || (deger & ~0x07u))
				red = "maske 0x07 disi";
			else if (h->n_temizle >= h->temizle_ust)
				red = "temizleme sayisi asildi";
			break;
		case KK_HY_INTAC:
			if (h->n_intac || deger != 0x07)
				red = h->n_intac ? "ikinci kez" : "deger 0x07 degil";
			break;
		case KK_HY_UYANDIR:
			if (h->n_uyandir || deger != 0x00)
				red = h->n_uyandir ? "ikinci kez" : "deger 0x00 degil";
			break;
		case KK_HY_FIFO:
			red = kk_h_fifo_denetle(h, ai, pi, veri, &tel);
			break;
		default:
			red = "bilinmeyen tur";
			tur = 0;
		}
	if (red) {
		h->red++;
		HU("KAPI REDDI (#%d): %s deger 0x%x adim %d parca %d veri %d: %s; YAZILMADI -> DUR", h->red,
		   kk_hy_ad[tur >= 0 && tur <= KK_HY_FIFO ? tur : 0], deger, ai, pi, veri, red);
		snprintf(h->hata, sizeof(h->hata), "kapi reddi: %s", red);
		return -EPERM;
	}
	switch (tur) {
	case KK_HY_ETKIN:
		h->n_etkin++;
		h->c52++;
#if !IS_ENABLED(CONFIG_KK_HI1102_YUKLE_SAHTE)
		h->func->enable_timeout = 1000;
#endif
		e = P_ENABLE(h);
		break;
	case KK_HY_BLOK:
		h->n_blok++;
		h->c52 += 2;
		e = P_BLOK(h, KK_H_BLK);
		break;
	case KK_HY_TEMIZLE:
		h->n_temizle++;
		h->c52++;
		P_WRITEB(h, (u8)deger, 0x08, &e);
		break;
	case KK_HY_INTAC:
		h->n_intac++;
		h->c52++;
		P_WRITEB(h, 0x07, 0x09, &e);
		break;
	case KK_HY_UYANDIR:
		h->n_uyandir++;
		h->c52++;
		P_F0_WRITEB(h, 0x00, 0xF0, &e);
		break;
	case KK_HY_FIFO:
		h->n_fifo++;
		h->c53++;
		h->tel += tel;
		e = P_WRITESB(h, 0x00, h->tx, tel);
		break;
	}
	if (e)
		snprintf(h->hata, sizeof(h->hata), "%s SDIO hatasi %d", kk_hy_ad[tur], e);
	return e;
}

/* ---- okumalar (yalnız şu adresler): CMD52 F1 0x08, 0x2B; CMD53 F1 0x0C (4 B), 0x00 FIFO (1..512), 0x30 (80 B);
 *      G51: CMD52 F0 (CCCR) 0x03 IORx, 0x05 INTx (yalnız A9 sonunda teşhis);
 *      G53 P-D: A9'da F1 0x2B her MFARM + msg != 0 turunda (<= 8), F1 0x00 FIFO her DREADY turunda xfer bayt (<= 8) ---- */
static int kk_h_yanit(struct kk_h *h, int ai, u32 *xn)
{
	ktime_t son = ktime_add_ms(ktime_get(), 200);	/* READ_MEG_TIMEOUT (PFH:40) */
	u32 x;
	int e = 0;
	u8 v;

	for (;;) {
		v = P_READB(h, 0x08, &e);
		if (e) {
			snprintf(h->hata, sizeof(h->hata), "F1 0x08 okuma hatasi %d", e);
			return e;
		}
		if (v & 0x07)
			break;
		if (ktime_after(ktime_get(), son)) {
			snprintf(h->hata, sizeof(h->hata), "yanit yok (200 ms; F1 0x08 = 0x%02x)", v);
			return -ETIMEDOUT;
		}
		usleep_range(200, 500);		/* F6: 4.9 uykusuz cpu_relax */
	}
	e = kk_h_yaz(h, KK_HY_TEMIZLE, v & 0x07, ai, -1, 0);
	if (e)
		return e;
	x = P_READL(h, 0x0C, &e);
	if (e) {
		snprintf(h->hata, sizeof(h->hata), "F1 0x0C okuma hatasi %d", e);
		return e;
	}
	if (x < 1 || x > KK_H_RX) {	/* F7: 4.9 sınırsız kzalloc(xfer_count + 1) */
		snprintf(h->hata, sizeof(h->hata), "xfer_count %u (1..512 disi)", x);
		return -EPROTO;
	}
	e = P_READSB(h, h->rx, 0x00, x);
	if (e) {
		snprintf(h->hata, sizeof(h->hata), "FIFO okuma hatasi %d", e);
		return e;
	}
	h->rx[x] = 0;
	*xn = x;
	return 0;
}

static void kk_h_tx_hazirla(struct kk_h *h, const void *src, u32 n, u32 tel)
{
	memset(h->tx, 0, tel);
	if (src)
		memcpy(h->tx, src, n);
}

#if KK_H_KIP2_DERLI
/* mesaj gönder (+ yanıt bekle ve önek karşılaştır) */
static int kk_h_mesaj(struct kk_h *h, int ai, int pi, const char *metin, u32 n, const char *bekle)
{
	u32 x;
	int e;

	kk_h_tx_hazirla(h, metin, n, KK_H_HIZALA(n));
	e = kk_h_yaz(h, KK_HY_FIFO, 0, ai, pi, 0);
	if (e || !bekle)
		return e;
	e = kk_h_yanit(h, ai, &x);
	if (e)
		return e;
	if (strncmp(h->rx, bekle, strlen(bekle))) {
		snprintf(h->hata, sizeof(h->hata), "yanit '%.40s' (beklenen '%s')", h->rx, bekle);
		return -EPROTO;
	}
	return 0;
}

static int kk_h_veri(struct kk_h *h, int ai, int pi, const u8 *src, u32 n, u32 tel)
{
	u32 x;
	int e;

	kk_h_tx_hazirla(h, src, n, tel);
	e = kk_h_yaz(h, KK_HY_FIFO, 0, ai, pi, 1);
	if (e)
		return e;
	e = kk_h_yanit(h, ai, &x);
	if (e)
		return e;
	if (strncmp(h->rx, "FILES OK", 8)) {
		snprintf(h->hata, sizeof(h->hata), "yanit '%.40s' (beklenen 'FILES OK')", h->rx);
		return -EPROTO;
	}
	return 0;
}

#endif

/* A6: efuse EC (4.9 read_efuse_ec_version; F3: efuse 64 READM'i YOK) */
static int kk_h_a6(struct kk_h *h)
{
	const struct kk_h_adim *a = &kk_h_tablo[KK_H_AI_EC];
	u32 x;
	int e;

	kk_h_durum(h, "A6 READM");
	kk_h_tx_hazirla(h, a->metin, a->metin_len, KK_H_HIZALA(a->metin_len));
	e = kk_h_yaz(h, KK_HY_FIFO, 0, KK_H_AI_EC, -1, 0);
	if (!e)
		e = kk_h_yanit(h, KK_H_AI_EC, &x);
	if (e) {
		HU("A6 READM 0x%08x %u: %s -> DUR", a->adr, a->deger, h->hata);
		return e;
	}
	if (x < 2 || (h->rx[1] & 3) != 1) {
		snprintf(h->hata, sizeof(h->hata), "efuse EC %s (bayt %u, buf[1]=0x%02x)", x < 2 ? "kisa" : "V100", x,
			 x < 2 ? 0 : h->rx[1]);
		HU("A6 READM ec: %s; V100 dali hic sinanmadi -> DUR", h->hata);
		return -EPROTO;
	}
	HD("A6 READM ec: %u B %*ph; buf[1]&3 = 1 -> V120", x, (int)min_t(u32, x, 8), h->rx);
	return 0;
}

#if KK_H_KIP2_DERLI
/*
 * A6b (G51, yalnız kip 2): READM 0x50000744 64 — 4.9 store_efuse_info -> get_efuse_from_device (plat_efuse.c:110-128,
 * 186-214; read_efuse_ec_version'dan sonra, check_version'dan önce: plat_firmware.c:1794, debian-dmesg.txt:5713-5715).
 * Yanıt içeriği YALNIZ kaydedilir (karar vermez); protokol hatası (yanıt yok / SDIO hatası) F8 gereği DUR.
 */
static int kk_h_a6b(struct kk_h *h)
{
	const struct kk_h_adim *a = &kk_h_tablo[KK_H_AI_EFUSE];
	char hx[2 * KK_H_READM64 + 1];
	u32 x;
	int e;

	kk_h_durum(h, "A6b READM efuse");
	kk_h_tx_hazirla(h, a->metin, a->metin_len, KK_H_HIZALA(a->metin_len));
	e = kk_h_yaz(h, KK_HY_FIFO, 0, KK_H_AI_EFUSE, -1, 0);
	if (!e)
		e = kk_h_yanit(h, KK_H_AI_EFUSE, &x);
	if (e) {
		HU("A6b READM 0x%08x %u: %s -> DUR", a->adr, a->deger, h->hata);
		return e;
	}
	bin2hex(hx, h->rx, min_t(u32, x, KK_H_READM64));
	hx[2 * min_t(u32, x, KK_H_READM64)] = 0;
	HD("A6b READM 0x%08x %u (efuse; 4.9 store_efuse_info, F3): %u B %s%s (yalniz kayit)", a->adr, a->deger, x, hx,
	   x > KK_H_READM64 ? " ..." : "");
	return 0;
}
#endif

/* A7: VERSION (4.9 check_version: 3 deneme, gönder -> 1 ms -> oku, önek CfgVersion) */
static int kk_h_a7(struct kk_h *h)
{
	const struct kk_h_adim *a = &kk_h_tablo[KK_H_AI_SURUM];
	u32 x;
	int e = -EPROTO, i;

	kk_h_durum(h, "A7 VERSION");
	for (i = 0; i < 3; i++) {
		h->surum_deneme++;
		kk_h_tx_hazirla(h, a->metin, a->metin_len, KK_H_HIZALA(a->metin_len));
		e = kk_h_yaz(h, KK_HY_FIFO, 0, KK_H_AI_SURUM, -1, 0);
		if (e == -EPERM)
			return e;
		if (e)
			continue;
		msleep(1);
		e = kk_h_yanit(h, KK_H_AI_SURUM, &x);
		if (e == -EPERM)
			return e;
		if (e) {
			HU("A7 VERSION deneme %d: %s", i + 1, h->hata);
			continue;
		}
		strscpy(h->surum, h->rx, sizeof(h->surum));
		if (!strncmp(h->rx, KK_H_SURUM_ONEK, strlen(KK_H_SURUM_ONEK))) {
			HD("A7 VERSION '%s' onek TUTTU (%s; deneme %d)", h->surum, KK_H_SURUM_ONEK, i + 1);
			return 0;
		}
		snprintf(h->hata, sizeof(h->hata), "surum '%.40s' onek %s degil", h->surum, KK_H_SURUM_ONEK);
		HU("A7 VERSION deneme %d: %s", i + 1, h->hata);
		e = -EPROTO;
	}
	HU("A7 VERSION 3 denemede tutmadi -> DUR");
	return e;
}

#if KK_H_KIP2_DERLI
/*
 * G51 geri okuma: READM <adr> 64 (ROM'a okuma iletisi; 4.9 sdio_read_mem / ram_reg_test_cfg PARA_READM ile aynı tür).
 * Yanıt beklenen baytlarla (gömülü ikilinin dilimi ya da cali için sıfır) memcmp. Protokol hatası -> e (DUR);
 * içerik uyuşmazlığı -> 0 döner, *tuttu = false (6'sı da okunur, karar 6. geri okumadan sonraki adımda = R6; G52 A').
 */
static int kk_h_geri(struct kk_h *h, int ai, const struct kk_h_adim *a, bool *tuttu)
{
	static const u8 sifir[KK_H_READM64];
	const u8 *bek;
	u32 x, boy, i;
	int e;

	*tuttu = false;
	if (a->dosya < KK_H_DOSYA_SAYI) {
		bek = kk_h_fw(a->dosya, &boy);
		if (a->ofs + KK_H_READM64 > boy) {
			snprintf(h->hata, sizeof(h->hata), "geri okuma ofs %u ikili disi", a->ofs);
			return -EINVAL;
		}
		bek += a->ofs;
	} else {
		bek = sifir;			/* cali: 520 B sıfır */
	}
	e = kk_h_mesaj(h, ai, -1, a->metin, a->metin_len, NULL);
	if (!e)
		e = kk_h_yanit(h, ai, &x);
	if (e)
		return e;
	h->geri_n++;
	if (x == KK_H_READM64 && !memcmp(h->rx, bek, KK_H_READM64)) {
		*tuttu = true;
		h->geri_tuttu++;
		HD("A8 #%d GERI %d/6 READM 0x%08x 64 (%s ofs %u): %u B TUTTU (%*phN..)", ai, h->geri_n, a->adr,
		   a->dosya < KK_H_DOSYA_SAYI ? kk_h_dosya_ad[a->dosya] : "cali", a->ofs, x, 8, h->rx);
		return 0;
	}
	for (i = 0; i < min_t(u32, x, KK_H_READM64) && h->rx[i] == bek[i]; i++)
		;
	HU("A8 #%d GERI %d/6 READM 0x%08x 64 (%s ofs %u): %u B TUTMADI (ilk fark bayt %u; gelen %*phN, beklenen %*phN)", ai,
	   h->geri_n, a->adr, a->dosya < KK_H_DOSYA_SAYI ? kk_h_dosya_ad[a->dosya] : "cali", a->ofs, x, i,
	   16, h->rx + (i & ~15u), 16, bek + (i & ~15u));
	return 0;
}

/* A8: tablo adımları (yalnız kip 2). Yeniden gönderme YOK (F8): ilk hatada DUR, kalan adımlar yapılmaz. */
static noinline int kk_h_a8(struct kk_h *h)
{
	int ai, pi, e = 0, wm = 0, fl = 0;

	for (ai = KK_H_AI_A8; ai < KK_H_ADIM_SAYI; ai++) {
		const struct kk_h_adim *a = &kk_h_tablo[ai];
		ktime_t t0 = ktime_get();

		/*
		 * G52 M2c-A' (eller kararı, daraltıcı): geri okuma kapısı 6. geri okumadan sonraki adımda (tablo öz denetimi: R6).
		 * 6/6 tutmadıysa R6..R12 (PMU/BCPU WRITEM) ve QUIT GÖNDERİLMEZ -> DUR.
		 */
		if (ai > KK_H_AI_A8 && a->tur != KK_HT_READM_GERI && kk_h_tablo[ai - 1].tur == KK_HT_READM_GERI) {
			HD("GERI OKUMA %d/6 TUTTU", h->geri_tuttu);
			if (h->geri_tuttu != KK_H_GERI_SAYI || h->geri_n != KK_H_GERI_SAYI) {
				snprintf(h->hata, sizeof(h->hata), "geri okuma %d/6 tuttu; R6-R12 ve QUIT GONDERILMEDI",
					 h->geri_tuttu);
				e = -EIO;
				goto dur;
			}
		}
		switch (a->tur) {
		case KK_HT_SETPM:
			kk_h_durum(h, "A8 SETPM");
			e = kk_h_mesaj(h, ai, -1, a->metin, a->metin_len, "SET OK");
			if (!e)
				HD("A8 #%d SETPM %u -> 'SET OK'", ai, a->deger);
			break;
		case KK_HT_WRITEM: {
			const char *ad = "?";
			int j;

			for (j = 0; j < KK_H_WM_SAYI; j++)
				if (kk_h_wm_izin[j].adr == a->adr && kk_h_wm_izin[j].deger == a->deger)
					ad = kk_h_wm_izin[j].ad;
			kk_h_durum(h, "A8 WRITEM");
			if (wm >= KK_H_GERI_WM_ONCE && (h->geri_tuttu != KK_H_GERI_SAYI || h->geri_n != KK_H_GERI_SAYI)) {
				/* ikinci savunma: R6..R12 yalnız 6/6 geri okumadan sonra (öz denetim + kapı bunu zaten sağlar) */
				snprintf(h->hata, sizeof(h->hata), "%s oncesi geri okuma 6/6 degil; GONDERILMEDI", ad);
				e = -EIO;
				break;
			}
			e = kk_h_mesaj(h, ai, -1, a->metin, a->metin_len, "WRITEM OK");
			if (!e)
				HD("A8 #%d WRITEM 0x%08x <- 0x%x (%s) -> 'WRITEM OK' [%d/12]", ai, a->adr, a->deger, ad, ++wm);
			break;
		}
		case KK_HT_FILES:
			kk_h_durum(h, "A8 FILES");
			for (pi = 0; pi < kk_h_parca_sayi[h->pi] && !e; pi++) {
				const struct kk_h_parca *p = &kk_h_parcalar[h->pi][pi];
				u32 boy;
				const u8 *f = kk_h_fw(p->dosya, &boy);
				ktime_t t1 = ktime_get();

				if (p->dosya != a->dosya)
					continue;
				e = kk_h_mesaj(h, ai, pi, p->metin, p->metin_len, "READY");
				if (e)
					break;
				usleep_range(5000, 5100);	/* FILE_CMD_WAIT_TIME_MIN/MAX */
				e = kk_h_veri(h, ai, pi, f + p->ofs, p->n, p->tel);
				if (e)
					break;
				HD("A8 #%d FILES %s adr 0x%08x ofs %u n %u tel %u crc32 0x%08x -> READY, FILES OK (%lld ms) [%d]",
				   ai, kk_h_dosya_ad[p->dosya], p->adr, p->ofs, p->n, p->tel, kk_h_crc(f + p->ofs, p->n),
				   ktime_ms_delta(ktime_get(), t1), ++fl);
			}
			break;
		case KK_HT_CALI:
			kk_h_durum(h, "A8 CALI");
			e = kk_h_mesaj(h, ai, -1, a->metin, a->metin_len, "READY");
			if (e)
				break;
			usleep_range(5000, 5100);
			e = kk_h_veri(h, ai, -1, NULL, KK_H_CALI_ANLAMLI, KK_H_CALI_TEL);
			if (!e)
				HD("A8 #%d CALI adr 0x%08x 520 B sifir (tel 1024, F4) -> READY, FILES OK [%d]", ai, a->adr, ++fl);
			break;
		case KK_HT_ATLA_V120:
			HD("A8 #%d WRITEM 0x60001044 ATLANDI (V120; 4.9 exec_number_type_cmd): %s", ai, a->kaynak);
			break;
		case KK_HT_READM_GERI: {
			bool tuttu;

			kk_h_durum(h, "A8 GERI OKUMA");
			e = kk_h_geri(h, ai, a, &tuttu);
			break;
		}
		case KK_HT_QUIT:
			/* G51 (eller kararı, daraltıcı): 6/6 geri okuma tutmadıysa QUIT GÖNDERİLMEZ (G52: ikinci savunma; kapı R6'da) */
			if (h->geri_tuttu != KK_H_GERI_SAYI || h->geri_n != KK_H_GERI_SAYI) {
				snprintf(h->hata, sizeof(h->hata), "geri okuma %d/6 tuttu; QUIT GONDERILMEDI", h->geri_tuttu);
				e = -EIO;
				break;
			}
			kk_h_durum(h, "A8 QUIT");
			e = kk_h_mesaj(h, ai, -1, a->metin, a->metin_len, NULL);
			if (!e) {
				h->quit_t = ktime_get();
				HD("A8 #%d QUIT gonderildi (yanit beklenmez)", ai);
			}
			break;
		default:
			snprintf(h->hata, sizeof(h->hata), "beklenmeyen adim turu %u", a->tur);
			e = -EINVAL;
		}
dur:
		if (e) {
			HU("A8 #%d (%s) %s (%lld ms) -> DUR; kalan adimlar YAPILMADI", ai, a->kaynak, h->hata,
			   ktime_ms_delta(ktime_get(), t0));
			return e;
		}
	}
	HD("A8 tablo tamam: WRITEM %d/12, FILES+CALI %d, GERI %d/6, QUIT", wm, fl, h->geri_tuttu);
	return 0;
}

/*
 * A9: hazır iletisi (SDIO yoklaması; F5): F1 0x30'dan 80 B; MFARM + msg bit 0 -> 0x2B ACK. G51: son tarih ktime ile 20 sn
 * (G50: 500 tur, gerçekte 9,7 sn); yoklama aralığı ve döngü gövdesi G50 ile AYNI (okuma, panik/hazır denetimi, msleep(10)).
 * Teşhis kopyaları (ilk okuma, ilk değişim) yalnız bellekte; bütün teşhis satırları ve teşhis CMD52 okumaları döngüden SONRA.
 */
static void kk_h_a9_teshis(struct kk_h *h, const char *sonuc, s64 ms, int n, const u8 *ilk, const u8 *deg, int deg_i,
			   s64 deg_ms)
{
	char hx[2 * KK_H_EXT_BOY + 1];
	int e8 = 0, e3 = 0, e5 = 0;
	u8 v8, v3, v5;

	HD("A9 TESHIS: sonuc %s; A9 basindan %lld ms, QUIT'ten %lld ms; yoklama %d; son tarih %d ms (4.9 20 sn)", sonuc, ms,
	   ktime_ms_delta(ktime_get(), h->quit_t), n, KK_H_HAZIR_MS);
	bin2hex(hx, ilk, KK_H_EXT_BOY);
	hx[2 * KK_H_EXT_BOY] = 0;
	HD("A9 TESHIS ext ilk (yoklama 1): %s", hx);
	if (deg_i) {
		bin2hex(hx, deg, KK_H_EXT_BOY);
		hx[2 * KK_H_EXT_BOY] = 0;
		HD("A9 TESHIS ext ilk degisim (yoklama %d, +%lld ms): %s", deg_i, deg_ms, hx);
	} else {
		HD("A9 TESHIS ext ilk degisim: YOK (%d yoklamada hep ilk okumayla ayni)", n);
	}
	bin2hex(hx, h->ext, KK_H_EXT_BOY);
	hx[2 * KK_H_EXT_BOY] = 0;
	HD("A9 TESHIS ext son (yoklama %d): %s", n, hx);
	/* yalnız CMD52 OKUMA: F1 0x08 (ROM fazında da okunan durum), CCCR 0x03 IORx, CCCR 0x05 INTx (salt okunur) */
	v8 = P_READB(h, 0x08, &e8);
	v3 = P_F0_READB(h, 0x03, &e3);
	v5 = P_F0_READB(h, 0x05, &e5);
	HD("A9 TESHIS CMD52 okuma: F1 0x08 = 0x%02x (e %d), CCCR 0x03 IORx = 0x%02x (e %d), CCCR 0x05 INTx = 0x%02x (e %d)",
	   v8, e8, v3, e3, v5, e5);
}

/*
 * G53 P-D: tek CMD53 bayt kipi okumasının tavanı = 6.12 sdio_max_byte_size() (drivers/mmc/core/sdio_io.c:189-202) ile
 * aynı hesap. sdio_readsb(n <= tavan) TEK CMD53 bayt kipi gönderir (sdio_io.c:353-358; n = 512 -> sayı alanı 0,
 * sdio_ops.c:140-141) = 4.9 oal_mmc_io_rw_scat_extended(blocks 1, blksz n) bayt kipi (OSH:1933-1934). n > tavan ise
 * okuma bölünür (4.9'dan farklı tel) -> okumadan DUR. Yalnız bellekteki yapılar okunur (karta erişim yok).
 */
static u32 kk_h_bayt_tavan(struct kk_h *h)
{
#if IS_ENABLED(CONFIG_KK_HI1102_YUKLE_SAHTE)
	return KK_H_RX;			/* sahte: SDIO çekirdeği yok */
#else
	struct sdio_func *f = h->func;
	u32 m = f->card->host->max_blk_size;

	m = min_t(u32, m, (f->card->quirks & MMC_QUIRK_BLKSZ_FOR_BYTE_MODE) ? f->cur_blksize : f->max_blksize);
	return min_t(u32, m, (f->card->quirks & MMC_QUIRK_BROKEN_BYTE_MODE_512) ? 511u : 512u);
#endif
}

/*
 * G53 P-D: DREADY uzunluk denetimi (4.9 oal_sdio_build_rx_netbuf_list, OSH:830-872): dağıtım listesi comm_reg[i] << 5
 * (HISDIO_D2H_SCATT_BUFFLEN_ALIGN_BITS = 5, OCM:116), ilk sıfıra kadar, en çok 64; HISDIO_ALIGN_4_OR_BLK(toplam) == xfer_count
 * (OIF:43 = KK_H_HIZALA). Ek (Furkan onayı): xfer 1..512 ve tek bayt kipi CMD53. NULL = geçerli.
 */
static const char *kk_h_a9_uzunluk(struct kk_h *h, u32 xfer, u32 *top, int *pn)
{
	u32 t = 0;
	int i;

	for (i = 0; i < 64 && h->ext[16 + i]; i++)
		t += (u32)h->ext[16 + i] << 5;
	*top = t;
	*pn = i;
	if (!xfer)
		return "xfer 0";
	if (xfer > KK_H_RX)
		return "xfer > 512";
	if (xfer & 3u)
		return "xfer hizasiz";
	if (!t)
		return "comm bos";
	if (KK_H_HIZALA(t) != xfer)
		return "comm toplami != xfer";
	if (xfer > kk_h_bayt_tavan(h))
		return "tek CMD53 bayt kipi tavani asildi";
	return NULL;
}

#if IS_ENABLED(CONFIG_KK_HI1102_M3A)
#define KK_H_PK_ILK_HEX	448u	/* ilk paketin yükü TAM (512 - 64); satır başına 64 B */
#define KK_H_PK_HEX	128u	/* sonraki paketlerin ilk 128 B'ı */
static const char * const kk_hcc_ana_ad[] = { "WIFI", "OAM", "TEST", "CHR" };

/* tampon parçasını satır başına 64 B onaltılık olarak kayda (dmesg satırı <= ~200 karakter) */
static void kk_h_hex_dok(struct kk_h *h, const char *evre, int pk, const char *ne, const u8 *p, u32 n)
{
	char hx[2 * 64 + 1];
	u32 o, k;

	for (o = 0; o < n; o += 64) {
		k = min(n - o, 64u);
		bin2hex(hx, p + o, k);
		hx[2 * k] = 0;
		HD("%s HCC %d %s [%u..%u) / %u B: %s", evre, pk, ne, o, o + k, n, hx);
	}
}

/*
 * G54 M3a: bir FIFO okumasının (xfer B) HCC çözümü = 4.9 sdio_transfer_rx_handler -> hcc_transfer_rx_handler -> hcc_rx
 * (OHH:703-724, :1540-1606) ve rx_action[ana]: dağıtım listesi comm[i] << 5 (A9 uzunluk denetiminden geçmiş), her parça ayrı
 * paket. Başlık kk_hcc_rx_gecerli (= hcc_check_rx_netbuf_vaild) ile; geçersiz -> sayılır, atılır (4.9: atılır). Geçerli:
 * ana tür sayılır, paket BIRAKILIR (OAM: 4.9 oam_rx_post_action_function -> SDT; SDT yok. WIFI/TEST/CHR: plat-yalnız 4.9'da
 * işleyici kayıtsız -> atılır). İlk paketin yükü TAM, sonrakilerin ilk 128 B'ı kayda. Erişim yok (yalnız bellek).
 */
static void kk_h_paket(struct kk_h *h, const char *evre)
{
	u32 o = 0, top;
	int i;

	for (i = 0; i < 64 && h->ext[16 + i]; i++, o += top) {
		const u8 *b = h->rx + o;
		struct kk_hcc_alan a;
		u32 ek = 0, cek = 0, kalan = 0;
		int pk;

		top = (u32)h->ext[16 + i] << 5;
		if (o + top > KK_H_RX)		/* A9 uzunluk denetimi bunu dışlar; savunma */
			break;
		pk = ++h->pk_n;
		kk_hcc_coz(b, &a);
		if (top < KK_HCC_TOPLAM || kk_hcc_rx_ac(b, top, &ek, &cek, &kalan)) {
			h->pk_gecersiz++;
			HU("%s HCC %d (parca %d, %u B): GECERSIZ baslik %*phN (ana %u alt %u pad_hdr %u pad_payload %u pay_len %u); 4.9 gibi ATILDI",
			   evre, pk, i + 1, top, 8, b, a.ana, a.alt, a.pad_hdr, a.pad_payload, a.pay_len);
			kk_h_hex_dok(h, evre, pk, "ham", b, min(top, KK_H_PK_HEX));
			continue;
		}
		if (a.ana == KK_HCC_OAM)
			h->pk_oam++;
		else
			h->pk_diger++;
		HD("%s HCC %d (parca %d, %u B): ana %u %s alt %u sira %u pad_hdr %u pad_payload %u more %u option %u pay_len %u ek %u; %s %d. paket sayildi, BIRAKILDI",
		   evre, pk, i + 1, top, a.ana, kk_hcc_ana_ad[a.ana], a.alt, a.sira, a.pad_hdr, a.pad_payload, a.more, a.option,
		   a.pay_len, ek, a.ana == KK_HCC_OAM ? "OAM" : "diger", a.ana == KK_HCC_OAM ? h->pk_oam : h->pk_diger);
		if (pk == 1) {			/* beyin: ilk paketin TAM yükü */
			if (ek)
				kk_h_hex_dok(h, evre, pk, "ek", b + KK_HCC_BASLIK + a.pad_hdr, ek);
			kk_h_hex_dok(h, evre, pk, "yuk", b + cek, min_t(u32, a.pay_len, KK_H_PK_ILK_HEX));
		} else {			/* sonrakilerin ilk 128 B'ı (ham, başlık dahil) */
			kk_h_hex_dok(h, evre, pk, "ham", b, min(top, KK_H_PK_HEX));
		}
	}
}

/* D2H ileti bitleri (4.9 oal_sdio_comm.h:37-50) -> kısa adlar */
static void kk_h_msg_ad(u32 msg, char *s, size_t n)
{
	static const char * const ad[14] = { "READY", "FC_UPDATE", "FC_OFF", "FC_ON", "WAKEUP_SUCC", "ALLOW_SLEEP",
					     "DISALLOW_SLEEP", "PANIC", "POWEROFF_ACK", "OPEN_BCPU_ACK", "CLOSE_BCPU_ACK",
					     "CREDIT", "HIGH_PKT_LOSS", "HALT_BCPU" };
	size_t k = 0;
	int b;

	s[0] = 0;
	for (b = 0; b < 32 && k < n; b++)
		if (msg & BIT(b))
			k += scnprintf(s + k, n - k, "%s%s", k ? "|" : "", b < 14 ? ad[b] : "?");
	if (!k)
		scnprintf(s, n, "-");
}
#endif

/*
 * G53 P-D: A9 = 4.9 oal_sdio_do_isr (OSH:972-1078; sdio_extend_func = 1, hisdio_intr_mode = 1) eşdeğeri. Her yoklamada:
 *  1. ext <- CMD53 OKUMA F1 0x30, 80 B (oal_sdio_extend_buf_get, OSH:695-705; G52 ile aynı).
 *  2. int = int_stat & 0x07 (OSH:919-924). 0 ise bekle (func1_no_int_count; extend kipinde F1 0x08'e temizleme yazması yok,
 *     OSH:954-957; kesme ext okumasıyla temizlenir [G52 gözlemi]).
 *  3. DUR denetimleri, bu turun hiçbir okumasından ÖNCE (eller kararı, tutucu): RERROR (4.9 log + devam, OSH:1023-1028);
 *     MFARM + msg b7 PANIK (4.9 SDIO'yu kapatır, OSH:610-614); kesme sayısı > 16; ACK gerekecekken ACK sayısı 8;
 *     DREADY iken uzunluk tutarsız (kk_h_a9_uzunluk) ya da paket sayısı 8.
 *  4. MFARM + msg != 0 -> CMD52 OKUMA F1 0x2B (D2H ACK; oal_sdio_msg_stat, OSH:552-573; ileti başına 1, bit başına değil).
 *  5. DREADY -> CMD53 OKUMA F1 0x00 (FIFO, sabit adres), xfer bayt -> rx (512 + 4 B); ilk 64 B kayda, veri atılır
 *     (oal_sdio_data_sg_irq -> sdio_transfer_rx_handler, OSH:1068-1072, OHH:703-724).
 *  6. MFARM + msg b0 -> HAZIR (4.9: b0 geri çağrısı wlan_pm_set_device_ready; DREADY aynı turdaysa önce o okunur).
 * Son tarih 20 sn (ktime) ve yoklama aralığı (msleep(10)) G52 ile aynı. Çipe YAZMA YOK (kk_h_yaz çağrılmaz).
 * Teşhis (G51) aynen; ek olarak her kesme turu (ILETI), her ACK, her paket (VERI, ilk 64 B hex) ve A9 OZET satırı.
 */
static noinline int kk_h_a9(struct kk_h *h)
{
	ktime_t t0 = ktime_get(), son = ktime_add_ms(t0, KK_H_HAZIR_MS);
	u8 ilk[KK_H_EXT_BOY], deg[KK_H_EXT_BOY];
	char hx[2 * KK_H_A9_HEX + 1];
	int i, e = 0, r, deg_i = 0;
	s64 deg_ms = 0;
	const char *sn;

	kk_h_durum(h, "A9 HAZIR");
	memset(ilk, 0, sizeof(ilk));
	HD("A9 P-D kesme servisi (4.9 oal_sdio_do_isr): ACK <= %d (F1 0x2B CMD52 okuma), VERI <= %d x <= %u B (F1 FIFO CMD53 okuma; tek bayt kipi tavani %u B), kesme <= %d; QUIT'ten %lld ms; yazma YOK",
	   KK_H_A9_ACK_UST, KK_H_A9_VERI_UST, KK_H_RX, kk_h_bayt_tavan(h), KK_H_A9_KESME_UST,
	   ktime_ms_delta(t0, h->quit_t));
	for (i = 0; ; i++) {
		u32 ham, st, msg, xfer, top = 0;
		const char *neden = NULL;
		bool ack, veri;
		int pn = 0;

		e = P_FROMIO(h, h->ext, KK_H_EXT_ADR, KK_H_EXT_BOY);
		if (e) {
			snprintf(h->hata, sizeof(h->hata), "F1 0x30 okuma hatasi %d", e);
			r = e;
			sn = "OKUMA HATASI";
			break;
		}
		if (!i) {
			memcpy(ilk, h->ext, KK_H_EXT_BOY);
		} else if (!deg_i && memcmp(ilk, h->ext, KK_H_EXT_BOY)) {
			memcpy(deg, h->ext, KK_H_EXT_BOY);
			deg_i = i + 1;
			deg_ms = ktime_ms_delta(ktime_get(), t0);
		}
		ham = get_unaligned_le32(h->ext);
		msg = get_unaligned_le32(h->ext + 4);
		xfer = get_unaligned_le32(h->ext + 8);
		st = ham & KK_H_INT_MASKE;
		if (st) {
			h->a9_kesme++;
			ack = (st & KK_H_INT_MFARM) && msg;
			veri = st & KK_H_INT_DREADY;
			if (veri)
				neden = kk_h_a9_uzunluk(h, xfer, &top, &pn);
			HD("A9 ILETI %d (yoklama %d, +%lld ms): int 0x%x msg 0x%x xfer %u credit 0x%08x comm %*phN",
			   h->a9_kesme, i + 1, ktime_ms_delta(ktime_get(), t0), ham, msg, xfer,
			   get_unaligned_le32(h->ext + 12), 8, h->ext + 16);
			r = -EIO;
			if (st & KK_H_INT_RERROR) {
				snprintf(h->hata, sizeof(h->hata), "RERROR (int 0x%x msg 0x%x); OKUNMADI", ham, msg);
				sn = "RERROR";
				break;
			}
			if ((st & KK_H_INT_MFARM) && (msg & KK_H_MSG_PANIK)) {
				snprintf(h->hata, sizeof(h->hata), "D2H DEVICE_PANIC (int 0x%x msg 0x%x)", ham, msg);
				sn = "PANIK";
				break;
			}
			if (h->a9_kesme > KK_H_A9_KESME_UST) {
				snprintf(h->hata, sizeof(h->hata), "kesme siniri: %d. kesme (en cok %d); OKUNMADI", h->a9_kesme,
					 KK_H_A9_KESME_UST);
				sn = "SINIR";
				break;
			}
			if (ack && h->a9_ack >= KK_H_A9_ACK_UST) {
				snprintf(h->hata, sizeof(h->hata), "ACK siniri: %d. ileti (en cok %d); OKUNMADI", h->a9_ack + 1,
					 KK_H_A9_ACK_UST);
				sn = "SINIR";
				break;
			}
			if (veri && neden) {
				snprintf(h->hata, sizeof(h->hata), "DREADY uzunluk tutarsiz: %s (xfer %u, comm %u B/%d); OKUNMADI",
					 neden, xfer, top, pn);
				sn = "UZUNLUK";
				break;
			}
			if (veri && h->a9_veri >= KK_H_A9_VERI_UST) {
				snprintf(h->hata, sizeof(h->hata), "VERI siniri: %d. paket (en cok %d); OKUNMADI", h->a9_veri + 1,
					 KK_H_A9_VERI_UST);
				sn = "SINIR";
				break;
			}
			if (ack) {
				u8 v = P_READB(h, 0x2B, &e);	/* D2H ACK (CONFIG_SDIO_D2H_MSG_ACK; OSH:563-573) */

				h->a9_ack++;
				HD("A9 ACK %d/%d: F1 0x2B CMD52 okuma = 0x%02x (e %d; msg 0x%x)", h->a9_ack, KK_H_A9_ACK_UST, v, e,
				   msg);
				if (e) {
					snprintf(h->hata, sizeof(h->hata), "F1 0x2B okuma hatasi %d", e);
					r = e;
					sn = "ACK HATASI";
					break;
				}
			}
			if (veri) {
				e = P_READSB(h, h->rx, 0x00, xfer);
				h->a9_veri++;
				h->a9_veri_bayt += xfer;
				bin2hex(hx, h->rx, min(xfer, KK_H_A9_HEX));
				hx[2 * min(xfer, KK_H_A9_HEX)] = 0;
				HD("A9 VERI %d/%d: F1 FIFO CMD53 okuma %u B (comm %d paket, %u B; e %d); ilk %u B: %s", h->a9_veri,
				   KK_H_A9_VERI_UST, xfer, pn, top, e, min(xfer, KK_H_A9_HEX), hx);
				if (e) {
					snprintf(h->hata, sizeof(h->hata), "FIFO okuma hatasi %d (xfer %u)", e, xfer);
					r = e;
					sn = "VERI HATASI";
					break;
				}
#if IS_ENABLED(CONFIG_KK_HI1102_M3A)
				kk_h_paket(h, "A9");	/* G54 M3a: HCC çözümü + OAM sayımı; erişim yok */
#endif
			}
			if ((st & KK_H_INT_MFARM) && (msg & KK_H_MSG_READY)) {
				HD("HAZIR: D2H WLAN_READY (%lld ms; yoklama %d; int 0x%x msg 0x%x; 0x2B ack okundu; QUIT'ten %lld ms; ACK %d, VERI %d)",
				   ktime_ms_delta(ktime_get(), t0), i + 1, ham, msg, ktime_ms_delta(ktime_get(), h->quit_t),
				   h->a9_ack, h->a9_veri);
				r = 0;
				sn = "HAZIR";
				break;
			}
		}
		if (ktime_after(ktime_get(), son)) {
			snprintf(h->hata, sizeof(h->hata), "hazir yok (%lld ms, %d yoklama)", ktime_ms_delta(ktime_get(), t0), i + 1);
			r = -ETIMEDOUT;
			sn = "HAZIR YOK";
			break;
		}
		msleep(10);
	}
	HD("A9 OZET: sonuc %s; kesme %d, ACK %d/%d (F1 0x2B CMD52 okuma), VERI %d/%d (F1 FIFO CMD53 okuma, %u B); A9 yazma 0",
	   sn, h->a9_kesme, h->a9_ack, KK_H_A9_ACK_UST, h->a9_veri, KK_H_A9_VERI_UST, h->a9_veri_bayt);
	kk_h_a9_teshis(h, sn, ktime_ms_delta(ktime_get(), t0), i + 1, ilk, deg, deg_i, deg_ms);
	return r;
}

#if IS_ENABLED(CONFIG_KK_HI1102_M3A)
/*
 * G54 M3a A10: hcc_enable eşdeğeri + HCC alım evresi. 4.9 wlan_power_on (plat_pm.c:1179) READY'den sonra
 * hcc_enable(hcc, TRUE) çağırır; hcc_enable (oal_hcc_host.c:1256-1281) YALNIZ host durumudur (kuyrukları boşaltır, tx akış
 * denetimini açar, durum HCC_ON) — tele erişmez. Burada tx yolu derlenmediği için kuyruk yok; durum bayrağı kayda geçer.
 * PM (wlan_pm_enable, g_wlan_pm_switch) yok: uyku zamanlayıcısı, 0xF0 <- 1, H2D SLEEP_REQ yok (Furkan onayı: pm_switch 0).
 * Ardından KK_H_A10_MS boyunca A9 ile AYNI kesme servisi (her MFARM + msg != 0 -> F1 0x2B ACK okuması; DREADY -> FIFO okuması
 * + HCC çözümü). DUR denetimleri her turun okumalarından ÖNCE (A9 ile aynı): RERROR, PANIC, kesme > 16, ACK 8, VERI 8,
 * uzunluk tutarsız; okuma hatası -> DUR. READY biti yeniden gelirse yalnız kayda (bilgi). Aralık 10 ms; 1 sn iletisizse
 * 100 ms. Sayaçlar A9'dan AYRI (A9'un 16/8/8 sınırları A10 için de ayrıca geçerli). Çipe YAZMA YOK.
 */
static noinline int kk_h_a10(struct kk_h *h)
{
	ktime_t t0 = ktime_get(), son = ktime_add_ms(t0, KK_H_A10_MS), sonileti = t0;
	const char *sn = "TAMAM";
	int i, e = 0, r = 0;
	char ad[96];

	kk_h_durum(h, "A10 HCC");
	mutex_lock(&h->kilit);
	h->hcc_on = 1;
	mutex_unlock(&h->kilit);
	h->credit_son = get_unaligned_le32(h->ext + 12);
	HD("A10 hcc_enable esdegeri: HCC_ON (yalniz host durumu; tx yolu DERLENMEDI, tele erisim yok; 4.9 oal_hcc_host.c:1256); PM YOK (pm_switch 0: uyku, 0xF0<-1, H2D ileti yok); credit 0x%08x",
	   h->credit_son);
	HD("A10 HCC alim evresi: %d ms, yoklama 10 ms (1 sn iletisiz -> 100 ms); ACK <= %d, VERI <= %d x <= %u B, kesme <= %d; yazma YOK",
	   KK_H_A10_MS, KK_H_A9_ACK_UST, KK_H_A9_VERI_UST, KK_H_RX, KK_H_A9_KESME_UST);
	for (i = 0; ; i++) {
		u32 ham, st, msg, xfer, kr, top = 0;
		const char *neden = NULL;
		bool ack, veri;
		int pn = 0;

		if (READ_ONCE(h->durdur)) {
			snprintf(h->hata, sizeof(h->hata), "surucu ayrildi (remove)");
			r = -ENODEV;
			sn = "AYRILDI";
			break;
		}
		e = P_FROMIO(h, h->ext, KK_H_EXT_ADR, KK_H_EXT_BOY);
		if (e) {
			snprintf(h->hata, sizeof(h->hata), "A10 F1 0x30 okuma hatasi %d", e);
			r = e;
			sn = "OKUMA HATASI";
			break;
		}
		ham = get_unaligned_le32(h->ext);
		msg = get_unaligned_le32(h->ext + 4);
		xfer = get_unaligned_le32(h->ext + 8);
		kr = get_unaligned_le32(h->ext + 12);
		st = ham & KK_H_INT_MASKE;
		if (kr != h->credit_son) {
			HD("A10 credit 0x%08x -> 0x%08x (yoklama %d, +%lld ms)", h->credit_son, kr, i + 1,
			   ktime_ms_delta(ktime_get(), t0));
			h->credit_son = kr;
		}
		if (st) {
			sonileti = ktime_get();
			h->a10_kesme++;
			ack = (st & KK_H_INT_MFARM) && msg;
			veri = st & KK_H_INT_DREADY;
			if (veri)
				neden = kk_h_a9_uzunluk(h, xfer, &top, &pn);
			kk_h_msg_ad(st & KK_H_INT_MFARM ? msg : 0, ad, sizeof(ad));
			HD("A10 ILETI %d (yoklama %d, +%lld ms): int 0x%x msg 0x%x (%s) xfer %u credit 0x%08x comm %*phN",
			   h->a10_kesme, i + 1, ktime_ms_delta(sonileti, t0), ham, msg, ad, xfer, kr, 8, h->ext + 16);
			r = -EIO;
			if (st & KK_H_INT_RERROR) {
				snprintf(h->hata, sizeof(h->hata), "A10 RERROR (int 0x%x msg 0x%x); OKUNMADI", ham, msg);
				sn = "RERROR";
				break;
			}
			if ((st & KK_H_INT_MFARM) && (msg & KK_H_MSG_PANIK)) {
				snprintf(h->hata, sizeof(h->hata), "A10 D2H DEVICE_PANIC (int 0x%x msg 0x%x)", ham, msg);
				sn = "PANIK";
				break;
			}
			if (h->a10_kesme > KK_H_A9_KESME_UST) {
				snprintf(h->hata, sizeof(h->hata), "A10 kesme siniri: %d. kesme (en cok %d); OKUNMADI",
					 h->a10_kesme, KK_H_A9_KESME_UST);
				sn = "SINIR";
				break;
			}
			if (ack && h->a10_ack >= KK_H_A9_ACK_UST) {
				snprintf(h->hata, sizeof(h->hata), "A10 ACK siniri: %d. ileti (en cok %d); OKUNMADI",
					 h->a10_ack + 1, KK_H_A9_ACK_UST);
				sn = "SINIR";
				break;
			}
			if (veri && neden) {
				snprintf(h->hata, sizeof(h->hata), "A10 DREADY uzunluk tutarsiz: %s (xfer %u, comm %u B/%d); OKUNMADI",
					 neden, xfer, top, pn);
				sn = "UZUNLUK";
				break;
			}
			if (veri && h->a10_veri >= KK_H_A9_VERI_UST) {
				snprintf(h->hata, sizeof(h->hata), "A10 VERI siniri: %d. paket (en cok %d); OKUNMADI",
					 h->a10_veri + 1, KK_H_A9_VERI_UST);
				sn = "SINIR";
				break;
			}
			r = 0;
			if (ack) {
				u8 v = P_READB(h, 0x2B, &e);	/* D2H ACK (A9 ile aynı okuma) */

				h->a10_ack++;
				HD("A10 ACK %d/%d: F1 0x2B CMD52 okuma = 0x%02x (e %d; msg 0x%x)", h->a10_ack, KK_H_A9_ACK_UST,
				   v, e, msg);
				if (e) {
					snprintf(h->hata, sizeof(h->hata), "A10 F1 0x2B okuma hatasi %d", e);
					r = e;
					sn = "ACK HATASI";
					break;
				}
			}
			if (veri) {
				e = P_READSB(h, h->rx, 0x00, xfer);
				h->a10_veri++;
				h->a10_veri_bayt += xfer;
				HD("A10 VERI %d/%d: F1 FIFO CMD53 okuma %u B (comm %d paket, %u B; e %d)", h->a10_veri,
				   KK_H_A9_VERI_UST, xfer, pn, top, e);
				if (e) {
					snprintf(h->hata, sizeof(h->hata), "A10 FIFO okuma hatasi %d (xfer %u)", e, xfer);
					r = e;
					sn = "VERI HATASI";
					break;
				}
				kk_h_paket(h, "A10");
			}
			if ((st & KK_H_INT_MFARM) && (msg & KK_H_MSG_READY))
				HD("A10 READY biti yeniden (bilgi; durum degismez)");
		}
		if (ktime_after(ktime_get(), son))
			break;
		msleep(ktime_ms_delta(ktime_get(), sonileti) > 1000 ? 100 : 10);
	}
	h->a10_yoklama = i + 1;
	h->a10_ms = ktime_ms_delta(ktime_get(), t0);
	HD("A10 OZET: sonuc %s; %u ms, yoklama %d; kesme %d, ACK %d/%d, VERI %d/%d (%u B); HCC paket %d (OAM %d, diger %d, gecersiz %d); credit 0x%08x; A10 yazma 0",
	   sn, h->a10_ms, h->a10_yoklama, h->a10_kesme, h->a10_ack, KK_H_A9_ACK_UST, h->a10_veri, KK_H_A9_VERI_UST,
	   h->a10_veri_bayt, h->pk_n, h->pk_oam, h->pk_diger, h->pk_gecersiz, h->credit_son);
	return r;
}
#endif
#endif

/* A0: ön koşullar (SDIO erişimi YOK) */
static int kk_h_a0(struct kk_h *h)
{
	struct kk_h_kart k = {};
	const char *neden = NULL;
	int i, r = -EAGAIN;

	kk_h_durum(h, "A0 on kosul");
#if IS_ENABLED(CONFIG_KK_HI1102_YUKLE_SAHTE)
	if (!kk_sdio_kapi_hazir(h->dev)) {
		snprintf(h->hata, sizeof(h->hata), "kk-sdio-kapi HAZIR degil (baslat tetigi/dwmmc2 yok)");
		return -EPERM;
	}
	for (i = 0; i < KK_H_KART_TUR && (r = kk_hs_kart(&k)) == -EAGAIN; i++)
		msleep(100);
#else
	for (i = 0; i < KK_H_KART_TUR && (r = kk_sdio_kapi_kart(h->dev, &k)) == -EAGAIN; i++)
		msleep(100);
#endif
	if (r) {
		snprintf(h->hata, sizeof(h->hata), r == -EPERM ? "kk-sdio-kapi HAZIR degil (baslat tetigi/dwmmc2 yok)" :
			 r == -EAGAIN ? "kart 20 sn icinde gorulmedi" : "kart gorulmedi ya da beklenmeyen");
		return r == -EAGAIN ? -ENODEV : r;
	}
	h->func = k.func;
	HD("A0 kart: tip %u CIS 0x%04x/0x%04x; fonksiyon %u (fn%u 0x%04x/0x%04x, surucu %s); ios %u Hz %u bit; filtre reddi %u",
	   k.tip, k.vendor, k.device, k.fonksiyon, k.fn_no, k.fn_vendor, k.fn_device, k.surucu ? "BAGLI" : "yok", k.saat,
	   k.genislik_bit, k.filtre_reddi);
	if (k.tip != MMC_TYPE_SDIO || k.vendor != 0x0296 || k.device != 0x5347 || k.fonksiyon != 1 || k.fn_no != 1 ||
	    k.fn_vendor != 0x0296 || k.fn_device != 0x5347)
		neden = "kart kimligi beklenen degil (SDIO, 0x0296/0x5347, 1 fonksiyon)";
#if IS_ENABLED(CONFIG_KK_HI1102_M3A)
	else if (!k.surucu)		/* G54 M3a: fn1'e kk-hi1102 (kk-wifi) bağlı OLMALI */
		neden = "M3a: fn1'e surucu bagli degil";
	else if (k.func != h->m3a_fn)	/* probe'un fn1'i = kapının fn1'i (sahte: ikisi de NULL) */
		neden = "M3a: probe fn1'i kapinin fn1'i degil";
#else
	else if (k.surucu)
		neden = "fn1'e surucu bagli";
#endif
	else if ((k.saat != 400000 && k.saat != 3200000) || k.genislik_bit != 4)	/* G62: 3,2 MHz (ciu, bolucu 0) da kabul */
		neden = "ios 400000/3200000 Hz 4 bit degil";
	else if (k.filtre_reddi)
		neden = "G44 filtre reddi 0 degil";
	if (neden) {
		snprintf(h->hata, sizeof(h->hata), "%s", neden);
		return -ENODEV;
	}
#if IS_ENABLED(CONFIG_KK_HI1102_YALNIZ_KIP1)
	if (kk_h_tablo_kip1_denetle(kk_h_tablo, KK_H_ADIM_SAYI, KK_H_SURUM_ONEK, &neden)) {
		snprintf(h->hata, sizeof(h->hata), "tablo oz denetimi: %s", neden);
		return -EBADMSG;
	}
	HD("A0 kip 1 derlemesi: firmware YOK (gomulmedi), indirme kodu DERLENMEDI; tablo oz denetimi TAMAM (%d adim: '%s', '%s')",
	   KK_H_ADIM_SAYI, kk_h_tablo[KK_H_AI_EC].metin, kk_h_tablo[KK_H_AI_SURUM].metin);
	/* tamponlar: hepsi kmalloc (DMA'ya uygun); ext (A9) ayrılmaz */
	h->tx = kmalloc(KK_H_TX_KIP1, GFP_KERNEL);
	h->rx = kmalloc(KK_H_RX + 4, GFP_KERNEL);
	if (!h->tx || !h->rx) {
		snprintf(h->hata, sizeof(h->hata), "tx/rx tamponu ayrilamadi");
		return -ENOMEM;
	}
	h->tx_kap = KK_H_TX_KIP1;
	h->fifo_ust = 1 + 3;
	h->temizle_ust = 1 + 1 + 3;
	HD("A0 tamponlar: tx %u B fiz 0x%llx, rx fiz 0x%llx; kapi ust: FIFO %d, temizle %d",
	   h->tx_kap, (unsigned long long)virt_to_phys(h->tx), (unsigned long long)virt_to_phys(h->rx), h->fifo_ust,
	   h->temizle_ust);
	return 0;
#else
	for (i = 0; i < KK_H_DOSYA_SAYI; i++) {
		u32 boy, c;
		const u8 *f = kk_h_fw(i, &boy);

		c = kk_h_crc(f, boy);
		if (kk_hs_crc_boz(i))
			c ^= 0x1;
		if (boy != kk_h_dosya_boy[i] || c != kk_h_fw_crc32[i]) {
			snprintf(h->hata, sizeof(h->hata), "gomulu %s boy %u crc32 0x%08x (beklenen %u 0x%08x)",
				 kk_h_dosya_ad[i], boy, c, kk_h_dosya_boy[i], kk_h_fw_crc32[i]);
			return -EBADMSG;
		}
	}
	for (i = 0; i < KK_H_DOSYA_SAYI; i++) {	/* G50 M2c: çalışma anı sha256 (CRC32'ye ek) */
		u32 boy;
		const u8 *f = kk_h_fw(i, &boy);
		char hex[2 * SHA256_DIGEST_SIZE + 1];

		if (!kk_h_sha_tuttu(i, f, boy, hex)) {
			snprintf(h->hata, sizeof(h->hata), "gomulu %s sha256 %.16s (beklenen %.16s)", kk_h_dosya_ad[i], hex,
				 kk_h_fw_sha256[i]);
			return -EBADMSG;
		}
	}
	if (kk_h_tablo_denetle(kk_h_tablo, KK_H_ADIM_SAYI, kk_h_parcalar, kk_h_parca_sayi, kk_h_parca_boy, kk_h_dosya_boy,
			       KK_H_SURUM_ONEK, &neden)) {
		snprintf(h->hata, sizeof(h->hata), "tablo oz denetimi: %s", neden);
		return -EBADMSG;
	}
	HD("A0 firmware %s: %s %u crc 0x%08x, %s %u crc 0x%08x, %s %u crc 0x%08x TUTTU; sha256 3/3 TUTTU (%.8s/%.8s/%.8s); tablo oz denetimi TAMAM (%d adim, WRITEM 12 beyaz listede, READM efuse 1, geri okuma 6, parca %u/%u/%u)",
	   KK_H_FW_GERCEK ? "GERCEK" : "YER TUTUCU", kk_h_dosya_ad[0], kk_h_dosya_boy[0], kk_h_fw_crc32[0],
	   kk_h_dosya_ad[1], kk_h_dosya_boy[1], kk_h_fw_crc32[1], kk_h_dosya_ad[2], kk_h_dosya_boy[2], kk_h_fw_crc32[2],
	   kk_h_fw_sha256[0], kk_h_fw_sha256[1], kk_h_fw_sha256[2],
	   KK_H_ADIM_SAYI, kk_h_parca_sayi[0], kk_h_parca_sayi[1], kk_h_parca_sayi[2]);
	/*
	 * G50 M2c: yalnız onaylı parça (261 632 B). kk_hi1102_yukle.parca başka bir seçeneği seçtiyse ya da 261 632 B'lık
	 * tampon ayrılamazsa DUR, 0 SDIO erişimi (G48'deki 32 768/4 096'ya yarılama YOK: sayımlar onay cümlesiyle birebir).
	 */
	if (kk_h_parca_boy[h->pi] != KK_H_PARCA_ONAYLI) {
		snprintf(h->hata, sizeof(h->hata), "parca %u onayli degil (yalniz %u)", kk_h_parca_boy[h->pi], KK_H_PARCA_ONAYLI);
		return -EPERM;
	}
	/* tamponlar: hepsi kmalloc (DMA'ya uygun; .rodata/yığın doğrudan gönderilmez, §1.7-4) */
	if (!kk_hs_kmalloc_boz(KK_H_PARCA_ONAYLI))
		h->tx = kmalloc(KK_H_PARCA_ONAYLI, GFP_KERNEL | __GFP_NOWARN);
	if (!h->tx) {
		snprintf(h->hata, sizeof(h->hata), "tx %u B ayrilamadi (yarilama yok)", KK_H_PARCA_ONAYLI);
		return -ENOMEM;
	}
	h->tx_kap = KK_H_PARCA_ONAYLI;
	h->rx = kmalloc(KK_H_RX + 4, GFP_KERNEL);
	h->ext = kmalloc(KK_H_EXT_BOY, GFP_KERNEL);
	if (!h->rx || !h->ext) {
		snprintf(h->hata, sizeof(h->hata), "rx/ext tamponu ayrilamadi");
		return -ENOMEM;
	}
	/* kapı üst sınırları: tablodan (kip'e göre) + VERSION'ın 3 denemesi. G51: kip 2 adımları = 1 (efuse) ve 3.. */
	h->fifo_ust = 1 + 3;
	h->temizle_ust = 1 + 1 + 3;
	if (h->kip == 2) {
		for (i = 0; i < KK_H_ADIM_SAYI; i++) {
			const struct kk_h_adim *a = &kk_h_tablo[i];
			int j;

			if (a->kip != 2)
				continue;	/* READM EC ve VERSION yukarıda sayıldı */
			if (a->tur == KK_HT_FILES) {
				for (j = 0; j < kk_h_parca_sayi[h->pi]; j++)
					if (kk_h_parcalar[h->pi][j].dosya == a->dosya) {
						h->fifo_ust += 2;
						h->temizle_ust += 2;
					}
			} else if (a->tur == KK_HT_CALI) {
				h->fifo_ust += 2;
				h->temizle_ust += 2;
			} else if (a->metin) {
				h->fifo_ust++;
				h->temizle_ust += a->yanit != KK_HB_YOK;
			}
		}
	}
	HD("A0 tamponlar: tx %u B fiz 0x%llx, rx fiz 0x%llx, ext fiz 0x%llx; parca %u; kapi ust: FIFO %d, temizle %d",
	   h->tx_kap, (unsigned long long)virt_to_phys(h->tx), (unsigned long long)virt_to_phys(h->rx),
	   (unsigned long long)virt_to_phys(h->ext), kk_h_parca_boy[h->pi], h->fifo_ust, h->temizle_ust);
	return 0;
#endif
}

static void kk_h_is(struct work_struct *w)
{
	struct kk_h *h = container_of(w, struct kk_h, is);
	ktime_t t0 = ktime_get();
	bool talep = false;
	int e;

	h->calisiyor = true;
#if IS_ENABLED(CONFIG_KK_HI1102_YALNIZ_KIP1)
	if (h->kip != 1) {	/* store zaten reddeder; savunma */
		snprintf(h->hata, sizeof(h->hata), "kip %d bu derlemede yok", h->kip);
		HU("kip %d -> DUR, 0 SDIO erisimi", h->kip);
		goto son;
	}
#endif
	e = kk_h_a0(h);
	if (e) {
		HU("A0 %s -> DUR, 0 SDIO erisimi", h->hata);
		goto son;
	}
	P_CLAIM(h);		/* A1: dizi boyunca host talep edilir */
	talep = true;
	kk_h_durum(h, "A2-A5 kurulum");
	e = kk_h_yaz(h, KK_HY_ETKIN, 0, -1, -1, 0);		/* A2 */
	if (!e)
		HD("A2 fn1 etkin (CCCR 0x02 |= 0x02; IORx yoklandi)");
	if (!e) {
		e = kk_h_yaz(h, KK_HY_BLOK, KK_H_BLK, -1, -1, 0);	/* A3 */
		if (!e)
			HD("A3 blok 512 (FBR1 0x110 <- 0x00, 0x111 <- 0x02)");
	}
	if (!e)
		e = kk_h_yaz(h, KK_HY_TEMIZLE, 0x07, -1, -1, 0);	/* A4 */
	if (!e) {
		e = kk_h_yaz(h, KK_HY_INTAC, 0x07, -1, -1, 0);
		if (!e)
			HD("A4 int 0x07 (F1 0x08 <- 0x07, F1 0x09 <- 0x07)");
	}
	if (!e) {
		e = kk_h_yaz(h, KK_HY_UYANDIR, 0x00, -1, -1, 0);	/* A5 */
		if (!e)
			HD("A5 0xF0<-0x00 (DISALLOW_TO_SLEEP; 4.9 wlan_pm_init_dev)");
	}
	if (e) {
		HU("A2-A5 %s -> DUR", h->hata);
		goto son;
	}
	e = kk_h_a6(h);
#if KK_H_KIP2_DERLI
	if (!e && h->kip == 2)
		e = kk_h_a6b(h);	/* G51: 4.9 sırası EC -> efuse 64 -> VERSION */
#endif
	if (!e)
		e = kk_h_a7(h);
	if (e)
		goto son;
	if (h->kip == 1) {
		h->sonuc = "EL SIKISMA TAMAM";
		HD("SONUC: EL SIKISMA TAMAM; CMD52 yazma %d (6 kurulum + %d yanit temizleme), CMD53 yazma %d; ROM WRITEM 0 FILES 0; surum '%s'",
		   h->c52, h->c52 - 6, h->c53, h->surum);
		goto son;
	}
#if KK_H_KIP2_DERLI
	if (strcmp(h->surum, KK_H_SURUM_TAM)) {	/* G50 M2c: önek yetmez; ROM'a ilk O3 yazmasından önce tam metin */
		snprintf(h->hata, sizeof(h->hata), "surum '%.40s' 4.9/G49 metni degil", h->surum);
		HU("A7 kip 2: %s -> DUR (SETPM/WRITEM/FILES GONDERILMEDI)", h->hata);
		goto son;
	}
	HD("A7 kip 2: surum 4.9/G49 ile TAM ayni ('%s'); A8'e geciliyor", KK_H_SURUM_TAM);
	e = kk_h_a8(h);
	if (!e) {
		HD("A8 indirme %lld ms; CMD53 tel %u B", ktime_ms_delta(ktime_get(), t0), h->tel);
		e = kk_h_a9(h);
	}
	if (!e) {
		h->sonuc = "HAZIR";
		HD("SONUC: HAZIR; CMD52 yazma %d, CMD53 yazma %d, tel %u B; VERSION deneme %d; kapi reddi %d; geri okuma %d/6 TUTTU",
		   h->c52, h->c53, h->tel, h->surum_deneme, h->red, h->geri_tuttu);
	} else {
		HU("A9/A8 %s -> DUR", h->hata);
	}
#if IS_ENABLED(CONFIG_KK_HI1102_M3A)
	if (!e) {
		e = kk_h_a10(h);
		if (!e) {
			h->sonuc = "M3A TAMAM";
			HD("SONUC M3a: TAMAM; HAZIR + HCC_ON + A10 %u ms; HCC paket %d (OAM %d, diger %d, gecersiz %d); A9+A10 ACK %d, VERI %d; CMD52 yazma %d, CMD53 yazma %d (A9/A10 yazma 0); kapi reddi %d",
			   h->a10_ms, h->pk_n, h->pk_oam, h->pk_diger, h->pk_gecersiz, h->a9_ack + h->a10_ack,
			   h->a9_veri + h->a10_veri, h->c52, h->c53, h->red);
#if IS_ENABLED(CONFIG_KK_HI1102_M3B)
			/* G55 M3b: port host katmanlari fn1'i devralir (yalniz is parcacigi baslatilir; host burada birakilir) */
			HD("M3b: port host katmanlarina devir (kk_m3b_baslat) -> %d", kk_m3b_baslat(h->func));
#endif
		} else {
			h->sonuc = "DUR";
			HU("A10 %s -> DUR (surucu durur; baska erisim yok)", h->hata);
		}
	}
#endif
#endif
son:
	if (talep)
		P_RELEASE(h);	/* A10 */
	h->calisiyor = false;
	h->sure_ms = ktime_ms_delta(ktime_get(), t0);
	if (!h->sonuc)
		h->sonuc = "DUR";
	HD("bitti: sonuc %s; adim '%s'; CMD52 yazma %d, CMD53 yazma %d, tel %u B, %u ms; kapi reddi %d; hata '%s'; geri alma yazmasi YOK",
	   h->sonuc, h->adim ? h->adim : "-", h->c52, h->c53, h->tel, h->sure_ms, h->red, h->hata[0] ? h->hata : "-");
	kk_hs_ozet();
	kfree(h->tx);
	kfree(h->rx);
	kfree(h->ext);
	h->tx = h->rx = h->ext = NULL;
	h->tx_kap = 0;
	if (h->func)
		put_device(&h->func->dev);
	h->func = NULL;
	kk_h_durum(h, h->adim);
}

#if IS_ENABLED(CONFIG_KK_HI1102_M3A)
/* G54 M3a: kk-wifi probe'undan (bağlandıktan sonra) bir kez; kip 2 sabit. Erişim yok: iş kuyruğa verilir. */
int kk_hi1102_m3a_baslat(struct sdio_func *f)
{
	struct kk_h *h = kk_h_g;

	if (!h)
		return -ENODEV;
	if (!KK_H_KIP2_DERLI || kk_h_kip_azami < 2) {
		HU("M3a: kip 2 izinli degil (%s); motor baslamadi, erisim yok",
		   !KK_H_KIP2_DERLI ? "yer tutucu firmware: kip 2 derlenmedi" : "kk_hi1102_yukle.kip_azami=2 yok");
		return -EPERM;
	}
	if (test_and_set_bit(0, &kk_h_tetik_bit)) {
		h->tetik_red++;
		HU("M3a: TETIK REDDEDILDI (red #%d): bu acilista motor zaten basladi; hicbir erisim yapilmadi", h->tetik_red);
		return -EBUSY;
	}
	h->m3a_fn = f;
	h->kip = 2;
	h->sonuc = NULL;
	HD("TETIK kabul (kip 2: tam indirme); is kuyruguna verildi (M3a: kk-wifi probe'u, fn1 %s)", f ? sdio_func_id(f) : "sahte");
	kk_h_durum(h, "kuyrukta");
	queue_work(system_long_wq, &h->is);
	return 0;
}

void kk_hi1102_m3a_ayril(struct sdio_func *f)
{
	struct kk_h *h = kk_h_g;

	if (!h || h->m3a_fn != f)
		return;
	WRITE_ONCE(h->durdur, true);
	flush_work(&h->is);
	HD("M3a: surucu ayrildi; motor durdu (A10 yoklamasi kesildi)");
}
#else
static ssize_t yukle_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t n)
{
	struct kk_h *h = kk_h_g;
	int kip;

	if (!h)
		return -ENODEV;
	if (sysfs_streq(buf, "1"))
		kip = 1;
	else if (sysfs_streq(buf, "2"))
		kip = 2;
	else {
		HU("yukle: gecersiz deger (yalniz 1 ya da 2); hak harcanmadi, erisim yok");
		return -EINVAL;
	}
	if (kip == 2 && (!KK_H_KIP2_DERLI || kk_h_kip_azami < 2)) {
		HU("yukle: kip 2 izinli degil (%s); hak harcanmadi, erisim yok",
		   IS_ENABLED(CONFIG_KK_HI1102_YALNIZ_KIP1) ? "M2b yalniz kip 1 derlemesi: kip 2 DERLENMEDI" :
		   !KK_H_KIP2_DERLI ? "yer tutucu firmware: kip 2 derlenmedi" : "kk_hi1102_yukle.kip_azami=2 yok");
		return -EPERM;
	}
	if (test_and_set_bit(0, &kk_h_tetik_bit)) {
		h->tetik_red++;
		HU("TETIK REDDEDILDI (red #%d): bu acilista zaten tetiklendi (acilis basina 1); hicbir erisim yapilmadi",
		   h->tetik_red);
		return -EBUSY;
	}
	h->kip = kip;
	h->sonuc = NULL;
#if IS_ENABLED(CONFIG_KK_HI1102_YALNIZ_KIP1)
	HD("TETIK kabul (kip %d: el sikisma); is kuyruguna verildi", kip);
#else
	HD("TETIK kabul (kip %d: %s); is kuyruguna verildi", kip, kip == 1 ? "el sikisma" : "tam indirme");
#endif
	kk_h_durum(h, "kuyrukta");
	queue_work(system_long_wq, &h->is);
	return n;
}
static DEVICE_ATTR_WO(yukle);
#endif

#if IS_ENABLED(CONFIG_KK_HI1102_YALNIZ_KIP1)
#define KK_H_PARCA_GOSTER(h)	0u	/* parça yok (FILES derlenmedi) */
#else
#define KK_H_PARCA_GOSTER(h)	kk_h_parca_boy[(h)->pi]
#endif
static ssize_t yukle_durum_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct kk_h *h = kk_h_g;
	ssize_t r;

	if (!h)
		return -ENODEV;
	mutex_lock(&h->kilit);
	r = sysfs_emit(buf, "tetik=%d kip=%d adim='%s' sonuc='%s' cmd52_yazma=%d cmd53_yazma=%d tel=%u parca=%u sure_ms=%u surum='%s' kapi_reddi=%d tetik_red=%d hata='%s' firmware=%s kip_azami=%u a9_kesme=%d a9_ack=%d a9_veri=%d\n",
		       test_bit(0, &kk_h_tetik_bit) ? 1 : 0, h->kip, h->adim ? h->adim : "-",
		       h->calisiyor ? "calisiyor" : (h->sonuc ? h->sonuc : "-"), h->c52, h->c53, h->tel,
		       KK_H_PARCA_GOSTER(h), h->sure_ms, h->surum, h->red, h->tetik_red, h->hata[0] ? h->hata : "-",
		       KK_H_FW_DURUM, kk_h_kip_azami, h->a9_kesme, h->a9_ack, h->a9_veri);
#if IS_ENABLED(CONFIG_KK_HI1102_M3A)
	/* G54 M3a: P-D satırının SONUNA (önceki alanlar aynı yerde); son '\n' yerine ek alanlar */
	if (r > 0 && buf[r - 1] == '\n')
		r--;
	r += sysfs_emit_at(buf, r, " m3a=1 hcc=%s a10_kesme=%d a10_ack=%d a10_veri=%d a10_ms=%u hcc_paket=%d oam=%d diger=%d gecersiz=%d\n",
			   h->hcc_on ? "ON" : "OFF", h->a10_kesme, h->a10_ack, h->a10_veri, h->a10_ms, h->pk_n, h->pk_oam,
			   h->pk_diger, h->pk_gecersiz);
#endif
	mutex_unlock(&h->kilit);
	return r;
}
static DEVICE_ATTR_RO(yukle_durum);

static struct attribute *kk_h_attrs[] = {
#if !IS_ENABLED(CONFIG_KK_HI1102_M3A)
	&dev_attr_yukle.attr,		/* G54 M3a: YOK (tetik yalnız kk_wifi/baslat -> probe) */
#endif
	&dev_attr_yukle_durum.attr,
	NULL,
};
static const struct attribute_group kk_h_grup = { .attrs = kk_h_attrs };

/* Kapı probe'unun sonunda: yalnız sysfs; SDIO/MMIO/PMIC erişimi YOK. */
void kk_hi1102_ekle(struct device *kapi)
{
	struct kk_h *h;
	int i;

	if (!kk_h_ac) {
		dev_info(kapi, "kk: hi1102: kapali (kk_hi1102_yukle.ac=1 yok); yukle dosyasi yok, SDIO erisimi yok\n");
		return;
	}
	if (kk_h_g)
		return;
	h = devm_kzalloc(kapi, sizeof(*h), GFP_KERNEL);
	if (!h)
		return;
	h->dev = kapi;
	mutex_init(&h->kilit);
	INIT_WORK(&h->is, kk_h_is);
#if !IS_ENABLED(CONFIG_KK_HI1102_YALNIZ_KIP1)
	for (i = 0; i < KK_H_PARCA_SECENEK; i++)
		if (kk_h_parca_boy[i] == kk_h_parca_param)
			h->pi = i;
#endif
	if (devm_device_add_group(kapi, &kk_h_grup)) {
		dev_warn(kapi, "kk: hi1102: sysfs grubu eklenemedi; yukleyici kapali\n");
		return;
	}
	kk_h_g = h;
#if IS_ENABLED(CONFIG_KK_HI1102_YALNIZ_KIP1)
	(void)i;
	HD("basladi (ac=1; tetik bekliyor; 0 SDIO erisimi); M2b YALNIZ KIP 1 derlemesi: kip 2 DERLENMEDI (kip_azami=%u etkisiz); firmware YOK; indirme kodu YOK; tablo %d adim (READM, VERSION; cfg %.8s)",
	   kk_h_kip_azami, KK_H_ADIM_SAYI, KK_H_CFG_SHA256);
#else
	HD("basladi (ac=1; tetik bekliyor; 0 SDIO erisimi); kip_azami=%u (kip 2 %s); firmware %s (sha %.8s/%.8s/%.8s); tablo %d adim (cfg %.8s, band_5g_enable=%d); parca %u",
	   kk_h_kip_azami, KK_H_KIP2_DERLI ? (kk_h_kip_azami >= 2 ? "IZINLI" : "kapali") : "DERLENMEDI",
	   KK_H_FW_GERCEK ? "GERCEK (sha = 4.9)" : "YER TUTUCU", kk_h_fw_sha256[0], kk_h_fw_sha256[1], kk_h_fw_sha256[2],
	   KK_H_ADIM_SAYI, KK_H_CFG_SHA256, KK_H_BAND_5G, kk_h_parca_boy[h->pi]);
#endif
#if IS_ENABLED(CONFIG_KK_HI1102_M3A)
	HD("M3a: yukle dosyasi YOK; tetik /sys/kernel/kk_wifi/baslat -> kk-hi1102 probe (0x0296/0x5347 fn1) -> A0-A9 (P-D) -> A10 hcc_enable + HCC alim %d ms; cfg80211/netdev/tx DERLENMEDI",
	   KK_H_A10_MS);
#endif
}

MODULE_DESCRIPTION("Kara Kutu: Hi1102 ROM firmware yukleyicisi (M2; sysfs tetigi, gomulu firmware, derleme ani tablo)");
MODULE_LICENSE("GPL");
