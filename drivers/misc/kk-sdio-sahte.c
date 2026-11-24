// SPDX-License-Identifier: GPL-2.0
/*
 * Kara Kutu G45 (Wi-Fi M1): kk-sdio-kapi için SAHTE yazmaç modeli. YALNIZ QEMU (CONFIG_KK_SDIO_KAPI_SAHTE).
 * Cihaz imajında derlenmez. Kapı, QEMU'da sahte RAM bloklarına (DT reg) yazar; RAM, CRG/PL061 davranışını
 * kendiliğinden göstermez. Bu model her izinli yazmadan sonra RAM'deki durum yazmaçlarını donanım gibi günceller:
 *   PEREN0/PEREN4 -> PERCLKENx ve PERSTATx |= değer; PERRSTDIS4 -> PERRSTSTAT4 &= ~değer;
 *   CLKDIV19 hiword (maske [31:16]) -> alt 16 bit; PL061 maskeli DATA -> +0x3fc güncellenir.
 * Hata enjeksiyonu (cmdline kk_sdio_sahte.bozuk=):
 *   c1   PERSTAT0 güncellenmez            (C1 geri okuması tutmaz)
 *   c2   CLKDIV19 hiword çözülmez         (RAM'de 0x00400040 kalır; C2 geri okuması tutmaz)
 *   c4   PERSTAT4'te hedef bitle birlikte b0 da değişir (XOR yalnız hedef bit değil)
 *   c5   PERRSTSTAT4 güncellenmez
 *   gpio +0x3fc güncellenmez               (GPIO geri okuması tutmaz)
 *   x1   iomg-mmc1 +0x0c yazması tutmaz (0 kalır)
 * G62-3 (CONFIG_KK_SDIO_KAPI_SDIO25; CLKDIV6 0xc0 de hiword çözülür):
 *   s1   CLKDIV19 b8 yazması (0x01000100) tutmaz           (S1 geri okuması tutmaz -> geri alma)
 *   s2   CLKDIV6 b6 yazması (0x00400040) tutmaz            (S2 geri okuması tutmaz -> S2+S1 geri alma)
 *   s3   CLKDIV6 b6 yazılırken b4 (PLL seçimi) de değişir  (komşu bit; S2 geri okuması tutmaz)
 *   s4   CLKDIV19 b8 yazılırken b9 da değişir              (komşu bit; S1 geri okuması tutmaz)
 *   s5   geri alma yazmaları (0x01000000 / 0x00400000) tutmaz (tek başına etkisiz: yalnız geri alma yoluna girilince görünür)
 *   s6   s2 + s5 birlikte: S2 yazması tutmaz VE geri almalar tutmaz (geri okuma FARKLI; yine DUR)
 * G46 W1 (yalnız CONFIG_KK_SDIO_KAPI_PMIC_YAZ=y QEMU çekirdeğinde) sahte PMIC hata enjeksiyonu. Bu kancalar sahte
 * PMIC RAM'ine YAZMAZ (RAM'i yalnız kapının tek regmap_write'ı değiştirir); kapının gördüğünü değiştirir:
 *   pmichata  W1 yazması (0x3e <- 0x01) RAM'e yapılır ama regmap -EIO dönmüş gibi görünür
 *   pmicgeri  0x01 yazıldıktan sonra (0x00 geri yazılana dek) 0x3e, RAM'de 0x01 iken 0x03 okunur
 *   pmic3f    0x01 yazıldıktan sonra (0x00 geri yazılana dek) 0x3f'in b0'ı ters okunur (komşu değişmiş gibi)
 *   pmiconce  0x3e'nin taze okuması (yazmadan hemen önce) 0x00 yerine 0x02 okunur. G47: 0x3e okuma sırası
 *             açılış dökümü (1), tetik önce dökümü (2), taze okuma (3) -> 3. okuma
 * G47 ekleri (geri alma kuralı: yalnız P1'in kendi geri okuması tutmazsa):
 *   pmicokuma 0x01 yazıldıktan sonra 0x3e'nin İLK okuması -EIO döner (geri okunamadı -> geri alma)
 *   pmichata0 W1 yazması -EIO döner VE 0x3e (0x00 geri yazılana dek) 0x00 okunur (yazma tutmamış gibi; RAM'de 0x01
 *             kalır: yalnız kapının kararını sınar -> geri okuma tutmaz, 0x3e 0x00 -> geri alma yazması gerekmez)
 */
#include <linux/io.h>
#include <linux/module.h>
#include <linux/string.h>
#include "kk-sdio-kapi.h"

static char *bozuk = "";
module_param(bozuk, charp, 0444);
MODULE_PARM_DESC(bozuk, "hata enjeksiyonu: c1|c2|c4|c5|gpio|x1|s1..s6 (G62-3) (+ W1: pmichata|pmicgeri|pmic3f|pmiconce; G47: pmicokuma|pmichata0)");

static u32 kk_once;	/* yazmadan önceki değer (CLKDIV19 / PL061 DATA) */

static bool kk_bozuk(const char *ad)
{
	return bozuk && !strcmp(bozuk, ad);
}

