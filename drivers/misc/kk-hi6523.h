/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Kara Kutu G39: hi6523 (scharger V300 sürücüsü, sürüm 0x200) yazmaç adresleri ve 4.9 kodlamaları.
 * Kaynak (4.9): drivers/hisi/ap/platform/kirin710/soc_schargerV300_interface.h (adres/bit),
 * drivers/power/hisi/charger/schargerV300/hisi_scharger_v300.{c,h}:
 *   IIN  (0x60 [6:2]) hi6523_get_input_current: kod -> mA tablosu (2 = 470 mA)
 *   ICHG (0x91 [4:0]) hi6523_set_charge_current: <=9: 100+100*kod; 10..14: 1000+200*(kod-9); ...
 *   VTERM(0x92 [6:3]) hi6523_set_terminal_voltage: 3800 + 50*kod (12 = 4400 mV)
 *   WDT  (0xE9 [1:0]) hi6523_set_watchdog_timer: 0 kapalı, 1 10 sn, 2 20 sn, 3 40 sn
 *   CHG_EN 0x90 b5, BATFET 0x90 b4, HiZ (buck_enb) 0xA3 b0, OTG en 0x80 b6,
 *   STATUS1 0x07: b0 batfet_ctrl, b1 wdt_time_out_n (1 = watchdog dolmadı; 4.9 HI6523_WATCHDOG_OK).
 * Yalnız sabitler ve kod çözme; yazma yok.
 */
#ifndef KK_HI6523_H
#define KK_HI6523_H

#include <linux/types.h>

#define KK_HI_ADRES		0x6b

#define KK_HI_STATUS1		0x07
#define KK_HI_BUCK0		0x60	/* [6:2] IIN */
#define KK_HI_OTG0		0x80	/* b6 otg_en, [4:3] otg_lim */
#define KK_HI_CHG0		0x90	/* b5 chg_en, b4 batfet, [3:2] pre_ichg, [1:0] pre_vchg */
#define KK_HI_CHG1		0x91	/* [4:0] ICHG */
#define KK_HI_CHG2		0x92	/* [6:3] VTERM, [2:0] ITERM */
#define KK_HI_CHG3		0x93
#define KK_HI_CHG4		0x94
#define KK_HI_CHG5		0x95
#define KK_HI_LOGIC		0xa3	/* b0 buck_enb (HiZ) */
#define KK_HI_WDT		0xe9	/* [1:0] watchdog_timer */
/* G42: 0xE8 b0 wd_rst_n (WATCHDOG_SOFT_RST). 4.9 hi6523_reset_watchdog_timer = hi6523_write_mask(0xE8, 0x01, 0, 1):
 * oku -> b0=1 -> yaz. Okuma listesinde (kk_hi_okunan) DEĞİL; yalnız politikanın besleme adımı okur/yazar. */
#define KK_HI_BESLE		0xe8
#define KK_HI_BESLE_BIT		BIT(0)

#define KK_HI_CHG_EN		BIT(5)

/*
 * G44 (O1, salt okuma). Kaynak doğrulaması (4.9 hisi_scharger_v300.c):
 *  0x06 STATUS0: hi6523_get_charge_state (V3:1466; charge_monitor/vbus denetimi ve coul huawei_get_charge_state),
 *        hi6523_check_input_dpm_state (V3:1418), _acl_state (V3:1443): tek bayt okuma, ardından yazma yok.
 *        Bitler (soc_schargerV300_interface.h:253-266): b0 rechg, b1 therm (termal regülasyon), b2 dpm,
 *        b3 acl, [5:4] chgstate, b6 otg_on, b7 buck_ok. Canlı durum yazmacı (kesme yazmaçları 0x09+ ayrı).
 *  0x8E OTG_RESERVE2: hi6523_vdpm/dpm yolu (V3:1631/1646/1657) oku-değiştir-yaz'ın okuması (hi6523_write_mask
 *        V3:278-292); [4:1] 5V VDPM seçimi (3 = 4460 mV, 4 = 4675 mV; vterm_vdpm V3:82), b5 buck OCP yarı.
 *  0x62 BUCK_REG2 OKUNMAZ: 4.9 onu olağan işleyişte okumuyor; yalnız chargelog sysfs dökümünün 0x00-0xEA blok
 *        okumasında (V3:2155, kullanıcı alanı tetikler) geçiyor -> O1 koşulu doğrulanamadı.
 */
#define KK_HI_STATUS0		0x06
#define KK_HI_OTG_RES2		0x8e

/* 4.9'un olağan işleyişte de okuduğu, okuması yan etkisiz yazmaçlar (G39 görev A.4; G44 O1: 0x06, 0x8E) */
static const u8 kk_hi_okunan[] = {
	KK_HI_STATUS1, KK_HI_BUCK0, KK_HI_OTG0, KK_HI_CHG0, KK_HI_CHG1, KK_HI_CHG2,
	KK_HI_CHG3, KK_HI_CHG4, KK_HI_CHG5, KK_HI_LOGIC, KK_HI_WDT,
#if IS_ENABLED(CONFIG_KK_HI6523_O1)
	KK_HI_STATUS0, KK_HI_OTG_RES2,
#endif
};
#define KK_HI_SAYI		ARRAY_SIZE(kk_hi_okunan)

static inline int kk_hi_iin_ma(u8 buck0)
{
	static const u16 t[] = { 100, 130, 470, 900, 1000, 1080, 1200, 1300, 1400, 1500, 1600, 1700, 1800,
				 1900, 2000, 2100, 2200, 2280, 2400, 2480, 2600, 2680, 2800, 2880, 3000, 3080, 3200 };
	u8 k = (buck0 >> 2) & 0x1f;

	return k < ARRAY_SIZE(t) ? t[k] : 9999;	/* tablo dışı: sınır kapısı için "çok büyük" */
}

static inline int kk_hi_ichg_ma(u8 chg1)
{
	u8 k = chg1 & 0x1f;

	if (k <= 9)
		return 100 + 100 * k;
	if (k <= 14)
		return 1000 + 200 * (k - 9);
	if (k <= 24)
		return 2000 + 100 * (k - 14);
	if (k <= 30)
		return 3000 + 200 * (k - 24);
	return 4500;
}

static inline int kk_hi_vterm_mv(u8 chg2)
{
	return 3800 + 50 * ((chg2 >> 3) & 0x0f);
}

static inline int kk_hi_wdt_sn(u8 wdt)
{
	static const u8 t[] = { 0, 10, 20, 40 };

	return t[wdt & 3];
}

/* Ek koşul (G39): CHG_EN=1'den önce VTERM <= 4400 mV, ICHG <= 500 mA, IIN <= 470 mA */
#define KK_HI_VTERM_TAVAN	4400
#define KK_HI_ICHG_TAVAN	500
#define KK_HI_IIN_TAVAN		470

static inline bool kk_hi_param_uygun(u8 buck0, u8 chg1, u8 chg2)
{
	return kk_hi_vterm_mv(chg2) <= KK_HI_VTERM_TAVAN && kk_hi_ichg_ma(chg1) <= KK_HI_ICHG_TAVAN &&
	       kk_hi_iin_ma(buck0) <= KK_HI_IIN_TAVAN;
}

#endif
