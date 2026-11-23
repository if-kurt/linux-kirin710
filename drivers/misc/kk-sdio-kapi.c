// SPDX-License-Identifier: GPL-2.0
/*
 * Kara Kutu G45 (Wi-Fi M1) / G46 (W1) / G47 (S3, Faz B): dwmmc2 (fc183000, SDIO, Hi1102) kapısı. G40 kk-i2c6-kapi deseni.
 * Tasarım: work/g44/wifi/TASARIM-wifi-m1.md §3.1-3.2, §4.3, §5.3 (W1-W7). G47 kararları: work/g47/s3/RAPOR-s3.md.
 *
 * VARSAYILAN KAPALI: cmdline'da kk_sdio_kapi.ac=1 yoksa probe tek satır log basar ve döner; hiçbir MMIO/SPMI
 * erişimi yok, eşleme bile yapılmaz, sysfs dosyası da yok.
 *
 * G47: DİZİ AÇILIŞTA BAŞLAMAZ. ac=1 iken açılışta (probe) YALNIZ OKUMA yapılır (G46 Faz A okumaları: 0. ve 1.,
 * 2. ön koşul değerlendirmesi, P1 için 0x3E'ye ön bakış). Yazma dizisi çalışırken sysfs ile tetiklenir:
 *   echo 1 > /sys/bus/platform/devices/<kapi>/baslat     (cihazda <kapi> = fff35000.sdio-kapi)
 * Tetik AÇILIŞ BAŞINA BİR KEZ kabul edilir (ikinci tetik -EBUSY, hiçbir erişim yok). /sys/.../durum yalnız bellekteki
 * alanları basar (donanıma erişmez). Böylece bir takılma reset sonrası açılış döngüsüne dönüşemez.
 *
 * Tetikte sıra (her adım: oku -> hedefteyse yazma yok -> beklenmeyen değerse DUR -> yaz -> geri oku -> XOR
 * yalnız hedef bit mi). DUR = sonraki adımlar yapılmaz, MMIO geri alması yok, dwmmc2 aygıtı oluşturulmaz.
 *   1. Taze önce dökümü (CRG durum; bloklar yalnız saat açık + reset bırakılmışsa; PMIC 0x3D/0x3E/0x3F).
 *   2. Ön koşullar (yalnız okuma).
 *   3. P1 PMIC 0x3E (clk_pmu32kb, b0 en_32k_bt). 0x01 ise yazma yok. Değilse:
 *        - CONFIG_KK_SDIO_KAPI_PMIC_YAZ=n: YAZMA YOLU DERLENMEZ -> DUR (0 yazma).
 *        - =y, cmdline kk_sdio_kapi.pmic_yaz=1 YOK: -> DUR (0 yazma).
 *        - =y ve kk_sdio_kapi.pmic_yaz=1 (W1, Furkan onayı G47): oku-değiştir-yaz yalnız 0x00 -> 0x01 (yazmadan hemen
 *          önce 0x3D/0x3E/0x3F taze okunur, döküm ile aynı olmalı); TEK yazma yeri kk_pmic_yaz_tek; ardından HER
 *          DURUMDA (regmap hata dönse de) P1'in kendi geri okuması: 0x3E = 0x01 ve 0x3D/0x3F değişmemiş.
 *          G47 GERİ ALMA KURALI: 0x3E <- 0x00 YALNIZ P1'in kendi geri okuması tutmazsa (okunamaz ya da beklenen
 *          değil) yapılır; 0x3E zaten 0x00 okunuyorsa yazılmaz. Geri okuma tutup regmap hata döndüyse geri alma yok,
 *          DUR. P1 geçtikten sonra bir adımda DUR olursa 0x3E'ye DOKUNULMAZ (32 kHz açık kalır; 4.9 da açık tutar).
 *          Açılış başına en çok 2 PMIC yazması (W1 + yalnız geri okuma tutmazsa bir geri alma).
 *   4. CRG (tek bit; yalnız PEREN/RSTDIS/hiword): C2 CLKDIV19 b6, C1 PEREN0 b21, C4 PEREN4 b19, C5 PERRSTDIS4 b20.
 *   5. GPIO3_6 (wlan_en): 10 ms -> DATA maskeli adres +0x100 <- 0x40 -> geri okuma -> 30 ms.
 *   6. Pinmux iomg-mmc1 ×6: 0 -> 1 (1 ise yazma yok).
 *   7. Sonra dökümü ve değişmezler; hepsi tutarsa dwmmc2 için "dwmmc_k3" platform aygıtı oluşturulur
 *      (DT düğümü status="disabled"; of_platform onu oluşturmaz).
 *   8. G47: SDIO kart denetimi (yalnız bellekteki çekirdek yapıları okunur; karta/yazmaca erişim yok): mmc çekirdeğinin
 *      CMD5/CCCR/CIS sayımı sonunda kart görüldü mü, CIS vendor/device 0x0296/0x5347 mi, SDIO fonksiyonlarına
 *      sürücü bağlandı mı (bağlanmamalı: bu imajda hiçbir SDIO fonksiyon sürücüsü derli değil, aşağıdaki #error).
 *
 * Bütün MMIO yazmaları tek işlevde (kk_sdio_yaz) ve derlenmiş izin tablosundaki (blok, ofset, değer) üçlüleriyle
 * sınırlı (kk_sdio_izin, 11 girdi). iocg-mmc1 ve iomg'a yazma yolu yok. sysfs bind/unbind kapalı.
 */
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/mmc/card.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sdio_func.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/spmi.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/workqueue.h>
#if IS_ENABLED(CONFIG_KK_SDIO_KAPI_SAHTE)
#include <linux/mfd/syscon.h>
#endif
#include "kk-sdio-kapi.h"
#include "kk-hi1102-yukle.h"	/* G48: Hi1102 yükleyicisi (CONFIG_KK_HI1102_YUKLE; kapalıyken boş satır içi) */

/*
 * G47 (S3): kart SDIO'da görünene kadar gidilir; firmware yükleme (M2) ve ağ arayüzü YOK. Bu ağaçtaki her SDIO
 * fonksiyon sürücüsünün (sdio_register_driver/module_sdio_driver) Kconfig simgesi; biri açıksa derleme durur.
 * WILC1000_SDIO özellikle: kimlik tablosu 0x0296/0x5347 (= Hi1102'nin CIS kimliği) ile eşleşir.
 * build.sh ayrıca vmlinux'ta sdio_register_driver çağıranı ve request_firmware olmadığını denetler.
 */
#if IS_ENABLED(CONFIG_WILC1000_SDIO) || IS_ENABLED(CONFIG_SDIO_UART) || (IS_ENABLED(CONFIG_CFG80211) && !IS_ENABLED(CONFIG_KK_HI1102_M3B)) || \
	IS_ENABLED(CONFIG_BT) || IS_ENABLED(CONFIG_SMS_SDIO_DRV) || IS_ENABLED(CONFIG_RTL8723BS) || \
	IS_ENABLED(CONFIG_BRCMFMAC_SDIO) || IS_ENABLED(CONFIG_MWIFIEX_SDIO) || IS_ENABLED(CONFIG_ATH6KL_SDIO) || \
	IS_ENABLED(CONFIG_ATH10K_SDIO) || IS_ENABLED(CONFIG_LIBERTAS_SDIO) || IS_ENABLED(CONFIG_WLCORE_SDIO) || \
	IS_ENABLED(CONFIG_WL1251_SDIO) || IS_ENABLED(CONFIG_RSI_SDIO) || IS_ENABLED(CONFIG_CW1200_WLAN_SDIO) || \
	IS_ENABLED(CONFIG_B43_SDIO) || IS_ENABLED(CONFIG_MT7663S) || IS_ENABLED(CONFIG_MT7921S) || \
	IS_ENABLED(CONFIG_WFX) || IS_ENABLED(CONFIG_RTW88_8723CS) || IS_ENABLED(CONFIG_RTW88_8723DS) || \
	IS_ENABLED(CONFIG_RTW88_8821CS) || IS_ENABLED(CONFIG_RTW88_8822BS) || IS_ENABLED(CONFIG_RTW88_8822CS) || \
	IS_ENABLED(CONFIG_BT_HCIBTSDIO) || IS_ENABLED(CONFIG_BT_MRVL_SDIO) || IS_ENABLED(CONFIG_BT_MTKSDIO)
