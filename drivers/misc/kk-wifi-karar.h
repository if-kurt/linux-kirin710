/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Kara Kutu G52 M3-0.1: kk-wifi (Hi1102 port iskeleti) SAF kararları — kernel ve birim testi aynı dosyayı derler.
 *   kk_wifi_tetik_coz : sysfs `baslat` girdisi; yalnız "1" ya da "1\n" kabul (G47/M2 tetik deseni).
 *   kk_wifi_kimlik    : probe'da bağlanma kararı (G/Ç yok; yalnız sdio_func/mmc_card alanları).
 */
#ifndef KK_WIFI_KARAR_H
#define KK_WIFI_KARAR_H

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/errno.h>
#else
#include <stddef.h>
#include <stdbool.h>
#include <errno.h>
#endif

#define KK_WIFI_VENDOR	0x0296	/* Hi1102 CIS (6.12'de SDIO_VENDOR_ID_MICROCHIP_WILC ile çakışır; WILC1000 kapalı) */
#define KK_WIFI_DEVICE	0x5347

enum kk_wifi_red {
	KK_WIFI_TAMAM = 0,
	KK_WIFI_RED_KIMLIK,		/* vendor/device beklenen değil */
	KK_WIFI_RED_FONKSIYON,		/* fn1 değil */
	KK_WIFI_RED_FONK_SAYI,		/* kartta fonksiyon sayısı 1 değil (Faz B: 1/1) */
	KK_WIFI_RED_HOST,		/* host SDIO-yalnız değil (karakutu,sdio-yalniz yok) -> eMMC host'u asla */
	KK_WIFI_RED_IKINCI,		/* bu açılışta zaten bir fonksiyona bağlandı */
	KK_WIFI_RED_SAYI
};

/* 0 = tetik; -EINVAL = geçersiz girdi */
static inline int kk_wifi_tetik_coz(const char *b, size_t n)
{
	if (n == 1 && b[0] == '1')
		return 0;
	if (n == 2 && b[0] == '1' && b[1] == '\n')
		return 0;
	return -EINVAL;
}

static inline enum kk_wifi_red kk_wifi_kimlik(unsigned int vendor, unsigned int device, unsigned int fnum,
					      unsigned int kart_fonk, bool sdio_yalniz_host, bool onceden_bagli)
{
	if (onceden_bagli)
		return KK_WIFI_RED_IKINCI;
	if (!sdio_yalniz_host)
		return KK_WIFI_RED_HOST;
	if (vendor != KK_WIFI_VENDOR || device != KK_WIFI_DEVICE)
		return KK_WIFI_RED_KIMLIK;
	if (fnum != 1)
		return KK_WIFI_RED_FONKSIYON;
	if (kart_fonk != 1)
		return KK_WIFI_RED_FONK_SAYI;
	return KK_WIFI_TAMAM;
}

#endif /* KK_WIFI_KARAR_H */