void kk_sdio_sahte_once(void __iomem *b, unsigned int blok, u32 off)
{
	if (blok == KB_CRG && (off == 0x0f4 || off == 0x0c0))
		kk_once = readl(b + off) & 0xffff;
	else if (blok == KB_GPIO3)
		kk_once = readl(b + 0x3fc) & 0xff;
}

void kk_sdio_sahte_yansit(void __iomem *b, unsigned int blok, u32 off, u32 val)
{
	u32 m, v;

	switch (blok) {
	case KB_CRG:
		switch (off) {
		case 0x000:	/* PEREN0 */
			writel(readl(b + 0x008) | val, b + 0x008);
			if (!kk_bozuk("c1"))
				writel(readl(b + 0x00c) | val, b + 0x00c);
			break;
		case 0x040:	/* PEREN4 */
			writel(readl(b + 0x048) | val, b + 0x048);
			v = readl(b + 0x04c) | val;
			if (kk_bozuk("c4"))
				v ^= BIT(0);
			writel(v, b + 0x04c);
			break;
		case 0x094:	/* PERRSTDIS4 */
			if (!kk_bozuk("c5"))
				writel(readl(b + 0x098) & ~val, b + 0x098);
			break;
		case 0x0f4:	/* CLKDIV19, hiword maske */
			if (kk_bozuk("c2"))
				break;
			if (kk_bozuk("s1") && val == 0x01000100)
				break;
			if ((kk_bozuk("s5") || kk_bozuk("s6")) && val == 0x01000000)
				break;
			m = val >> 16;
			v = ((kk_once & ~m) | (val & m)) & 0xffff;
			if (kk_bozuk("s4") && val == 0x01000100)
				v ^= BIT(9);
			writel(v, b + 0x0f4);
			break;
		case 0x0c0:	/* CLKDIV6, hiword maske (G62-3) */
			if ((kk_bozuk("s2") || kk_bozuk("s6")) && val == 0x00400040)
				break;
			if ((kk_bozuk("s5") || kk_bozuk("s6")) && val == 0x00400000)
				break;
			m = val >> 16;
			v = ((kk_once & ~m) | (val & m)) & 0xffff;
			if (kk_bozuk("s3") && val == 0x00400040)
				v ^= BIT(4);
			writel(v, b + 0x0c0);
			break;
		}
		break;
	case KB_GPIO3:
		if (kk_bozuk("gpio"))
			break;
		m = (off >> 2) & 0xff;
		v = (kk_once & ~m) | (val & m);
		writel(v & 0xff, b + 0x3fc);
		writel(v & m, b + off);
		break;
	case KB_IOMG_MMC1:
		if (kk_bozuk("x1") && off == 0x0c)
			writel(0, b + off);
		break;
	}
	pr_info("kk: sdio sahte: blok %u +0x%03x <- 0x%08x yansitildi (bozuk='%s')\n", blok, off, val,
		bozuk ? bozuk : "");
}

#if IS_ENABLED(CONFIG_KK_SDIO_KAPI_PMIC_YAZ)
static int kk_son_pmic = -1;	/* kapının son PMIC yazma değeri (-1 = yazma yok) */
static int kk_3e_okuma;

int kk_sdio_sahte_pmic_yazdi(unsigned int reg, unsigned int val, int r)
{
	kk_son_pmic = val;
	pr_info("kk: sdio sahte: pmic 0x%02x <- 0x%02x gordu (regmap %d; bozuk='%s')\n", reg, val, r, bozuk ? bozuk : "");
	if ((kk_bozuk("pmichata") || kk_bozuk("pmichata0")) && val == 0x01)
		return -EIO;
	return r;
}

int kk_sdio_sahte_pmic_okundu(unsigned int reg, unsigned int *v, int r)
{
	static bool okuma_hatasi_verildi;
	unsigned int ilk = *v;

	if (r)
		return r;
	if (reg == 0x3e && ++kk_3e_okuma == 3 && kk_son_pmic < 0 && kk_bozuk("pmiconce") && *v == 0x00)
		*v = 0x02;
	if (kk_son_pmic == 0x01) {
		if (kk_bozuk("pmicgeri") && reg == 0x3e && *v == 0x01)
			*v = 0x03;
		if (kk_bozuk("pmic3f") && reg == 0x3f)
			*v ^= 0x01;
		if (kk_bozuk("pmichata0") && reg == 0x3e)
			*v = 0x00;
		if (kk_bozuk("pmicokuma") && reg == 0x3e && !okuma_hatasi_verildi) {
			okuma_hatasi_verildi = true;
			pr_info("kk: sdio sahte: pmic 0x3e okumasi -EIO gosterildi (bozuk='%s')\n", bozuk);
			return -EIO;
		}
	}
	if (*v != ilk)
		pr_info("kk: sdio sahte: pmic 0x%02x okumasi 0x%02x -> 0x%02x gosterildi (bozuk='%s')\n", reg, ilk, *v,
			bozuk ? bozuk : "");
	return 0;
}
#endif
