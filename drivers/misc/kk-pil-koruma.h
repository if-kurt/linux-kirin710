/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Kara Kutu G54 (G-6): düşük pil koruması — pil kaynağı arayüzü.
 * Cihazda kaynak kk_sensor_pil() (coul V/I saf SPMI okuması). Yalnız QEMU'da kk-sarj-sahte kendi sahte pilini
 * kk_pil_koruma_kaynak() ile kaydeder (cihaz imajında KK_SARJ_SAHTE kapalı; çağrı derlenmez).
 */
#ifndef KK_PIL_KORUMA_H
#define KK_PIL_KORUMA_H

#include <linux/types.h>

typedef int (*kk_pil_kaynak_fn)(void *ctx, int *vbat_mv, int *ibat_ma, int *pil_mc, bool *sic_gecerli);

#if IS_ENABLED(CONFIG_KK_PIL_KORUMA)
void kk_pil_koruma_kaynak(kk_pil_kaynak_fn fn, void *ctx, const char *ad);
#else
static inline void kk_pil_koruma_kaynak(kk_pil_kaynak_fn fn, void *ctx, const char *ad) { }
#endif

#endif