#error "kk-sdio-kapi (G47 S3): bir SDIO fonksiyon surucusu derli; kart yalniz sayilmali (M2/firmware/ag arayuzu yok)"
#endif

static bool kk_ac;
module_param_named(ac, kk_ac, bool, 0444);
MODULE_PARM_DESC(ac, "1 = kapi calisir (varsayilan 0: hicbir erisim yok)");

#if IS_ENABLED(CONFIG_KK_SDIO_KAPI_PMIC_YAZ)
static bool kk_pmic_yaz;
module_param_named(pmic_yaz, kk_pmic_yaz, bool, 0444);
MODULE_PARM_DESC(pmic_yaz, "1 = PMIC 0x3E 0x00 -> 0x01 tek yazmasina izin (W1; varsayilan 0)");
#define KK_PMIC_DURUM	(kk_pmic_yaz ? "DERLENDI, pmic_yaz=1 (W1 ACIK)" : "DERLENDI, pmic_yaz=0 (yazma kapali)")
#else
#define KK_PMIC_DURUM	"DERLENMEDI"
#endif

/* CRG (fff35000) */
#define CRG_PEREN0	0x000
#define CRG_PERCLKEN0	0x008
#define CRG_PERSTAT0	0x00c
#define CRG_PERCLKEN1	0x018
#define CRG_PERSTAT1	0x01c
#define CRG_PERCLKEN2	0x028
#define CRG_PERSTAT2	0x02c
#define CRG_PEREN4	0x040
#define CRG_PERCLKEN4	0x048
#define CRG_PERSTAT4	0x04c
#define CRG_PERRSTSTAT0	0x068
#define CRG_PERRSTSTAT1	0x074
#define CRG_PERRSTSTAT2	0x080
#define CRG_PERRSTDIS4	0x094
#define CRG_PERRSTSTAT4	0x098
#define CRG_CLKDIV6	0x0c0
#define CRG_CLKDIV19	0x0f4
#define CRG_PERCLKEN7	0x428
#define CRG_PERSTAT7	0x42c

/* PL061 */
#define GPIO_B6_MASKELI	0x100	/* adres bitleri [9:2] = maske; 0x100 >> 2 = 0x40 = yalnız b6 */
#define GPIO_DATA_HEPSI	0x3fc
#define GPIO_DIR	0x400
#define GPIO_IE		0x410
#define WLAN_EN		0x40u	/* BIT(6); u32 */

#define IOMG_GPIO3_6	0x5c

/* PMIC (hi6555v200, SPMI sid 0) */
#define PMIC_32K_GPS	0x3d
#define PMIC_32K_BT	0x3e	/* PMIC_OSC32K_BT_ONOFF_CTRL: b0 en_32k_bt (clk_pmu32kb), b7:1 reserved */
#define PMIC_32K_SYS	0x3f

/* G47: beklenen SDIO kimliği (Hi1102; 4.9 sürücüsünün kimlik tablosuyla aynı; görev tanımı) */
#define KK_SDIO_VENDOR	0x0296
#define KK_SDIO_DEVICE	0x5347
#define KK_KART_TUR	20	/* kart denetimi: 1 sn arayla en çok 20 tur */

static const char * const kk_blok_ad[KB_SAYI] = { "crg", "iomg-mmc1", "iocg-mmc1", "iomg", "gpio3" };
/* Haritalanan en küçük boyut (okunan en büyük ofset + 4) */
static const u32 kk_blok_min[KB_SAYI] = { CRG_PERSTAT7 + 4, 0x18, 0x18, IOMG_GPIO3_6 + 4, GPIO_IE + 4 };

/* İzinli MMIO yazmalarının TAMAMI: (blok, ofset, değer). kk_sdio_yaz bunun dışında hiçbir şey yazmaz. */
struct kk_sdio_izin_t {
	u8 blok;
	u16 off;
	u32 val;
	const char *ad;
};
static const struct kk_sdio_izin_t kk_sdio_izin[] = {
	{ KB_CRG,	CRG_CLKDIV19,	0x00400040, "W2 C2 CLKDIV19 b6 sc_gt_clk_sdio1_sys (hiword)" },
	{ KB_CRG,	CRG_PEREN0,	0x00200000, "W3 C1 PEREN0 b21 gt_hclk_sdio" },
	{ KB_CRG,	CRG_PEREN4,	0x00080000, "W4 C4 PEREN4 b19 gt_clk_sdio" },
	{ KB_CRG,	CRG_PERRSTDIS4,	0x00100000, "W5 C5 PERRSTDIS4 b20 ip_hrst_sdio" },
	{ KB_GPIO3,	GPIO_B6_MASKELI, WLAN_EN,   "W7 GPIO3_6 wlan_en yuksek (maskeli DATA)" },
	{ KB_IOMG_MMC1,	0x00,		1,	    "W6 X1 iomg-mmc1 pin0 SDIO" },
	{ KB_IOMG_MMC1,	0x04,		1,	    "W6 X1 iomg-mmc1 pin1 SDIO" },
	{ KB_IOMG_MMC1,	0x08,		1,	    "W6 X1 iomg-mmc1 pin2 SDIO" },
	{ KB_IOMG_MMC1,	0x0c,		1,	    "W6 X1 iomg-mmc1 pin3 SDIO" },
	{ KB_IOMG_MMC1,	0x10,		1,	    "W6 X1 iomg-mmc1 pin4 SDIO" },
	{ KB_IOMG_MMC1,	0x14,		1,	    "W6 X1 iomg-mmc1 pin5 SDIO" },
};

/* CRG adımları (sıra = tasarım §3.2-3: C2 -> C1 -> C4 -> C5) */
struct kk_crg_adim {
	const char *ad;
	u16 yaz_off;
	u32 yaz_val;
	u16 durum_off;
	u8 bit;
	u8 hedef;	/* bitin hedef değeri */
};
static const struct kk_crg_adim kk_sdio_crg[] = {
	{ "C2 CLKDIV19 b6 (sdio1 sys kapisi)",	CRG_CLKDIV19,	0x00400040, CRG_CLKDIV19,    6,  1 },
	{ "C1 hclk_sdio (PERSTAT0 b21)",	CRG_PEREN0,	0x00200000, CRG_PERSTAT0,    21, 1 },
	{ "C4 clk_sdio (PERSTAT4 b19)",		CRG_PEREN4,	0x00080000, CRG_PERSTAT4,    19, 1 },
	{ "C5 ip_hrst_sdio (PERRSTSTAT4 b20)",	CRG_PERRSTDIS4,	0x00100000, CRG_PERRSTSTAT4, 20, 0 },
};

/* Önce/sonra dökümünde okunan CRG yazmaçları */
static const u16 kk_crg_oku[] = {
	CRG_PERCLKEN0, CRG_PERSTAT0, CRG_PERCLKEN1, CRG_PERSTAT1, CRG_PERCLKEN2, CRG_PERSTAT2,
	CRG_PERCLKEN4, CRG_PERSTAT4, CRG_PERCLKEN7, CRG_PERSTAT7,
	CRG_PERRSTSTAT0, CRG_PERRSTSTAT1, CRG_PERRSTSTAT2, CRG_PERRSTSTAT4, CRG_CLKDIV6, CRG_CLKDIV19,
};
#define KK_CRG_N ARRAY_SIZE(kk_crg_oku)

