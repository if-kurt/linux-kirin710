/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Kara Kutu G61-4: paket/olay basina kmsg satirlarinin hiz siniri (yalniz Wi-Fi kk_m3b/kk_m3c).
 * Sinif basina: ilk KK_LOG_ILK satir her zaman; sonra en cok aralik_s saniyede 1 satir (o satir "ozet": bastirilan sayisini tasir).
 * Bastirilan satirlar yalniz sayilir (sinif ve toplam). Sayaclar zaten ayri tutuldugu icin bilgi kaybi yoktur.
 * Kilit yok: atomic + cmpxchg; yaris halinde kaybeden satir bastirilir (sayilir).
 */
#ifndef KK_M3B_LOG_H
#define KK_M3B_LOG_H
#include <linux/atomic.h>
#include <linux/jiffies.h>
#include <linux/compiler.h>

#define KK_LOG_ILK	20
#define KK_LOG_ARALIK_S	60
#define KK_LOG_SLOT	16

struct kk_log_s {
	atomic_t n;		/* bu sinifta denenen toplam satir */
	atomic_t bast;		/* son basilan satirdan beri bastirilan */
	unsigned long son;	/* son basim (jiffies) */
};
extern atomic_t kk_log_bast_top;	/* surucu genelinde bastirilan toplam (kk_m3b.c) */

/* 1: satir basilabilir (*bast = bu satirdan once bastirilan sayi, ozet), 0: bastirma (sayildi). */
static inline int kk_log_izin(struct kk_log_s *s, int ilk, unsigned int aralik_s, int *bast)
{
	unsigned long j = jiffies, o;
	int n = atomic_inc_return(&s->n);

	*bast = 0;
	if (n > 0 && n <= ilk) {
		WRITE_ONCE(s->son, j);
		return 1;
	}
	o = READ_ONCE(s->son);
	if (time_before(j, o + (unsigned long)aralik_s * HZ) || cmpxchg(&s->son, o, j) != o) {
		atomic_inc(&s->bast);
		atomic_inc(&kk_log_bast_top);
		return 0;
	}
	*bast = atomic_xchg(&s->bast, 0);
	return 1;
}

static inline struct kk_log_s *kk_log_slot(struct kk_log_s *tablo, unsigned int anahtar)
{
	return &tablo[anahtar % KK_LOG_SLOT];
}

/* sabit dizgi (sinif adi) -> anahtar */
static inline unsigned int kk_log_hash(const char *s)
{
	unsigned int h = 5381;

	while (s && *s)
		h = h * 33 + (unsigned char)*s++;
	return h;
}
#endif
