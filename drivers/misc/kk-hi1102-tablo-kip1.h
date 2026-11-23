/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Kara Kutu G49 (Wi-Fi M2b): kk-hi1102-yukle KİP 1 (el sıkışma) tablosu. YALNIZ CONFIG_KK_HI1102_YALNIZ_KIP1=y.
 * İki satır, üretilmiş tam tablonun (kk-hi1102-tablo.h; tools/tablo-uret.py) ilk iki satırıyla BAYT BAYT aynıdır;
 * build.sh her derlemede tabloyu yeniden üretir ve bu iki satırı ve KK_H_CFG_SHA256/KK_H_SURUM_ONEK satırlarını
 * karşılaştırır (fark -> derleme durur). SETPM/WRITEM/FILES/CALI/QUIT satırları ve parça tabloları bu derlemede YOK.
 */
#ifndef KK_HI1102_TABLO_KIP1_H
#define KK_HI1102_TABLO_KIP1_H
#define KK_H_CFG_SHA256 "3f5cbabe6309ef62c0c3d437573106027364225edd1b81a222dd56c635b79a96"
#define KK_H_SURUM_ONEK "Hi1102V100"
static const struct kk_h_adim kk_h_tablo[] = {
	{ KK_HT_READM_EC, 255, 1, KK_HB_VERI4, "READM 0x50000770 4 ", 19, 0x50000770, 0x4, 0, "plat_efuse.c read_efuse_ec_version (GET_EFUSE_EC_VERSION)" },
	{ KK_HT_VERSION, 255, 1, KK_HB_SURUM, "VERSION ", 8, 0x00000000, 0x0, 0, "PARA_VERSION=Hi1102V100;" },
};
#define KK_H_ADIM_SAYI 2
#endif