/* Blok okumadan önce aranan erişilebilirlik (saat açık, reset bırakılmış) ve ön koşul bitleri */
struct kk_kosul {
	const char *ad;
	u16 off;
	u8 bit;
	u8 olmali;
};
static const struct kk_kosul kk_erisim[] = {
	{ "clk_mmc1_peribus PERSTAT0 b15",	CRG_PERSTAT0,	 15, 1 },
	{ "prst_mmc1pericfg PERRSTSTAT0 b11",	CRG_PERRSTSTAT0, 11, 0 },
	{ "pclk_mmc1_ioc PERSTAT7 b21",		CRG_PERSTAT7,	 21, 1 },
	{ "prst_mmc1_ioc PERRSTSTAT4 b10",	CRG_PERRSTSTAT4, 10, 0 },
	{ "pclk_ioc PERSTAT2 b25",		CRG_PERSTAT2,	 25, 1 },
	{ "prst_ioc PERRSTSTAT2 b25",		CRG_PERRSTSTAT2, 25, 0 },
	{ "pclk_gpio3 PERSTAT1 b3",		CRG_PERSTAT1,	 3,  1 },
	{ "prst_gpio3 PERRSTSTAT1 b3",		CRG_PERRSTSTAT1, 3,  0 },
	{ "ip_rst_sdio PERRSTSTAT4 b26",	CRG_PERRSTSTAT4, 26, 0 },
	{ "CLKDIV6 b6 muxsy (sys yolu; C3 yok)", CRG_CLKDIV6,	 6,  0 },
};

struct kk_dokum {
	u32 crg[KK_CRG_N];
	u32 mx[6], cf[6], iomg5c, dir, dat, ie;
	unsigned int pm[3];	/* 0x3D, 0x3E, 0x3F */
};

/* G47: P1'in sonucu (durum dosyası ve DUR satırı için) */
enum kk_p1 {
	P1_YOK,		/* P1'e gelinmedi ya da 0x3E zaten 0x01 (yazma yok) */
	P1_TAMAM,	/* yazma + geri okuma tuttu (0x3E = 0x01) */
	P1_HATA_TUTTU,	/* regmap hata döndü ama geri okuma tuttu: geri alma YOK, 0x3E = 0x01 kaldı, DUR */
	P1_GERI_ALINDI,	/* geri okuma tutmadı -> 0x3E <- 0x00 (2. yazma) */
	P1_GERI_GEREKMEDI, /* geri okuma tutmadı ama 0x3E 0x00 okunuyor -> geri alma yazması yok */
	P1_GERI_HATA,	/* geri alma yazması hata döndü */
};
static const char * const kk_p1_ad[] = { "yok", "tamam", "hata-tuttu", "geri-alindi", "geri-gerekmedi", "geri-hata" };

struct kk_kapi {
	struct device *dev;
	void __iomem *b[KB_SAYI];
	phys_addr_t fiz[KB_SAYI];
	struct regmap *pmic;
	struct resource mmc_res[2];
	struct device_node *mmc_np;
	struct platform_device *mmc_pdev;
	int yazma, pmic_yazma;
	/* G47 */
	struct kk_dokum acilis;		/* açılış (probe) okumaları */
	bool acilis_ok;			/* açılış okumaları + ön koşullar tuttu */
	int tetik_red;			/* reddedilen tetik sayısı */
	enum kk_p1 p1;
	const char *sonuc;		/* durum dosyası için */
	struct mutex kilit;		/* kart[] metni */
	struct delayed_work kart_is;
	int kart_tur, kart_ilk;
	char kart[200];
#if IS_ENABLED(CONFIG_KK_HI1102_YUKLE)
	/* G48: kart denetiminin bulduğu fn1 (yalnız beklenen kimlik + sürücüsüz + tek fonksiyonsa; get_device'lı) */
	struct sdio_func *h_fn;
	bool h_bitti;
#endif
};

static bool kk_sdio_calisti;		/* probe: açılış başına bir kez */
static unsigned long kk_tetik_bit;	/* sysfs tetiği: açılış başına bir kez (bit 0) */

#define KD(fmt, ...)	dev_info(k->dev, "kk: sdio kapi: " fmt "\n", ##__VA_ARGS__)
#define KU(fmt, ...)	dev_warn(k->dev, "kk: sdio kapi: " fmt "\n", ##__VA_ARGS__)
#define B(v, n)		(((v) >> (n)) & 1)

static inline u32 kk_oku(struct kk_kapi *k, unsigned int blok, u32 off)
{
	return readl(k->b[blok] + off);
}

static u32 kk_crg_deger(const struct kk_dokum *d, u16 off)
{
	unsigned int i;

	for (i = 0; i < KK_CRG_N; i++)
		if (kk_crg_oku[i] == off)
			return d->crg[i];
	return 0;
}

/* TEK MMIO yazma yeri. Yalnız kk_sdio_izin'deki üçlüler; değilse yazmaz, -EPERM (çağıran DUR der). */
static noinline int kk_sdio_yaz(struct kk_kapi *k, unsigned int blok, u32 off, u32 val)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(kk_sdio_izin); i++)
		if (kk_sdio_izin[i].blok == blok && kk_sdio_izin[i].off == off && kk_sdio_izin[i].val == val)
			break;
	if (i == ARRAY_SIZE(kk_sdio_izin) || blok >= KB_SAYI || !k->b[blok]) {
		dev_err(k->dev, "kk: sdio kapi: IZINSIZ yazma istegi blok %u +0x%x <- 0x%08x; YAPILMADI -> DUR\n",
			blok, off, val);
		return -EPERM;
	}
	kk_sdio_sahte_once(k->b[blok], blok, off);
	writel(val, k->b[blok] + off);
	k->yazma++;
	kk_sdio_sahte_yansit(k->b[blok], blok, off, val);
	KD("YAZMA #%d %s fiz 0x%llx <- 0x%08x (%s)", k->yazma, kk_blok_ad[blok],
	   (unsigned long long)(k->fiz[blok] + off), val, kk_sdio_izin[i].ad);
	return 0;
}

static int kk_pmic_oku3(struct kk_kapi *k, unsigned int v[3])
{
	static const unsigned int adr[3] = { PMIC_32K_GPS, PMIC_32K_BT, PMIC_32K_SYS };
	int i, r;

	for (i = 0; i < 3; i++) {
		r = regmap_read(k->pmic, adr[i], &v[i]);
		r = kk_sdio_sahte_pmic_okundu(adr[i], &v[i], r);	/* yalnız QEMU W1 hata enjeksiyonu; aksi hâlde r */
		if (r) {
			KU("PMIC 0x%02x okunamadi (hata %d) -> DUR", adr[i], r);
			return r;
		}
	}
	return 0;
}

