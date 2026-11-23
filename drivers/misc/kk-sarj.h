/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Kara Kutu G39/G40 (Tur 5): şarj politikası ve Faz 2 CHG_EN deneyi — arka uç arayüzü.
 * Politika (kk-sarj.c) ve deney (kk-hi6523-deney.c) çipe yalnız bu arayüzden erişir.
 * Arka uçlar: sahte çip (kk-sarj-sahte.c, yalnız QEMU) ve kk-hi6523-oku'nun I2C arka ucu
 * (CONFIG_KK_HI6523_DENEY ya da G42'den beri CONFIG_KK_SARJ_POLITIKA). Faz 1 cihaz imajında ikisi de yok.
 * G42 (Faz 3): WDT katmanı — besle_yaz (0xE8 b0, 4.9 hi6523_reset_watchdog_timer birebir).
 */
#ifndef KK_SARJ_H
#define KK_SARJ_H

#include <linux/device.h>
#include <linux/types.h>

struct kk_sarj_arka {
	const char *ad;
	void *ctx;
	/* tek yazmaç okuma (4.9'un da okuduğu yazmaçlar) */
	int (*oku)(void *ctx, u8 reg, u8 *val);
	/* YALNIZ 0x90 (CHG_REG0) yazması; çağıran oku-değiştir-yaz ile yalnız b5'i (CHG_EN) değiştirir */
	int (*chg0_yaz)(void *ctx, u8 val);
	/* G41: YALNIZ 0xE9 (WATCHDOG_CTRL) yazması; yalnız Faz 2 deneyi kullanır (WDT 0 ise 3 = 40 sn, 4.9 chip_init) */
	int (*wdt_yaz)(void *ctx, u8 val);
	/* G42: YALNIZ 0xE8 (WATCHDOG_SOFT_RST) yazması; yalnız politika (Faz 3); çağıran 4.9 gibi oku-değiştir-yaz ile b0=1 */
	int (*besle_yaz)(void *ctx, u8 val);
	/* pil girdileri (yalnız politika): vbat (mV), ibat (mA, + = deşarj), pil sıcaklığı (m°C), geçerlilik */
	int (*pil)(void *ctx, int *vbat_mv, int *ibat_ma, int *pil_mc, bool *sic_gecerli);
};

struct kk_sarj;

struct kk_sarj *kk_sarj_baslat(struct device *dev, const struct kk_sarj_arka *a);
void kk_sarj_durdur(struct kk_sarj *s);

#if IS_ENABLED(CONFIG_KK_SARJ_POLITIKA)
/* Sigorta: reboot'tan hemen önce CHG_EN=0 dener; en çok zaman_ms bekler, sonra her koşulda döner. */
void kk_sarj_sigorta(unsigned int zaman_ms);
#else
static inline void kk_sarj_sigorta(unsigned int zaman_ms) { }
#endif

#if IS_ENABLED(CONFIG_KK_SENSOR_OKUMA)
/* G42: kk-sensor-okuma'dan pil girdileri (coul V/I SPMI okuması + önbellekteki HKADC pil sıcaklığı).
 * sic_gecerli: son geçerli sıcaklık okuması en çok 75 sn önce. 0 = başarılı. */
int kk_sensor_pil(int *vbat_mv, int *ibat_ma, int *pil_mc, bool *sic_gecerli);
#else
static inline int kk_sensor_pil(int *vbat_mv, int *ibat_ma, int *pil_mc, bool *sic_gecerli) { return -ENODEV; }
#endif

#if IS_ENABLED(CONFIG_KK_HI6523_DENEY)
/* Faz 2: elle tetiklenen tek seferlik CHG_EN deneyi (sysfs "deney/"); dev'e bağlanır */
int kk_deney_kaydet(struct device *dev, const struct kk_sarj_arka *a);
void kk_deney_birak(void);
bool kk_deney_aktif(void);
void kk_deney_sigorta(unsigned int zaman_ms);
#else
static inline bool kk_deney_aktif(void) { return false; }
static inline void kk_deney_sigorta(unsigned int zaman_ms) { }
#endif

#endif
