/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Kara Kutu G55 M3b — YALNIZ QEMU (CONFIG_KK_HI1102_YUKLE_SAHTE): port'un SDIO çağrıları sahte Hi1102 modeline
 * (kk-hi1102-sahte.c, kk_hs_*) yönlenir; QEMU'da gerçek SDIO kartı yok (M3a motoru da aynı modeli kullanır).
 * Cihaz imajında bu dosya hiçbir şey tanımlamaz.
 */
#ifndef KK_PORT_SAHTE_H
#define KK_PORT_SAHTE_H
#if IS_ENABLED(CONFIG_KK_HI1102_YUKLE_SAHTE)
#include <linux/types.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/core.h>
#include <linux/mmc/host.h>
#include <linux/mmc/card.h>
u8 kk_hs_readb(unsigned int fn, unsigned int adr, int *err);
void kk_hs_writeb(unsigned int fn, u8 v, unsigned int adr, int *err);
int kk_hs_writesb(unsigned int adr, const void *buf, unsigned int n);
int kk_hs_readsb(void *dst, unsigned int adr, unsigned int n);
u32 kk_hs_readl(unsigned int adr, int *err);
int kk_hs_memcpy_fromio(void *dst, unsigned int adr, unsigned int n);
u8 kk_hs_f0_readb(unsigned int adr, int *err);
int kk_q_yok(const char *ad);
void kk_q_writel(u32 v, unsigned int adr, int *err);
int kk_q_toio(unsigned int adr, const void *src, int n);
void kk_q_mmc_wait_for_req(struct mmc_host *h, struct mmc_request *mrq);

#define sdio_readb(f, a, e)		kk_hs_readb(1, (a), (e))
#define sdio_writeb(f, v, a, e)		kk_hs_writeb(1, (v), (a), (e))
#define sdio_f0_readb(f, a, e)		kk_hs_f0_readb((a), (e))
#define sdio_f0_writeb(f, v, a, e)	kk_hs_writeb(0, (v), (a), (e))
#define sdio_readl(f, a, e)		kk_hs_readl((a), (e))
#define sdio_writel(f, v, a, e)		kk_q_writel((v), (a), (e))
#define sdio_readsb(f, d, a, n)		kk_hs_readsb((d), (a), (n))
#define sdio_writesb(f, a, s, n)	kk_hs_writesb((a), (s), (n))
#define sdio_memcpy_fromio(f, d, a, n)	kk_hs_memcpy_fromio((d), (a), (n))
#define sdio_memcpy_toio(f, a, s, n)	kk_q_toio((a), (s), (n))
#define sdio_claim_host(f)		do { (void)(f); } while (0)
#define sdio_release_host(f)		do { (void)(f); } while (0)
#define sdio_enable_func(f)		kk_q_yok("sdio_enable_func")
#define sdio_disable_func(f)		kk_q_yok("sdio_disable_func")
#define sdio_set_block_size(f, b)	kk_q_yok("sdio_set_block_size")
#define sdio_claim_irq(f, h)		kk_q_yok("sdio_claim_irq")
#define sdio_release_irq(f)		kk_q_yok("sdio_release_irq")
#define mmc_wait_for_req(h, m)		kk_q_mmc_wait_for_req((h), (m))
#endif
#endif