/* Okumalar; erisim = blok okumalarının koşulu. Dönüş: 0 tamam, <0 DUR. */
static int kk_dokum_al(struct kk_kapi *k, struct kk_dokum *d, const char *an, bool kosul_denetle)
{
	unsigned int i;

	for (i = 0; i < KK_CRG_N; i++)
		d->crg[i] = kk_oku(k, KB_CRG, kk_crg_oku[i]);
	KD("[%s] crg PERCLKEN0=0x%08x PERSTAT0=0x%08x PERCLKEN1=0x%08x PERSTAT1=0x%08x PERCLKEN2=0x%08x PERSTAT2=0x%08x",
	   an, d->crg[0], d->crg[1], d->crg[2], d->crg[3], d->crg[4], d->crg[5]);
	KD("[%s] crg PERCLKEN4=0x%08x PERSTAT4=0x%08x PERCLKEN7=0x%08x PERSTAT7=0x%08x",
	   an, d->crg[6], d->crg[7], d->crg[8], d->crg[9]);
	KD("[%s] crg PERRSTSTAT0=0x%08x PERRSTSTAT1=0x%08x PERRSTSTAT2=0x%08x PERRSTSTAT4=0x%08x CLKDIV6=0x%08x CLKDIV19=0x%08x",
	   an, d->crg[10], d->crg[11], d->crg[12], d->crg[13], d->crg[14], d->crg[15]);
	KD("[%s] cozum hclk_sdio st=%u clk_sdio st=%u hrst_sdio(b20)=%u rst_sdio(b26)=%u gt_sdio1_sys(CLKDIV19 b6)=%u muxsy(CLKDIV6 b6)=%u",
	   an, B(kk_crg_deger(d, CRG_PERSTAT0), 21), B(kk_crg_deger(d, CRG_PERSTAT4), 19),
	   B(kk_crg_deger(d, CRG_PERRSTSTAT4), 20), B(kk_crg_deger(d, CRG_PERRSTSTAT4), 26),
	   B(kk_crg_deger(d, CRG_CLKDIV19), 6), B(kk_crg_deger(d, CRG_CLKDIV6), 6));

	if (kosul_denetle) {
		int kotu = 0;

		for (i = 0; i < ARRAY_SIZE(kk_erisim); i++) {
			u32 v = kk_crg_deger(d, kk_erisim[i].off);

			if (B(v, kk_erisim[i].bit) != kk_erisim[i].olmali) {
				KU("ON KOSUL TUTMADI: %s = %u (olmali %u)", kk_erisim[i].ad, B(v, kk_erisim[i].bit),
				   kk_erisim[i].olmali);
				kotu++;
			}
		}
		if (kotu) {
			KU("%d CRG on kosulu tutmadi; bloklar OKUNMADI, hicbir sey yazilmadi -> DUR", kotu);
			return -ENODEV;
		}
	}

	for (i = 0; i < 6; i++) {
		d->mx[i] = kk_oku(k, KB_IOMG_MMC1, i * 4);
		d->cf[i] = kk_oku(k, KB_IOCG_MMC1, i * 4);
	}
	d->iomg5c = kk_oku(k, KB_IOMG, IOMG_GPIO3_6);
	d->dir = kk_oku(k, KB_GPIO3, GPIO_DIR);
	d->dat = kk_oku(k, KB_GPIO3, GPIO_DATA_HEPSI);
	d->ie = kk_oku(k, KB_GPIO3, GPIO_IE);
	KD("[%s] iomg-mmc1 = %x %x %x %x %x %x (1=SDIO 0=GPIO); iocg-mmc1 = 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x (yalniz okunur)",
	   an, d->mx[0], d->mx[1], d->mx[2], d->mx[3], d->mx[4], d->mx[5],
	   d->cf[0], d->cf[1], d->cf[2], d->cf[3], d->cf[4], d->cf[5]);
	KD("[%s] iomg+0x5c (GPIO3_6) = 0x%x; gpio3 DIR=0x%02x DATA=0x%02x IE=0x%02x; wlan_en yon=%s seviye=%u",
	   an, d->iomg5c, d->dir & 0xff, d->dat & 0xff, d->ie & 0xff, B(d->dir, 6) ? "cikis" : "giris", B(d->dat, 6));
	if (kk_pmic_oku3(k, d->pm))
		return -ENODEV;
	KD("[%s] pmic 0x3d=0x%02x 0x3e=0x%02x (en_32k_bt=%u) 0x3f=0x%02x", an, d->pm[0], d->pm[1], B(d->pm[1], 0), d->pm[2]);
	return 0;
}

static int kk_on_kosul(struct kk_kapi *k, const struct kk_dokum *d)
{
	int i, kotu = 0;

	if (d->iomg5c & 7) {
		KU("ON KOSUL TUTMADI: iomg+0x5c fonk %u (GPIO3_6 GPIO islevinde degil)", d->iomg5c & 7);
		kotu++;
	}
	if (!B(d->dir, 6)) {
		KU("ON KOSUL TUTMADI: GPIO3 DIR b6 = 0 (wlan_en cikis degil; DIR'e yazma yolu yok)");
		kotu++;
	}
	if (B(d->dat, 6)) {
		KU("ON KOSUL TUTMADI: GPIO3 DATA b6 = 1 (wlan_en zaten yuksek; 4.9 sirasi dusukten baslar)");
		kotu++;
	}
	for (i = 0; i < 6; i++) {
		if (d->mx[i] > 1) {
			KU("ON KOSUL TUTMADI: iomg-mmc1 +0x%02x = 0x%x (0 ya da 1 degil)", i * 4, d->mx[i]);
			kotu++;
		}
	}
	if (kotu) {
		KU("%d deger on kosulu tutmadi; hicbir sey yazilmadi -> DUR", kotu);
		return -ENODEV;
	}
	KD("on kosullar tamam (CRG erisim/reset, b26=0, CLKDIV6 b6=0, iomg+0x5c=GPIO, wlan_en cikis+dusuk, pinmux 0/1)");
	return 0;
}

#if IS_ENABLED(CONFIG_KK_SDIO_KAPI_PMIC_YAZ)
/*
 * G46 W1: TEK PMIC yazma yeri (kapıda başka regmap_write/update_bits yok). Yazmaç sabit PMIC_32K_BT (0x3E).
 * Değer yalnız iki biçimde: geri=false -> 0x01 (W1; yalnız açılışın 1. PMIC yazması olarak) ya da geri=true ->
 * 0x00 (geri alma = W1 öncesi değer; yalnız 1. yazmadan sonra, 2. yazma olarak). Başka sırada istek -> yazmaz.
 */
static noinline int kk_pmic_yaz_tek(struct kk_kapi *k, bool geri)
{
	unsigned int val = geri ? 0x00 : 0x01;
	int r;

	if (k->pmic_yazma != (geri ? 1 : 0)) {
		dev_err(k->dev, "kk: sdio kapi: IZINSIZ PMIC yazma istegi 0x3e <- 0x%02x (onceki PMIC yazma %d); YAPILMADI\n",
			val, k->pmic_yazma);
		return -EPERM;
	}
	r = regmap_write(k->pmic, PMIC_32K_BT, val);
	k->pmic_yazma++;
	r = kk_sdio_sahte_pmic_yazdi(PMIC_32K_BT, val, r);	/* yalnız QEMU W1 hata enjeksiyonu; aksi hâlde r */
	KD("PMIC YAZMA #%d 0x3e <- 0x%02x (%s); regmap sonuc %d", k->pmic_yazma, val,
	   geri ? "W1 GERI ALMA: onceki deger" : "W1 en_32k_bt", r);
	return r;
}

/*
 * G47 geri alma: YALNIZ P1'in kendi geri okuması tutmadığında (okunamadı ya da beklenen değil) çağrılır. 0x3E'yi
 * W1 öncesi değerine (0x00; P1 yalnız 0x00 iken yazar) döndürür. 0x3E zaten 0x00 okunuyorsa yazmaz. En çok 1 kez
 * (kk_pmic_yaz_tek sırası). Sonuç ne olursa olsun DUR.
 */
static int kk_pmic_geri_al(struct kk_kapi *k, const char *neden)
{
	unsigned int v = 0xff, s[3];
	int r;

	r = regmap_read(k->pmic, PMIC_32K_BT, &v);
	r = kk_sdio_sahte_pmic_okundu(PMIC_32K_BT, &v, r);
	if (!r && v == 0x00) {
		k->p1 = P1_GERI_GEREKMEDI;
		KU("P1 GERI ALMA gerekmiyor (%s): 0x3e = 0x00 okunuyor -> DUR", neden);
		return -ENODEV;
	}
	KU("P1 GERI ALMA (%s): 0x3e okuma %s0x%02x -> 0x00 yaziliyor", neden, r ? "HATA, son deger " : "", v);
	r = kk_pmic_yaz_tek(k, true);
	if (r) {
		k->p1 = P1_GERI_HATA;
		KU("P1 GERI ALMA yazmasi hata %d; baska deneme yok -> DUR", r);
		return -ENODEV;
	}
	k->p1 = P1_GERI_ALINDI;
	if (kk_pmic_oku3(k, s)) {
		KU("P1 GERI ALMA sonrasi PMIC okunamadi -> DUR");
		return -ENODEV;
	}
	KU("P1 GERI ALMA geri okuma: 0x3e = 0x%02x %s; 0x3d=0x%02x 0x3f=0x%02x -> DUR", s[1],
	   s[1] == 0x00 ? "AYNI (0x00)" : "FARKLI", s[0], s[2]);
	return -ENODEV;
}
#endif

