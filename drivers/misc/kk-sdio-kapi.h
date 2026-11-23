/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Kara Kutu G45 (Wi-Fi M1): kk-sdio-kapi ile YALNIZ QEMU sahte yazmaç modeli (kk-sdio-sahte.c) arasındaki arayüz.
 * CONFIG_KK_SDIO_KAPI_SAHTE kapalıyken (her cihaz imajı) kancalar boş satır içi işlevdir: kod üretmez.
 */
#ifndef KK_SDIO_KAPI_H
#define KK_SDIO_KAPI_H

#include <linux/types.h>

/* Kapının eşlediği bloklar (DT reg-names sırası) */
enum { KB_CRG, KB_IOMG_MMC1, KB_IOCG_MMC1, KB_IOMG, KB_GPIO3, KB_SAYI };

#if IS_ENABLED(CONFIG_KK_SDIO_KAPI_SAHTE)
/* Yazmadan hemen önce/sonra çağrılır; yalnız sahte RAM bloklarında donanım davranışını taklit eder. */
void kk_sdio_sahte_once(void __iomem *taban, unsigned int blok, u32 off);
void kk_sdio_sahte_yansit(void __iomem *taban, unsigned int blok, u32 off, u32 val);
#else
static inline void kk_sdio_sahte_once(void __iomem *taban, unsigned int blok, u32 off) { }
static inline void kk_sdio_sahte_yansit(void __iomem *taban, unsigned int blok, u32 off, u32 val) { }
#endif

/*
 * G46 W1 (yalnız QEMU, SAHTE + PMIC_YAZ birlikte): sahte PMIC hata enjeksiyonu kancaları. Sahte PMIC RAM'ine
 * YAZMAZLAR; yalnız kapının gördüğü regmap dönüş kodunu (yazdi) ya da okunan değeri/okuma dönüşünü (okundu; G47:
 * dönüş kodu da) değiştirebilirler.
 * Başka her yapılandırmada (her cihaz imajı dahil) boş satır içi işlevdir: kod üretmez.
 */
#if IS_ENABLED(CONFIG_KK_SDIO_KAPI_SAHTE) && IS_ENABLED(CONFIG_KK_SDIO_KAPI_PMIC_YAZ)
int kk_sdio_sahte_pmic_yazdi(unsigned int reg, unsigned int val, int r);
int kk_sdio_sahte_pmic_okundu(unsigned int reg, unsigned int *v, int r);
#else
static inline int kk_sdio_sahte_pmic_yazdi(unsigned int reg, unsigned int val, int r) { return r; }
static inline int kk_sdio_sahte_pmic_okundu(unsigned int reg, unsigned int *v, int r) { return r; }
#endif

#endif
