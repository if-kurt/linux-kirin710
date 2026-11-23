/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Kara Kutu G48 (Wi-Fi M2a): kk-sdio-kapi <-> kk-hi1102-yukle <-> (yalnız QEMU) kk-hi1102-sahte arayüzü.
 * CONFIG_KK_HI1102_YUKLE kapalıyken kapı tarafındaki çağrı boş satır içi işlevdir (kapı nesnesi S3 ile aynı kalır).
 */
#ifndef KK_HI1102_YUKLE_H
#define KK_HI1102_YUKLE_H

#include <linux/types.h>

struct device;
struct sdio_func;

/* Kapının kart denetiminin bulduğu kart (yalnız çekirdek yapılarından okunur; karta erişim yok) */
struct kk_h_kart {
	struct sdio_func *func;	/* fn1; get_device ile tutulur (çağıran put_device yapar). Sahte: NULL */
	u16 vendor, device;	/* kart CIS */
	u16 fn_vendor, fn_device;
	u8 tip, fonksiyon, fn_no, genislik_bit;
	bool surucu;		/* fn1'e sürücü bağlı mı (bağlı olmamalı) */
	u32 saat;		/* host->ios.clock */
	u32 filtre_reddi;	/* host->kk_blocked (G44 filtresi) */
};

#if IS_ENABLED(CONFIG_KK_HI1102_YUKLE)
/* kk-sdio-kapi.c: 0 = kart hazır (out dolu, func get_device'lı); -EAGAIN = denetim sürüyor; -ENODEV = görülmedi/
 * beklenmeyen; -EPERM = kapı HAZIR değil (dwmmc2 oluşturulmadı). */
int kk_sdio_kapi_kart(struct device *kapi, struct kk_h_kart *out);
bool kk_sdio_kapi_hazir(struct device *kapi);
/* kk-hi1102-yukle.c: kapı probe'unun sonunda çağrılır; yalnız sysfs öznitelikleri açar (ac=1 ise), erişim yok */
void kk_hi1102_ekle(struct device *kapi);
#else
static inline void kk_hi1102_ekle(struct device *kapi) { }
#endif

#if IS_ENABLED(CONFIG_KK_HI1102_M3A)
/*
 * G54 M3a: kk-wifi.c (SDIO fonksiyon sürücüsü) probe'u bağlanınca motoru (A0..A10) bir kez başlatır. Erişim yok; iş kuyruğa
 * verilir. 0 = kuyrukta; -ENODEV = motor yok (kk_hi1102_yukle.ac=1 yok); -EPERM = kip 2 izinli değil; -EBUSY = bu açılışta
 * zaten başladı. f: bağlanan fn1 (sahte: NULL). kk_hi1102_m3a_ayril: remove'da; çalışan işi durdurur ve bekler.
 */
int kk_hi1102_m3a_baslat(struct sdio_func *f);
void kk_hi1102_m3a_ayril(struct sdio_func *f);
#endif

/* ---- YALNIZ QEMU sahte modeli (CONFIG_KK_HI1102_YUKLE_SAHTE); cihaz imajında derlenmez ---- */
#if IS_ENABLED(CONFIG_KK_HI1102_YUKLE_SAHTE)
int kk_hs_kart(struct kk_h_kart *out);
int kk_hs_enable_func(void);
int kk_hs_set_block_size(unsigned int blksz);
u8 kk_hs_readb(unsigned int fn, unsigned int adr, int *err);
void kk_hs_writeb(unsigned int fn, u8 v, unsigned int adr, int *err);
int kk_hs_writesb(unsigned int adr, const void *buf, unsigned int n);
int kk_hs_readsb(void *dst, unsigned int adr, unsigned int n);
u32 kk_hs_readl(unsigned int adr, int *err);
int kk_hs_memcpy_fromio(void *dst, unsigned int adr, unsigned int n);
u8 kk_hs_f0_readb(unsigned int adr, int *err);	/* G51: A9 teşhisi CCCR 0x03/0x05 */
bool kk_hs_kmalloc_boz(size_t boy);
bool kk_hs_crc_boz(int dosya);
bool kk_hs_sha_boz(int dosya);
void kk_hs_kapi_boz(int *tur, u32 *deger, int *ai, int *veri, u8 *tx, unsigned int tel);
void kk_hs_ozet(void);
#else
static inline bool kk_hs_kmalloc_boz(size_t boy) { return false; }
static inline bool kk_hs_crc_boz(int dosya) { return false; }
static inline bool kk_hs_sha_boz(int dosya) { return false; }
static inline void kk_hs_kapi_boz(int *tur, u32 *deger, int *ai, int *veri, u8 *tx, unsigned int tel) { }
static inline void kk_hs_ozet(void) { }
#endif

#endif