/* Açılışta (yalnız okuma): P1'in tetikte ne yapacağını söyler. Hiçbir erişim yapmaz (açılış dökümünü kullanır). */
static const char *kk_pmic_onbakis(const struct kk_dokum *d)
{
	if (d->pm[1] == 0x01)
		return "0x3e = 0x01: tetikte P1 yazma yok";
#if IS_ENABLED(CONFIG_KK_SDIO_KAPI_PMIC_YAZ)
	if (kk_pmic_yaz)
		return d->pm[1] == 0x00 ? "0x3e = 0x00: tetikte W1 0x00 -> 0x01 yapilacak (iki kilit acik)" :
					  "0x3e reserved dolu: tetikte P1'de DUR";
	return "0x3e != 0x01 ve pmic_yaz=1 yok: tetikte P1'de DUR (0 yazma)";
#else
	return "0x3e != 0x01 ve PMIC yazma yolu DERLENMEDI: tetikte P1'de DUR (0 yazma)";
#endif
}

/* P1: PMIC 0x3E b0. PMIC_YAZ=n ya da pmic_yaz=1 yoksa yazma yolu yok/kapalı: 0x01 değilse DUR. */
static int kk_pmic_adim(struct kk_kapi *k, const struct kk_dokum *d)
{
	unsigned int o1 = d->pm[1];

	if (o1 == 0x01) {
		KD("P1 PMIC 0x3e = 0x01 (32 kHz zaten acik); yazma yok");
		return 0;
	}
#if IS_ENABLED(CONFIG_KK_SDIO_KAPI_PMIC_YAZ)
	if (kk_pmic_yaz) {
		unsigned int o0 = d->pm[0], o2 = d->pm[2], yeni = o1 | BIT(0), t[3], s[3];
		int yr, n0 = k->pmic_yazma;

		/* oku-değiştir-yaz: yalnız reserved bitler (b7:1) 0 iken; yeni = eski | b0 = 0x01 */
		if (o1 != 0x00 || yeni != 0x01) {
			KU("P1 PMIC 0x3e = 0x%02x (0x00/0x01 degil: reserved bit dolu); yazilmadi -> DUR", o1);
			return -ENODEV;
		}
		/* yazmadan hemen önce taze okuma: döküm ile aynı olmalı (arada başka bir yazan yok) */
		if (kk_pmic_oku3(k, t))
			return -ENODEV;
		if (t[0] != o0 || t[1] != o1 || t[2] != o2) {
			KU("P1 PMIC taze okuma dokumden farkli (0x3d 0x%02x->0x%02x 0x3e 0x%02x->0x%02x 0x3f 0x%02x->0x%02x); yazilmadi -> DUR",
			   o0, t[0], o1, t[1], o2, t[2]);
			return -ENODEV;
		}
		KD("P1 PMIC 0x3e = 0x00 (taze okuma ayni); W1 izinli (kk_sdio_kapi.pmic_yaz=1): 0x00 | b0 -> 0x01");
		yr = kk_pmic_yaz_tek(k, false);
		if (k->pmic_yazma == n0)	/* sıra denetimi (IZINSIZ) yazmadı: geri okunacak/geri alınacak bir şey yok */
			return -ENODEV;
		/* G47: geri alma kararını YALNIZ P1'in kendi geri okuması verir (regmap dönüşü değil) */
		if (kk_pmic_oku3(k, s))
			return kk_pmic_geri_al(k, yr ? "yazma hatasi + geri okunamadi" : "geri okunamadi");
		KD("P1 PMIC geri okuma: 0x3e 0x%02x -> 0x%02x %s; 0x3d 0x%02x->0x%02x 0x3f 0x%02x->0x%02x", o1, s[1],
		   (s[1] == 0x01 && s[0] == o0 && s[2] == o2) ? "AYNI" : "FARKLI", o0, s[0], o2, s[2]);
		if (s[1] != 0x01 || s[0] != o0 || s[2] != o2)
			return kk_pmic_geri_al(k, yr ? "yazma hatasi + geri okuma beklenen degil" : "geri okuma beklenen degil");
		if (yr) {
			k->p1 = P1_HATA_TUTTU;
			KU("P1 yazma hata dondu (%d) ama geri okuma tuttu: 0x3e = 0x01 BIRAKILDI, geri alma YOK (G47 karari: geri alma yalniz geri okuma tutmazsa) -> DUR",
			   yr);
			return -ENODEV;
		}
		k->p1 = P1_TAMAM;
		msleep(50);	/* 32 kHz oturma (4.9 ~22 ms; tasarım ≥ 50 ms) */
		return 0;
	}
	KU("P1 PMIC 0x3e = 0x%02x (beklenen 0x01); PMIC yazma izni yok (kk_sdio_kapi.pmic_yaz=1 verilmedi) -> DUR, 0 yazma",
	   o1);
#else
	KU("P1 PMIC 0x3e = 0x%02x (beklenen 0x01); PMIC yazma yolu DERLENMEDI (CONFIG_KK_SDIO_KAPI_PMIC_YAZ=n) -> DUR, 0 yazma",
	   o1);
#endif
	return -ENODEV;
}

static int kk_crg_adimlari(struct kk_kapi *k)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(kk_sdio_crg); i++) {
		const struct kk_crg_adim *a = &kk_sdio_crg[i];
		u32 m = BIT(a->bit), o, g;

		o = kk_oku(k, KB_CRG, a->durum_off);
		if (B(o, a->bit) == a->hedef) {
			KD("%s: zaten hedefte (+0x%03x=0x%08x); yazma yok", a->ad, a->durum_off, o);
			continue;
		}
		if (kk_sdio_yaz(k, KB_CRG, a->yaz_off, a->yaz_val))
			return -ENODEV;
		g = kk_oku(k, KB_CRG, a->durum_off);
		KD("%s: durum +0x%03x 0x%08x -> 0x%08x (XOR 0x%08x, beklenen 0x%08x) %s", a->ad, a->durum_off, o, g,
		   o ^ g, m, ((o ^ g) == m && B(g, a->bit) == a->hedef) ? "AYNI" : "FARKLI");
		if ((o ^ g) != m || B(g, a->bit) != a->hedef) {
			KU("%s: geri okuma beklenen degil; geri alma denenmez, sonraki adimlar yapilmaz -> DUR", a->ad);
			return -ENODEV;
		}
	}
	return 0;
}

static int kk_gpio_adim(struct kk_kapi *k)
{
	u32 dir, o, g, gm;

	msleep(10);	/* 4.9 power_state_change: düşük 10 ms */
	dir = kk_oku(k, KB_GPIO3, GPIO_DIR);
	o = kk_oku(k, KB_GPIO3, GPIO_DATA_HEPSI);
	if (!B(dir, 6) || B(o, 6)) {
		KU("GPIO3_6: on kosul degisti (DIR=0x%02x DATA=0x%02x); yazilmadi -> DUR", dir & 0xff, o & 0xff);
		return -ENODEV;
	}
	if (kk_sdio_yaz(k, KB_GPIO3, GPIO_B6_MASKELI, WLAN_EN))
		return -ENODEV;
	gm = kk_oku(k, KB_GPIO3, GPIO_B6_MASKELI);
	g = kk_oku(k, KB_GPIO3, GPIO_DATA_HEPSI);
	/* XOR yalnız ÇIKIŞ pinlerinde değerlendirilir (DIR=1); giriş pinlerinin seviyesi dışarıdan değişebilir */
	KD("GPIO3_6 wlan_en: maskeli geri 0x%02x (beklenen 0x40); DATA 0x%02x -> 0x%02x (XOR 0x%02x; cikis pinlerinde 0x%02x, beklenen 0x40) %s",
	   gm & 0xff, o & 0xff, g & 0xff, (o ^ g) & 0xff, (o ^ g) & dir & 0xff,
	   (gm == WLAN_EN && ((o ^ g) & dir & 0xff) == WLAN_EN) ? "AYNI" : "FARKLI");
	if (gm != WLAN_EN || ((o ^ g) & dir & 0xff) != WLAN_EN) {
		KU("GPIO3_6 geri okuma beklenen degil; geri alma denenmez -> DUR");
		return -ENODEV;
	}
	msleep(30);	/* 4.9: yüksek sonrası 30 ms */
	return 0;
}

static int kk_pinmux_adim(struct kk_kapi *k)
{
	u32 v, g;
	int i;

	for (i = 0; i < 6; i++) {
		v = kk_oku(k, KB_IOMG_MMC1, i * 4);
		if (v == 1)
			continue;
		if (v != 0) {
			KU("X1 iomg-mmc1 +0x%02x = 0x%x (0 ya da 1 degil) -> DUR", i * 4, v);
			return -ENODEV;
		}
		if (kk_sdio_yaz(k, KB_IOMG_MMC1, i * 4, 1))
			return -ENODEV;
		g = kk_oku(k, KB_IOMG_MMC1, i * 4);
		KD("X1 iomg-mmc1 +0x%02x 0x%x <- 1; geri 0x%x %s", i * 4, v, g, g == 1 ? "AYNI" : "FARKLI");
		if (g != 1) {
			KU("X1 pinmux geri okuma beklenen degil; geri alma denenmez -> DUR");
			return -ENODEV;
		}
	}
	return 0;
}

/* Sonra dökümü: hedef bitler yerinde, dokunulmayanlar aynı. */
static int kk_sonra_denetle(struct kk_kapi *k, const struct kk_dokum *o, const struct kk_dokum *s)
{
	int i, kotu = 0;

#define KK_SART(c, fmt, ...) do { if (!(c)) { KU("SONRA DENETIMI: " fmt, ##__VA_ARGS__); kotu++; } } while (0)
	KK_SART(B(kk_crg_deger(s, CRG_CLKDIV19), 6) == 1, "CLKDIV19 b6 != 1");
	KK_SART(B(kk_crg_deger(s, CRG_PERSTAT0), 21) == 1, "PERSTAT0 b21 != 1");
	KK_SART(B(kk_crg_deger(s, CRG_PERSTAT4), 19) == 1, "PERSTAT4 b19 != 1");
	KK_SART(B(kk_crg_deger(s, CRG_PERRSTSTAT4), 20) == 0, "PERRSTSTAT4 b20 != 0");
	KK_SART(kk_crg_deger(s, CRG_CLKDIV6) == kk_crg_deger(o, CRG_CLKDIV6), "CLKDIV6 degisti");
	for (i = 0; i < 6; i++) {
		KK_SART(s->mx[i] == 1, "iomg-mmc1 +0x%02x = 0x%x", i * 4, s->mx[i]);
		KK_SART(s->cf[i] == o->cf[i], "iocg-mmc1 +0x%02x degisti 0x%x -> 0x%x", i * 4, o->cf[i], s->cf[i]);
	}
	KK_SART(s->iomg5c == o->iomg5c, "iomg+0x5c degisti");
	KK_SART(s->dir == o->dir, "gpio3 DIR degisti");
	KK_SART(s->ie == o->ie, "gpio3 IE degisti");
	KK_SART(B(s->dat, 6) == 1, "gpio3 DATA b6 != 1 (0x%02x)", s->dat);
	KK_SART(((s->dat ^ o->dat) & o->dir & ~WLAN_EN & 0xff) == 0, "gpio3 diger cikis pinleri degisti 0x%02x -> 0x%02x",
		o->dat, s->dat);
	KK_SART(s->pm[0] == o->pm[0] && s->pm[2] == o->pm[2], "PMIC 0x3d/0x3f degisti");
	KK_SART(s->pm[1] == 0x01, "PMIC 0x3e = 0x%02x", s->pm[1]);
#undef KK_SART
	if (kotu) {
		KU("sonra denetiminde %d sorun; dwmmc2 OLUSTURULMAZ -> DUR", kotu);
		return -ENODEV;
	}
	return 0;
}

/* ---- G47: SDIO kart denetimi (yalnız çekirdek yapıları okunur; donanım/kart erişimi YOK) ---- */
static void kk_kart_yaz(struct kk_kapi *k, const char *s)
{
	mutex_lock(&k->kilit);
	strscpy(k->kart, s, sizeof(k->kart));
	mutex_unlock(&k->kilit);
}

static int kk_host_esle(struct device *d, void *p)
{
	return d->class && d->class->name && !strcmp(d->class->name, "mmc_host");
}

static int kk_kart_esle(struct device *d, void *p)
{
	return d->bus && d->bus->name && !strcmp(d->bus->name, "mmc");
}

struct kk_fn_say {
	struct kk_kapi *k;
	int n, surucu;
#if IS_ENABLED(CONFIG_KK_HI1102_YUKLE)
	struct sdio_func *f1;
#endif
};

static int kk_fn_gez(struct device *d, void *p)
{
	struct kk_fn_say *s = p;
	struct kk_kapi *k = s->k;
	struct device_driver *drv;
	struct sdio_func *f;

	if (!d->bus || !d->bus->name || strcmp(d->bus->name, "sdio"))
		return 0;
	f = dev_to_sdio_func(d);
	drv = READ_ONCE(d->driver);
	s->n++;
	if (drv)
		s->surucu++;
#if IS_ENABLED(CONFIG_KK_HI1102_YUKLE)
	if (f->num == 1)
		s->f1 = f;
#endif
	KD("kart fonksiyonu %s: fn%u vendor 0x%04x device 0x%04x sinif 0x%02x; surucu %s", dev_name(d), f->num,
	   f->vendor, f->device, f->class, drv ? drv->name : "YOK (beklenen)");
	return 0;
}

static void kk_kart_is(struct work_struct *w)
{
	struct kk_kapi *k = container_of(to_delayed_work(w), struct kk_kapi, kart_is);
	struct device *hd, *cd = NULL;
	struct kk_fn_say s = { .k = k };
	struct mmc_host *h = NULL;
	struct mmc_card *c;
	char m[200];
	bool hi;

	k->kart_tur++;
	hd = device_find_child(&k->mmc_pdev->dev, NULL, kk_host_esle);
	if (hd) {
		h = container_of(hd, struct mmc_host, class_dev);
		cd = device_find_child(hd, NULL, kk_kart_esle);
	}
	if (!cd) {
		if (k->kart_tur < KK_KART_TUR) {
			put_device(hd);
			schedule_delayed_work(&k->kart_is, HZ);
			return;
		}
		if (h)
			snprintf(m, sizeof(m), "GORULMEDI (%d sn): %s var, kart yok; ios saat %u Hz genislik %u bit; filtre reddi %u",
				 k->kart_tur, dev_name(hd), h->ios.clock, 1u << h->ios.bus_width, h->kk_blocked);
		else
			snprintf(m, sizeof(m), "GORULMEDI (%d sn): dwmmc2'de mmc host yok (dw_mmc baglanmadi)", k->kart_tur);
		KU("SDIO KART %s (CMD5 yanitsizsa mmc cekirdegi ayrica satir basmaz; CMD5 sonrasi hata 'whilst initialising SDIO card' basar)",
		   m);
		kk_kart_yaz(k, m);
#if IS_ENABLED(CONFIG_KK_HI1102_YUKLE)
		mutex_lock(&k->kilit);
		k->h_bitti = true;
		mutex_unlock(&k->kilit);
#endif
		put_device(hd);
		return;
	}
	if (!k->kart_ilk) {	/* fonksiyon aygıtları kart eklendikten hemen sonra eklenir: bir tur daha bekle */
		k->kart_ilk = k->kart_tur;
		put_device(cd);
		put_device(hd);
		schedule_delayed_work(&k->kart_is, HZ);
		return;
	}
	c = container_of(cd, struct mmc_card, dev);
	hi = c->type == MMC_TYPE_SDIO && c->cis.vendor == KK_SDIO_VENDOR && c->cis.device == KK_SDIO_DEVICE;
	device_for_each_child(cd, &s, kk_fn_gez);
	snprintf(m, sizeof(m), "GORULDU (%d sn): %s tip %u CIS 0x%04x/0x%04x %s; fonksiyon %u/%d; surucu %d; ios %u Hz %u bit; filtre reddi %u",
		 k->kart_ilk, dev_name(cd), c->type, c->cis.vendor, c->cis.device, hi ? "BEKLENEN" : "BEKLENMEYEN",
		 c->sdio_funcs, s.n, s.surucu, h->ios.clock, 1u << h->ios.bus_width, h->kk_blocked);
	KD("SDIO KART %s", m);
	KD("SDIO kart: %s; fonksiyon surucusu %s; firmware yukleme ve ag arayuzu yok (M2 yok)",
	   hi ? "vendor 0x0296 device 0x5347 (Hi1102) GORULDU" : "kimlik BEKLENMEYEN (Hi1102 0x0296/0x5347 degil)",
	   s.surucu ? "BAGLI (BEKLENMEYEN)" : "bagli degil (beklenen)");
	kk_kart_yaz(k, m);
#if IS_ENABLED(CONFIG_KK_HI1102_YUKLE)
	mutex_lock(&k->kilit);
	if (hi && !s.surucu && s.n == 1 && c->sdio_funcs == 1 && s.f1) {
		get_device(&s.f1->dev);
		k->h_fn = s.f1;
	}
	k->h_bitti = true;
	mutex_unlock(&k->kilit);
	KD("G48: kart %s", k->h_fn ? "Hi1102 yukleyicisine hazir (fn1 tutuldu; erisim yok)" : "yukleyiciye VERILMEDI (kimlik/surucu/fonksiyon)");
#endif
	put_device(cd);
	put_device(hd);
}

#if IS_ENABLED(CONFIG_KK_HI1102_YUKLE)
/* G48: yükleyiciye kart bilgisi (yalnız çekirdek yapıları; karta/yazmaca erişim yok). kk-hi1102-yukle.h'ye bakınız. */
bool kk_sdio_kapi_hazir(struct device *dev)
{
	struct kk_kapi *k = dev_get_drvdata(dev);

	return k && k->mmc_pdev;
}

int kk_sdio_kapi_kart(struct device *dev, struct kk_h_kart *o)
{
	struct kk_kapi *k = dev_get_drvdata(dev);
	struct mmc_card *c;
	struct mmc_host *h;
	int r = 0;

	if (!k || !k->mmc_pdev)
		return -EPERM;
	mutex_lock(&k->kilit);
	if (!k->h_bitti) {
		r = -EAGAIN;
	} else if (!k->h_fn) {
		r = -ENODEV;
	} else {
		c = k->h_fn->card;
		h = c->host;
		memset(o, 0, sizeof(*o));
		get_device(&k->h_fn->dev);
		o->func = k->h_fn;
		o->tip = c->type;
		o->vendor = c->cis.vendor;
		o->device = c->cis.device;
		o->fonksiyon = c->sdio_funcs;
		o->fn_no = k->h_fn->num;
		o->fn_vendor = k->h_fn->vendor;
		o->fn_device = k->h_fn->device;
		o->surucu = READ_ONCE(k->h_fn->dev.driver) != NULL;
		o->saat = h->ios.clock;
		o->genislik_bit = 1u << h->ios.bus_width;
		o->filtre_reddi = h->kk_blocked;
	}
	mutex_unlock(&k->kilit);
	return r;
}
#endif

/* ---- tetikte çalışan dizi ---- */
static void kk_dizi(struct kk_kapi *k)
{
	struct platform_device_info pi = {};
	struct kk_dokum once, sonra;
	int r;

	KD("TETIK kabul edildi (acilis basina 1; PMIC yazma yolu %s)", KK_PMIC_DURUM);
	if (!k->acilis_ok) {
		KU("acilis okumalari/on kosullari tutmamisti; dizi baslatilmadi -> DUR (hicbir erisim yok)");
		k->sonuc = "DUR: acilis okumalari tutmamisti";
		goto dur;
	}
	k->sonuc = "calisiyor";
	r = kk_dokum_al(k, &once, "once", true);
	if (r)
		goto dur;
	r = kk_on_kosul(k, &once);
	if (r)
		goto dur;
	r = kk_pmic_adim(k, &once);
	if (r)
		goto dur;
	r = kk_crg_adimlari(k);
	if (r)
		goto dur;
	r = kk_gpio_adim(k);
	if (r)
		goto dur;
	r = kk_pinmux_adim(k);
	if (r)
		goto dur;
	r = kk_dokum_al(k, &sonra, "sonra", false);
	if (r)
		goto dur;
	r = kk_sonra_denetle(k, &once, &sonra);
	if (r)
		goto dur;
	KD("hazir: %d MMIO yazma, %d PMIC yazma; hepsinin geri okumasi beklenen", k->yazma, k->pmic_yazma);

	pi.parent = k->dev;
	pi.fwnode = of_fwnode_handle(k->mmc_np);
	pi.name = "dwmmc_k3";
	pi.id = PLATFORM_DEVID_NONE;
	pi.res = k->mmc_res;
	pi.num_res = 2;
	pi.dma_mask = DMA_BIT_MASK(32);
	k->mmc_pdev = platform_device_register_full(&pi);
	if (IS_ERR(k->mmc_pdev)) {
		KU("dwmmc2 aygiti olusturulamadi (%ld)", PTR_ERR(k->mmc_pdev));
		k->mmc_pdev = NULL;
		goto dur;
	}
	of_node_put(k->mmc_np);	/* aygıt kendi referansını aldı */
	k->mmc_np = NULL;
	KD("dwmmc2 (%pR, irq %llu) dwmmc_k3 aygiti olusturuldu; SDIO kart denetimi en cok %d sn", &k->mmc_res[0],
	   (unsigned long long)k->mmc_res[1].start, KK_KART_TUR);
	k->sonuc = "HAZIR: dwmmc2 olusturuldu";
	kk_kart_yaz(k, "bekleniyor");
	schedule_delayed_work(&k->kart_is, HZ);
	return;

dur:
#if IS_ENABLED(CONFIG_KK_SDIO_KAPI_PMIC_YAZ)
	switch (k->p1) {
	case P1_TAMAM:
		KU("P1 sonrasi bir adimda DUR: PMIC 0x3e = 0x01 BIRAKILDI (G47 karari: geri alma yalniz P1'in kendi geri okumasi tutmazsa; reboot sifirlar)");
		break;
	case P1_HATA_TUTTU:
		KU("P1 DUR: yazma hata dondu ama geri okuma tuttu; PMIC 0x3e = 0x01 BIRAKILDI (geri alma yok)");
		break;
	case P1_GERI_ALINDI:
		KU("P1 DUR: 2 PMIC yazmasinin ikincisi W1 GERI ALMA (0x3e <- 0x00); P1 geri okumasi tutmamisti");
		break;
	case P1_GERI_GEREKMEDI:
		KU("P1 DUR: geri okuma tutmadi ama 0x3e = 0x00 okundu; geri alma yazmasi yapilmadi");
		break;
	case P1_GERI_HATA:
		KU("P1 DUR: geri alma yazmasi hata dondu; 0x3e durumu bilinmiyor (reboot sifirlar)");
		break;
	default:
		break;
	}
#endif
	of_node_put(k->mmc_np);
	k->mmc_np = NULL;
	if (!k->sonuc || !strcmp(k->sonuc, "calisiyor"))
		k->sonuc = "DUR";
	KU("DUR: %d MMIO yazma, %d PMIC yazma yapildi; MMIO geri alma yok; dwmmc2 olusturulmadi", k->yazma, k->pmic_yazma);
}

static ssize_t baslat_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t n)
{
	struct kk_kapi *k = dev_get_drvdata(dev);

	if (!k)
		return -ENODEV;
	if (!sysfs_streq(buf, "1")) {
		KU("tetik: gecersiz deger (yalniz '1' kabul edilir); hicbir erisim yapilmadi");
		return -EINVAL;
	}
	if (test_and_set_bit(0, &kk_tetik_bit)) {
		k->tetik_red++;
		KU("TETIK REDDEDILDI (red #%d): bu acilista zaten tetiklendi (acilis basina 1); hicbir erisim yapilmadi",
		   k->tetik_red);
		return -EBUSY;
	}
	kk_dizi(k);
	return n;
}
static DEVICE_ATTR_WO(baslat);

/* Yalnız bellekteki alanlar; donanıma/PMIC'e erişim YOK (kural 15a: yan etkisiz okuma işleyicisi). */
static ssize_t durum_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct kk_kapi *k = dev_get_drvdata(dev);
	ssize_t n;

	if (!k)
		return -ENODEV;
	mutex_lock(&k->kilit);
	n = sysfs_emit(buf, "acilis=%s tetik=%d red=%d sonuc='%s' mmio=%d pmic=%d p1=%s acilis_0x3e=0x%02x kart='%s'\n",
		       k->acilis_ok ? "tamam" : "TUTMADI", test_bit(0, &kk_tetik_bit) ? 1 : 0, k->tetik_red,
		       k->sonuc ? k->sonuc : "-", k->yazma, k->pmic_yazma, kk_p1_ad[k->p1], k->acilis.pm[1],
		       k->kart[0] ? k->kart : "-");
	mutex_unlock(&k->kilit);
	return n;
}
static DEVICE_ATTR_RO(durum);

static struct attribute *kk_sdio_kapi_attrs[] = {
	&dev_attr_baslat.attr,
	&dev_attr_durum.attr,
	NULL,
};
ATTRIBUTE_GROUPS(kk_sdio_kapi);

static int kk_pmic_al(struct kk_kapi *k)
{
	struct device_node *np = of_parse_phandle(k->dev->of_node, "karakutu,pmic", 0);
	struct device_node *ust;
	struct spmi_device *sd;
	bool spmi_ust;

	if (!np) {
		KU("karakutu,pmic yok -> DUR (hicbir erisim yapilmadi)");
		return -ENODEV;
	}
	sd = spmi_find_device_by_of_node(np);
	if (sd) {
		k->pmic = dev_get_regmap(&sd->dev, NULL);
		put_device(&sd->dev);
		of_node_put(np);
		if (!k->pmic) {
			dev_info(k->dev, "kk: sdio kapi: PMIC regmap'i (kk-spmi-syscon) henuz yok; ertelendi\n");
			return -EPROBE_DEFER;
		}
		return 0;
	}
#if IS_ENABLED(CONFIG_KK_SDIO_KAPI_SAHTE)
	if (of_device_is_compatible(np, "karakutu,sahte-pmic")) {
		k->pmic = syscon_node_to_regmap(np);
		of_node_put(np);
		if (IS_ERR(k->pmic)) {
			k->pmic = NULL;
			KU("SAHTE PMIC regmap'i kurulamadi -> DUR");
			return -ENODEV;
		}
		KU("SAHTE PMIC (RAM syscon, YALNIZ QEMU; CONFIG_KK_SDIO_KAPI_SAHTE)");
		return 0;
	}
#endif
	ust = of_get_parent(np);
	spmi_ust = ust && of_device_is_compatible(ust, "hisilicon,kirin970-spmi-controller");
	of_node_put(ust);
	if (spmi_ust) {
		of_node_put(np);
		dev_info(k->dev, "kk: sdio kapi: SPMI denetleyicisi PMIC aygitini henuz eklemedi; ertelendi\n");
		return -EPROBE_DEFER;
	}
	KU("karakutu,pmic (%pOF) bir SPMI aygiti degil -> DUR (hicbir erisim yapilmadi)", np);
	of_node_put(np);
	return -ENODEV;
}

static int kk_dt_al(struct kk_kapi *k)
{
	struct device_node *np = k->dev->of_node;
	int i;

	k->mmc_np = of_parse_phandle(np, "karakutu,mmc", 0);
	if (!k->mmc_np || !of_device_is_compatible(k->mmc_np, "hisilicon,hi3660-dw-mshc") ||
	    of_address_to_resource(k->mmc_np, 0, &k->mmc_res[0]) || of_irq_to_resource(k->mmc_np, 0, &k->mmc_res[1]) <= 0) {
		KU("karakutu,mmc (hi3660-dw-mshc, reg, irq) okunamadi -> DUR (hicbir erisim yapilmadi)");
		return -ENODEV;
	}
	/* Her blok ayrı reg girdisi; bölge TALEP EDİLMEZ (CRG başka sürücülerin de). Eşleme erişim değildir. */
	for (i = 0; i < KB_SAYI; i++) {
		struct resource r;
		int idx = of_property_match_string(np, "reg-names", kk_blok_ad[i]);

		if (idx < 0 || of_address_to_resource(np, idx, &r) || resource_size(&r) < kk_blok_min[i]) {
			KU("reg '%s' yok ya da kucuk (en az 0x%x) -> DUR", kk_blok_ad[i], kk_blok_min[i]);
			return -ENODEV;
		}
		k->fiz[i] = r.start;
		k->b[i] = devm_ioremap(k->dev, r.start, resource_size(&r));
		if (!k->b[i])
			return -ENOMEM;
	}
	return 0;
}

/* Açılış: YALNIZ OKUMA (G46 Faz A okumaları). Yazma dizisi baslat tetiğine kadar çalışmaz. */
static int kk_sdio_kapi_probe(struct platform_device *pdev)
{
	struct kk_kapi *k;
	int r;

	if (!kk_ac) {
		dev_info(&pdev->dev, "kk: sdio kapi: kapali (kk_sdio_kapi.ac=1 yok); hicbir MMIO/SPMI erisimi yok\n");
		return -ENODEV;
	}
	if (kk_sdio_calisti) {
		dev_warn(&pdev->dev, "kk: sdio kapi: bu acilista zaten calisti; tekrar yok\n");
		return -ENODEV;
	}
	k = devm_kzalloc(&pdev->dev, sizeof(*k), GFP_KERNEL);
	if (!k)
		return -ENOMEM;
	k->dev = &pdev->dev;
	mutex_init(&k->kilit);
	INIT_DELAYED_WORK(&k->kart_is, kk_kart_is);

	r = kk_pmic_al(k);
	if (r)
		return r == -EPROBE_DEFER ? r : -ENODEV;
	r = kk_dt_al(k);
	if (r) {
		of_node_put(k->mmc_np);
		KU("DUR: 0 MMIO yazma, 0 PMIC yazma yapildi; dwmmc2 olusturulmadi");
		return -ENODEV;
	}

	kk_sdio_calisti = true;	/* buradan sonra erişim başlar; erteleme yok */
	KD("basladi (ac=1; PMIC yazma yolu %s; MMIO izin tablosu %zu girdi); G47: acilista YALNIZ OKUMA, dizi %s/baslat tetigini bekler (acilis basina 1)",
	   KK_PMIC_DURUM, ARRAY_SIZE(kk_sdio_izin), dev_name(k->dev));
	r = kk_dokum_al(k, &k->acilis, "acilis", true);
	if (!r)
		r = kk_on_kosul(k, &k->acilis);
	k->acilis_ok = !r;
	if (k->acilis_ok)
		KD("acilis P1 on bakis: %s", kk_pmic_onbakis(&k->acilis));
	k->sonuc = k->acilis_ok ? "tetik bekleniyor" : "acilis okumalari tutmadi (tetik DUR der)";
	platform_set_drvdata(pdev, k);
	KD("acilis okumalari %s: 0 MMIO yazma, 0 PMIC yazma; dizi tetik bekliyor", k->acilis_ok ? "tamam" : "TUTMADI");
	kk_hi1102_ekle(k->dev);	/* G48: yalnız sysfs (kk_hi1102_yukle.ac=1 ise); erişim yok */
	return 0;
}

static const struct of_device_id kk_sdio_kapi_of[] = {
	{ .compatible = "karakutu,kirin710-sdio-kapi" },
	{ }
};
MODULE_DEVICE_TABLE(of, kk_sdio_kapi_of);

static struct platform_driver kk_sdio_kapi_drv = {
	.probe = kk_sdio_kapi_probe,
	.driver = {
		.name = "kk-sdio-kapi",
		.of_match_table = kk_sdio_kapi_of,
		.dev_groups = kk_sdio_kapi_groups,
		.suppress_bind_attrs = true,	/* sysfs bind/unbind ile yeniden çalıştırma yok */
	},
};
module_platform_driver(kk_sdio_kapi_drv);
MODULE_DESCRIPTION("Kara Kutu: dwmmc2/Hi1102 SDIO kapisi (acilista salt okuma; sysfs tetigiyle kosullu yazma + geri okuma)");
MODULE_LICENSE("GPL");
