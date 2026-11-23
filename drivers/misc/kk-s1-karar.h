/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Kara Kutu G47 S1: i2c3 kapısı, LM36274 arka ışık (E1/E2) ve PMCTRL DDR taban oyu (0x2B8) deneyinin
 * KARAR, BEYAZ LİSTE ve SIRA mantığı. Donanım erişimi yok: okuma/yazma çağıranın verdiği geri çağırmalarla.
 * Aynı dosya hem çekirdek sürücülerinde (kk-i2c3-kapi.c, kk-lm36274.c, kk-ddr-oy.c, kk-guc-teshis.c) hem
 * kullanıcı alanı birim testinde (work/g47/s1/birim) derlenir: sınanan kod = cihazdaki kod.
 *
 * Onaylı kararlar (Beyin + Furkan, G47; aynen):
 *  - E0/E1: i2c3 kapı/pinmux durumunu oku; gerekiyorsa G40 desenle aç (yalnız i2c3'ün kendi kapı/reset/pinmux
 *    bitleri). LM36274'ten yalnız okuma (yalnız 0x11 adresi, adres taraması yok).
 *  - E2: yalnız LM36274 parlaklık yazmaçları 0x04/0x05 yazılır; A/B için açılışta okunan bootloader değerine geri
 *    yazılabilir. self_test hiçbir zaman okunmaz.
 *  - DDR (koşullu): 0x2B8 yüksekte kalmışsa ve 4.9 memlat'ın boştaki değeri kaynakta kesinse yalnız oturumda o
 *    değer yazılır, ölçülür, oturum bitmeden eski değer geri yazılır.
 *
 * Adres doğrulaması (4.9 kaynağı/canlı DT; work/g47/s1/RAPOR-s1.md §2):
 *  CRG 0xFFF35000: PEREN2 +0x020, PERCLKEN2 +0x028, PERSTAT2 +0x02C (gt/st_clk_i2c3 = b7; soc_crgperiph_interface.h
 *   :16-19, :1104, :1389; canlı DT clk_i2c3 hi3xxx-clkgate <0x20 0x80>); PERRSTDIS2 +0x07C, PERRSTSTAT2 +0x080
 *   (ip_rst_i2c3 = b7; :39-40, :3247, :3328; canlı DT i2c@FDF0C000 reset-controller-reg <0x78 0x7c 0x80 7>).
 *   NOT: tasarım belgesi bu iki yazmacı "PERRSTDIS4/PERRSTSTAT4" diye adlandırıyor; adresler aynı, doğru ad *2.
 *  iomg 0xE896C700/704 (pinmux@e896c000 + gpio005/006_pmx_func <0x700 1>/<0x704 1>, function-mask 7)
 *  iocg 0xE896CF00/F04 (pinmux@e896c800 + gpio005/006_cfg_func: pull yok, drive-strength <0x10 0x70>)
 *  i2c3 0xFDF0C000, IC_ENABLE_STATUS +0x9C.
 */
#ifndef KK_S1_KARAR_H
#define KK_S1_KARAR_H

/* ------------------------------------------------------------------------------------------------------------ */
/* i2c3 kapısı                                                                                                   */
/* ------------------------------------------------------------------------------------------------------------ */
#define KK_I3_CRG_TABAN		0xfff35000ULL
#define KK_I3_PMX_TABAN		0xe896c700ULL
#define KK_I3_CFG_TABAN		0xe896cf00ULL
#define KK_I3_I2C_TABAN		0xfdf0c000ULL
/* Yalnız QEMU (CONFIG_KK_S1_SAHTE): sahte RAM blokları (work/g47/s1/qemu-s1/virt-s1.dts) */
#define KK_I3_SAHTE_CRG		0x7ff0b000ULL
#define KK_I3_SAHTE_PMX		0x7ff0c700ULL
#define KK_I3_SAHTE_CFG		0x7ff0cf00ULL
#define KK_I3_SAHTE_I2C		0x7ff0d000ULL

#define KK_I3_CRG_BOY		0x84	/* en büyük ofset PERRSTSTAT2 0x80 */
#define KK_I3_PIN_BOY		0x8	/* iki pin: +0, +4 */
#define KK_I3_PEREN2		0x020
#define KK_I3_PERCLKEN2		0x028
#define KK_I3_PERSTAT2		0x02c
#define KK_I3_PERRSTDIS2	0x07c
#define KK_I3_PERRSTSTAT2	0x080
#define KK_I3_BIT		0x80u	/* BIT(7) */
#define KK_I3_PMX_FONK		1u
#define KK_I3_CFG_MASKE		0x73u	/* pull-up b0, pull-down b1, sürücü gücü [6:4] (G40 ile aynı maske) */
#define KK_I3_CFG_49		0x10u
#define KK_I3_IC_ENABLE_STATUS	0x9c

enum { KK_I3_B_CRG = 0, KK_I3_B_PMX = 1, KK_I3_B_CFG = 2 };

/* plan bitleri (kk_i3_plan >= 0) */
#define KK_I3_P_SAAT	0x1	/* PEREN2 <- BIT(7) */
#define KK_I3_P_RESET	0x2	/* PERRSTDIS2 <- BIT(7) */
#define KK_I3_P_PMX0	0x4	/* iomg +0x700 <- 1 */
#define KK_I3_P_PMX1	0x8	/* iomg +0x704 <- 1 */
/* DUR nedenleri (kk_i3_plan < 0): hiçbir şey yazılmaz */
#define KK_I3_DUR_PMX	(-1)	/* pinmux 0 ya da 1 değil */
#define KK_I3_DUR_CFG	(-2)	/* pinconf 4.9 değeri değil; pinconf yazması onaylı DEĞİL */

struct kk_i3_oku {
	u32 clken2, stat2, rst2, pmx[2], cfg[2];
};

/* Fiziksel tabanlar beklenen mi (yanlış DT -> hiçbir şey okunmaz/yazılmaz). sahte: yalnız QEMU yapılandırması. */
static inline int kk_i3_taban_ok(u64 crg, u64 crg_boy, u64 pmx, u64 pmx_boy, u64 cfg, u64 cfg_boy, u64 i2c, int sahte)
{
	if (crg_boy < KK_I3_CRG_BOY || pmx_boy < KK_I3_PIN_BOY || cfg_boy < KK_I3_PIN_BOY)
		return 0;
	if (crg == KK_I3_CRG_TABAN && pmx == KK_I3_PMX_TABAN && cfg == KK_I3_CFG_TABAN && i2c == KK_I3_I2C_TABAN)
		return 1;
	return sahte && crg == KK_I3_SAHTE_CRG && pmx == KK_I3_SAHTE_PMX && cfg == KK_I3_SAHTE_CFG &&
	       i2c == KK_I3_SAHTE_I2C;
}

/* G1-G4 okumasından plan. Önce DUR koşulları (yazma öncesi): böylece yarım yazma durumu oluşmaz. */
static inline int kk_i3_plan(const struct kk_i3_oku *o)
{
	int i, p = 0;

	for (i = 0; i < 2; i++) {
		if (o->pmx[i] != 0 && o->pmx[i] != KK_I3_PMX_FONK)
			return KK_I3_DUR_PMX;
		if ((o->cfg[i] & KK_I3_CFG_MASKE) != KK_I3_CFG_49)
			return KK_I3_DUR_CFG;
	}
	if (!(o->stat2 & KK_I3_BIT))
		p |= KK_I3_P_SAAT;
	if (o->rst2 & KK_I3_BIT)
		p |= KK_I3_P_RESET;
	if (o->pmx[0] == 0)
		p |= KK_I3_P_PMX0;
	if (o->pmx[1] == 0)
		p |= KK_I3_P_PMX1;
	return p;
}

/* DERLENMİŞ BEYAZ LİSTE: yalnız şu 4 yazma vardır. Pinconf'a, PERDIS2'ye, PERRSTEN2'ye ASLA yazılmaz. */
static inline int kk_i3_yazma_izinli(int blok, u32 off, u32 val)
{
	if (blok == KK_I3_B_CRG)
		return (off == KK_I3_PEREN2 || off == KK_I3_PERRSTDIS2) && val == KK_I3_BIT;
	if (blok == KK_I3_B_PMX)
		return (off == 0 || off == 4) && val == KK_I3_PMX_FONK;
	return 0;
}

/* G5 (i2c3 IC_ENABLE_STATUS) yalnız saat açık ve reset bırakılmışken okunur (aksi SError riski) */
static inline int kk_i3_g5_okunur(u32 stat2, u32 rst2)
{
	return (stat2 & KK_I3_BIT) && !(rst2 & KK_I3_BIT);
}

struct kk_i3_io {
	void *ctx;
	u32 (*rd)(void *ctx, int blok, u32 off);
	void (*wr)(void *ctx, int blok, u32 off, u32 val);	/* çağıran ayrıca beyaz listeyi denetler */
	void (*log)(void *ctx, const char *satir);
};

#define KK_S1_LOGN 240
#define kk_s1_log(io, ...) do { char _s[KK_S1_LOGN]; snprintf(_s, sizeof(_s), __VA_ARGS__); (io)->log((io)->ctx, _s); } while (0)

/* Beyaz listeli tek yazma + sayaç. 0 = yazıldı. */
static inline int kk_i3_yaz(const struct kk_i3_io *io, int blok, u32 off, u32 val, unsigned int *yazma)
{
	if (!kk_i3_yazma_izinli(blok, off, val)) {
		kk_s1_log(io, "IZINSIZ yazma reddedildi: blok %d +0x%x <- 0x%x (beyaz liste disi)", blok, off, val);
		return -1;
	}
	io->wr(io->ctx, blok, off, val);
	(*yazma)++;
	return 0;
}

/*
 * Plandaki yazmaları SIRAYLA yapar (G40 deseni): saat (PEREN2) -> reset bırak (PERRSTDIS2) -> pinmux.
 * Her yazmadan sonra geri okuma; beklenen değilse DUR (geri alma denenmez, G40 ile aynı; reboot sıfırlar).
 * Dönüş: 0 = hepsi tuttu; <0 = DUR (yazılanlar *yazma'da).
 */
static inline int kk_i3_hazirla(const struct kk_i3_io *io, int plan, unsigned int *yazma)
{
	u32 once, sonra;
	int i;

	if (plan < 0)
		return -1;
	if (plan & KK_I3_P_SAAT) {
		once = io->rd(io->ctx, KK_I3_B_CRG, KK_I3_PERSTAT2);
		if (once & KK_I3_BIT) {
			kk_s1_log(io, "saat: PERSTAT2 b7 artik 1 (0x%08x); yazilmadi", once);
		} else {
			if (kk_i3_yaz(io, KK_I3_B_CRG, KK_I3_PEREN2, KK_I3_BIT, yazma))
				return -1;
			sonra = io->rd(io->ctx, KK_I3_B_CRG, KK_I3_PERSTAT2);
			kk_s1_log(io, "YAZMA %u: CRG PEREN2 +0x020 <- 0x%08x; PERSTAT2 0x%08x -> 0x%08x (beklenen 0x%08x) %s",
				  *yazma, KK_I3_BIT, once, sonra, once | KK_I3_BIT, sonra == (once | KK_I3_BIT) ? "AYNI" : "FARKLI");
			if (sonra != (once | KK_I3_BIT)) {
				kk_s1_log(io, "DUR: saat geri okumasi beklenen degil (degisen 0x%08x); geri alma denenmez", once ^ sonra);
				return -2;
			}
		}
	}
	if (plan & KK_I3_P_RESET) {
		once = io->rd(io->ctx, KK_I3_B_CRG, KK_I3_PERRSTSTAT2);
		if (!(once & KK_I3_BIT)) {
			kk_s1_log(io, "reset: PERRSTSTAT2 b7 artik 0 (0x%08x); yazilmadi", once);
		} else {
			if (kk_i3_yaz(io, KK_I3_B_CRG, KK_I3_PERRSTDIS2, KK_I3_BIT, yazma))
				return -1;
			sonra = io->rd(io->ctx, KK_I3_B_CRG, KK_I3_PERRSTSTAT2);
			kk_s1_log(io, "YAZMA %u: CRG PERRSTDIS2 +0x07c <- 0x%08x; PERRSTSTAT2 0x%08x -> 0x%08x (beklenen 0x%08x) %s",
				  *yazma, KK_I3_BIT, once, sonra, once & ~KK_I3_BIT, sonra == (once & ~KK_I3_BIT) ? "AYNI" : "FARKLI");
			if (sonra != (once & ~KK_I3_BIT)) {
				kk_s1_log(io, "DUR: reset geri okumasi beklenen degil (degisen 0x%08x); geri alma denenmez", once ^ sonra);
				return -3;
			}
		}
	}
	for (i = 0; i < 2; i++) {
		if (!(plan & (KK_I3_P_PMX0 << i)))
			continue;
		once = io->rd(io->ctx, KK_I3_B_PMX, 4 * i);
		if (once == KK_I3_PMX_FONK)
			continue;
		if (once != 0) {
			kk_s1_log(io, "DUR: pinmux +0x%x = 0x%08x (0 ya da 1 degil); yazilmadi", 0x700 + 4 * i, once);
			return -4;
		}
		if (kk_i3_yaz(io, KK_I3_B_PMX, 4 * i, KK_I3_PMX_FONK, yazma))
			return -1;
		sonra = io->rd(io->ctx, KK_I3_B_PMX, 4 * i);
		kk_s1_log(io, "YAZMA %u: iomg +0x%x <- 1; geri 0x%08x %s", *yazma, 0x700 + 4 * i, sonra,
			  sonra == KK_I3_PMX_FONK ? "AYNI" : "FARKLI");
		if (sonra != KK_I3_PMX_FONK) {
			kk_s1_log(io, "DUR: pinmux geri okumasi beklenen degil; geri alma denenmez");
			return -5;
		}
	}
	return 0;
}

/* ------------------------------------------------------------------------------------------------------------ */
/* LM36274 (i2c3, 0x11)                                                                                          */
/* ------------------------------------------------------------------------------------------------------------ */
#define KK_LM_ADRES	0x11
#define KK_LM_REV	0x01
#define KK_LM_LSB	0x04	/* BRIGHTNESS_LSB [2:0] */
#define KK_LM_MSB	0x05	/* BRIGHTNESS_MSB */
#define KK_LM_EN	0x08	/* BL_ENABLE */
#define KK_LM_BIAS1	0x09	/* BIAS_CONFIG_1 */
#define KK_LM_FLAGS	0x0f	/* arıza bayrakları: probe'da OKUNMAZ, yalnız 'kapat' başında (4.9 ovp_check emsali) */
#define KK_LM_SON	0x11
/* bootloader (normal açılış) değerleri [D, FBL 10-fastbootlog.txt:1731-1736; TASARIM §1.2] */
#define KK_LM_BL_REV	0x05
#define KK_LM_BL_LSB	0x04
#define KK_LM_BL_MSB	0x9c
#define KK_LM_BL_EN	0x17
#define KK_LM_BL_BIAS1	0x99
#define KK_LM_YOK	(-1000)	/* okunmadı */

/* probe'da okunan yazmaçlar: 0x01-0x0E, 0x10, 0x11 (0x0F hariç) */
static inline int kk_lm_probe_okunur(int reg)
{
	return reg >= 0x01 && reg <= KK_LM_SON && reg != KK_LM_FLAGS;
}

/* Kimlik: 0x01 = 0x05, 0x08 = 0x17, 0x04 = 0x04, 0x05 = 0x9C, 0x09 = 0x99. Tutmazsa yazma KİLİTLİ. */
static inline int kk_lm_kimlik_ok(const int *r)
{
	return r[KK_LM_REV] == KK_LM_BL_REV && r[KK_LM_EN] == KK_LM_BL_EN && r[KK_LM_LSB] == KK_LM_BL_LSB &&
	       r[KK_LM_MSB] == KK_LM_BL_MSB && r[KK_LM_BIAS1] == KK_LM_BL_BIAS1;
}

/* DERLENMİŞ BEYAZ LİSTE: yazmaç yalnız 0x04/0x05; değer yalnız 0 ya da açılışta okunan (= bootloader) değer. */
static inline int kk_lm_yazma_izinli(int reg, int val, int acilis_lsb, int acilis_msb)
{
	if (reg == KK_LM_LSB)
		return val == 0 || (val == acilis_lsb && acilis_lsb == KK_LM_BL_LSB);
	if (reg == KK_LM_MSB)
		return val == 0 || (val == acilis_msb && acilis_msb == KK_LM_BL_MSB);
	return 0;
}

struct kk_lm_io {
	void *ctx;
	int (*rd)(void *ctx, int reg);			/* >= 0 değer, < 0 hata */
	int (*wr)(void *ctx, int reg, int val);		/* 0 / < 0; çağıran ayrıca beyaz listeyi denetler */
	void (*log)(void *ctx, const char *satir);
};

struct kk_lm_durum {
	int reg[KK_LM_SON + 1];	/* açılış (probe) okumaları; KK_LM_YOK ya da < 0 hata */
	int bus_ok;		/* 0x01 = 0x05 okundu: hat ve IC yanıt veriyor (canlı okuma serbest) */
	int kimlik;		/* kimlik tuttu */
	int kilit;		/* 1: 'kapat' reddedilir (kimlik/I2C hatası/geri okuma/arıza bayrağı) */
	int kapali;		/* 'kapat' tamamlandı */
	int kirli;		/* son tam 'geri'den beri yazma oldu */
	int son_flags;		/* son 0x0F okuması */
	unsigned int yazma, hata;
};

/* Probe: önce yalnız 0x01. Hata ya da 0x05 değilse DUR (başka okuma yok; yanlış hız/IC). Sonra 0x02-0x0E, 0x10, 0x11. */
static inline void kk_lm_probe_oku(struct kk_lm_durum *d, const struct kk_lm_io *io)
{
	int r, v;

	for (r = 0; r <= KK_LM_SON; r++)
		d->reg[r] = KK_LM_YOK;
	d->bus_ok = d->kimlik = d->kapali = d->kirli = 0;
	d->kilit = 1;
	d->son_flags = KK_LM_YOK;
	v = io->rd(io->ctx, KK_LM_REV);
	d->reg[KK_LM_REV] = v;
	if (v < 0) {
		d->hata++;
		kk_s1_log(io, "DUR: 0x01 okunamadi (%d); baska okuma yok, yazma KILITLI", v);
		return;
	}
	if (v != KK_LM_BL_REV) {
		kk_s1_log(io, "DUR: 0x01 = 0x%02x (beklenen 0x05); baska okuma yok, yazma KILITLI", v);
		return;
	}
	d->bus_ok = 1;
	for (r = 0x02; r <= KK_LM_SON; r++) {
		if (!kk_lm_probe_okunur(r))
			continue;
		v = io->rd(io->ctx, r);
		d->reg[r] = v;
		if (v < 0) {
			d->hata++;
			kk_s1_log(io, "DUR: 0x%02x okunamadi (%d); yazma KILITLI", r, v);
			return;
		}
	}
	kk_s1_log(io, "acilis: 01=%02x 02=%02x 03=%02x 04=%02x 05=%02x 06=%02x 07=%02x 08=%02x 09=%02x 0a=%02x 0b=%02x 0c=%02x 0d=%02x 0e=%02x 10=%02x 11=%02x (0x0F okunmadi)",
		  d->reg[1], d->reg[2], d->reg[3], d->reg[4], d->reg[5], d->reg[6], d->reg[7], d->reg[8], d->reg[9],
		  d->reg[10], d->reg[11], d->reg[12], d->reg[13], d->reg[14], d->reg[16], d->reg[17]);
	d->kimlik = kk_lm_kimlik_ok(d->reg);
	d->kilit = !d->kimlik;
	kk_s1_log(io, "kimlik (01=05 08=17 04=04 05=9c 09=99) %s; parlaklik %d/2047; yazma %s",
		  d->kimlik ? "TUTTU" : "TUTMADI", ((d->reg[KK_LM_MSB] & 0xff) << 3) | (d->reg[KK_LM_LSB] & 7),
		  d->kimlik ? "ACIK (yalniz sysfs tetigiyle)" : "KILITLI");
}

/* Beyaz listeli tek yazma + geri okuma. 0 = AYNI. */
static inline int kk_lm_yaz(struct kk_lm_durum *d, const struct kk_lm_io *io, int reg, int val)
{
	int r, g;

	if (!kk_lm_yazma_izinli(reg, val, d->reg[KK_LM_LSB], d->reg[KK_LM_MSB])) {
		d->hata++;
		kk_s1_log(io, "IZINSIZ yazma reddedildi: 0x%02x <- 0x%02x", reg, val);
		return -1;
	}
	d->kirli = 1;
	d->yazma++;
	r = io->wr(io->ctx, reg, val);
	if (r < 0) {
		d->hata++;
		kk_s1_log(io, "YAZMA %u: 0x%02x <- 0x%02x I2C HATA %d", d->yazma, reg, val, r);
		return -2;
	}
	g = io->rd(io->ctx, reg);
	kk_s1_log(io, "YAZMA %u: 0x%02x <- 0x%02x; geri 0x%02x %s", d->yazma, reg, val, g & 0xff,
		  g == val ? "AYNI" : (g < 0 ? "OKUMA HATA" : "FARKLI"));
	if (g != val) {
		d->hata++;
		return -3;
	}
	return 0;
}

/* Açılış değerlerini geri yaz (LSB -> MSB). kilit 'geri'yi engellemez (kurtarma yolu). */
static inline int kk_lm_geri(struct kk_lm_durum *d, const struct kk_lm_io *io)
{
	int e = 0;

	if (!d->kimlik) {
		kk_s1_log(io, "geri: kimlik tutmadi -> yazma yok");
		return -1;
	}
	if (!d->kirli) {
		kk_s1_log(io, "geri: degisiklik yok (acilis degerleri yerinde); yazma yok");
		return 0;
	}
	if (kk_lm_yaz(d, io, KK_LM_LSB, d->reg[KK_LM_LSB]))
		e = -2;
	if (kk_lm_yaz(d, io, KK_LM_MSB, d->reg[KK_LM_MSB]))
		e = -2;
	if (e) {
		d->kilit = 1;
		d->kapali = 0;	/* durum karışık: 'kapali' değil, 'kirli' kalır (yeniden 'geri' denenebilir) */
		kk_s1_log(io, "geri: BASARISIZ; yazma KILITLI (yalniz 'geri' yeniden denenebilir; reboot bootloader degerini kurar)");
		return e;
	}
	d->kirli = 0;
	d->kapali = 0;
	kk_s1_log(io, "geri: tamam (0x04=0x%02x 0x05=0x%02x)", d->reg[KK_LM_LSB], d->reg[KK_LM_MSB]);
	return 0;
}

/*
 * E2 'kapat' (4.9 lcdkit_backlight_ic_set_brightness(0) ile aynı dizi): 0x0F oku; sonra 0x04 <- 0, 0x05 <- 0
 * (LSB -> MSB), her yazmadan sonra geri okuma. 0x0F okunamaz ya da != 0 ise YAZMA YOK (kilit).
 * Geri okuma tutmazsa: açılış değerleri geri yazılmaya çalışılır, kilit.
 */
static inline int kk_lm_kapat(struct kk_lm_durum *d, const struct kk_lm_io *io)
{
	int f;

	if (d->kilit || !d->kimlik) {
		kk_s1_log(io, "kapat: reddedildi (%s)", d->kimlik ? "kilitli" : "kimlik tutmadi");
		return -1;
	}
	if (d->kapali) {
		kk_s1_log(io, "kapat: zaten kapali; yazma yok");
		return 0;
	}
	f = io->rd(io->ctx, KK_LM_FLAGS);
	d->son_flags = f;
	if (f < 0) {
		d->hata++;
		d->kilit = 1;
		kk_s1_log(io, "kapat: 0x0F okunamadi (%d) -> yazma yok, KILIT", f);
		return -2;
	}
	if (f != 0) {
		d->kilit = 1;
		kk_s1_log(io, "kapat: 0x0F = 0x%02x (ariza bayragi) -> yazma yok, KILIT", f);
		return -3;
	}
	kk_s1_log(io, "kapat: 0x0F = 0x00");
	if (kk_lm_yaz(d, io, KK_LM_LSB, 0) || kk_lm_yaz(d, io, KK_LM_MSB, 0)) {
		d->kilit = 1;
		kk_s1_log(io, "kapat: geri okuma tutmadi -> acilis degerleri geri yaziliyor, KILIT");
		kk_lm_geri(d, io);
		return -4;
	}
	d->kapali = 1;
	kk_s1_log(io, "kapat: tamam (parlaklik 0)");
	return 0;
}

/* ------------------------------------------------------------------------------------------------------------ */
/* PMCTRL DDR taban oyu 0x2B8 (DDR_FLOOR_VOTE2)                                                                  */
/* ------------------------------------------------------------------------------------------------------------ */
/*
 * 4.9 değeri [D zincir, RAPOR-s1 §6]: canlı DT memlat-cpu0/cpu4 "hisi,devbw" governor="performance",
 * vote-reg <0x2b8 0xff>/<0x2b8 0xff00>, freq-tbl son = 0x656 (1622 MHz); devfreq_devbw.c:62-73 set_hw_vote_reg
 * (rsb 4, mbits = mask << 16) + governor_performance.c:16-41 (GOV_START -> UINT_MAX) + find_freq :118-138
 * (tablonun en büyüğü) -> alan = 1622 >> 4 = 0x65; yazılan 0x00FF0065 (cpu0) ve 0xFF006500 (cpu4); sonra hiç
 * değişmez (memlat governor'ı bağlı değil; devfreq_suspend_devbw çağıranı yok).
 */
#define KK_DDR_PMCTRL		0xfff31000ULL
#define KK_DDR_OY_OFS		0x2b8
#define KK_DDR_SONUC_OFS	0x2e0
#define KK_DDR_SAHTE_PMCTRL	0x7ff0e000ULL	/* yalnız QEMU */
#define KK_DDR_49_ALAN		0x65u
#define KK_DDR_49_YAZ0		0x00ff0065u
#define KK_DDR_49_YAZ1		0xff006500u

/* Fiziksel taban denetimi (oy ve sonuç yazmaçları ayrı 4 baytlık eşlemeler) */
static inline int kk_ddr_taban_ok(u64 oy, u64 oy_boy, u64 sonuc, u64 sonuc_boy, int sahte)
{
	if (oy_boy < 4 || sonuc_boy < 4)
		return 0;
	if (oy == KK_DDR_PMCTRL + KK_DDR_OY_OFS && sonuc == KK_DDR_PMCTRL + KK_DDR_SONUC_OFS)
		return 1;
	return sahte && oy == KK_DDR_SAHTE_PMCTRL + KK_DDR_OY_OFS && sonuc == KK_DDR_SAHTE_PMCTRL + KK_DDR_SONUC_OFS;
}

/*
 * "0x2B8 yüksek mi" ölçütü: açılışta okunan değerin alanı (alan0 [7:0] = cpu0-3, alan1 [15:8] = cpu4-7) 4.9'un
 * değeri 0x65'ten BÜYÜKSE o alan deneye girer (4.9 değerini yazmak o alanı düşürür). Alan <= 0x65 ise 4.9 değeri
 * ana hattakinden düşük değildir -> yazmanın kazanç anlamı yok, yapılmaz. Üst 16 bit 0 değilse (maske bitlerinin
 * okumada 0 dönmesi varsayımı [Ç] tutmadı) davranış belirsiz -> hiçbir alan girmez.
 * Dönüş: bit0 = alan0, bit1 = alan1.
 */
static inline unsigned int kk_ddr_kosul(u32 eski)
{
	unsigned int m = 0;

	if (eski >> 16)
		return 0;
	if ((eski & 0xff) > KK_DDR_49_ALAN)
		m |= 1;
	if (((eski >> 8) & 0xff) > KK_DDR_49_ALAN)
		m |= 2;
	return m;
}

/* DERLENMİŞ BEYAZ LİSTE: ofset yalnız 0x2B8; değer yalnız 4.9 değeri ya da açılışta okunan alan (maskeli). */
static inline int kk_ddr_yazma_izinli(u32 off, u32 val, u32 eski, unsigned int kosul)
{
	if (off != KK_DDR_OY_OFS)
		return 0;
	if (val == KK_DDR_49_YAZ0 || val == (0x00ff0000u | (eski & 0xff)))
		return !!(kosul & 1);
	if (val == KK_DDR_49_YAZ1 || val == (0xff000000u | (eski & 0xff00)))
		return !!(kosul & 2);
	return 0;
}

/* Maskeli yazmadan sonra beklenen okuma (maske bitleri okumada 0) */
static inline u32 kk_ddr_beklenen(u32 once, u32 val)
{
	u32 m = val >> 16;

	return ((once & 0xffff) & ~m) | (val & m);
}

struct kk_ddr_io {
	void *ctx;
	u32 (*rd)(void *ctx, u32 off);
	void (*wr)(void *ctx, u32 off, u32 val);	/* çağıran ayrıca beyaz listeyi denetler */
	void (*log)(void *ctx, const char *satir);
};

struct kk_ddr_durum {
	u32 eski;
	unsigned int kosul, uygulanan, yazma;
	int kilit;
};

static inline void kk_ddr_baslat(struct kk_ddr_durum *d, const struct kk_ddr_io *io)
{
	d->eski = io->rd(io->ctx, KK_DDR_OY_OFS);
	d->kosul = kk_ddr_kosul(d->eski);
	d->uygulanan = 0;
	d->yazma = 0;
	d->kilit = 0;
	kk_s1_log(io, "acilis 0x2b8=0x%08x alan0=0x%02x (%u MHz) alan1=0x%02x (%u MHz); 4.9 degeri 0x65/0x65 (1616 MHz); kosul=%u -> %s",
		  d->eski, d->eski & 0xff, (d->eski & 0xff) << 4, (d->eski >> 8) & 0xff, ((d->eski >> 8) & 0xff) << 4,
		  d->kosul, d->kosul ? "deney YAPILABILIR (yalniz sysfs 'uygula')" :
		  (d->eski >> 16 ? "YAPILMAZ (ust 16 bit 0 degil: belirsiz)" : "YAPILMAZ (iki alan da <= 0x65)"));
}

static inline int kk_ddr_yaz(struct kk_ddr_durum *d, const struct kk_ddr_io *io, u32 val)
{
	u32 once, g, b;

	if (!kk_ddr_yazma_izinli(KK_DDR_OY_OFS, val, d->eski, d->kosul)) {
		kk_s1_log(io, "IZINSIZ yazma reddedildi: 0x2b8 <- 0x%08x", val);
		return -1;
	}
	once = io->rd(io->ctx, KK_DDR_OY_OFS);
	io->wr(io->ctx, KK_DDR_OY_OFS, val);
	d->yazma++;
	g = io->rd(io->ctx, KK_DDR_OY_OFS);
	b = kk_ddr_beklenen(once, val);
	kk_s1_log(io, "YAZMA %u: 0x2b8 <- 0x%08x; 0x%08x -> 0x%08x (beklenen 0x%08x) %s", d->yazma, val, once, g, b,
		  g == b ? "AYNI" : "FARKLI");
	return g == b ? 0 : -2;
}

/* Uygulanan alanlara açılış değerini geri yaz. kilit 'geri'yi engellemez. */
static inline int kk_ddr_geri(struct kk_ddr_durum *d, const struct kk_ddr_io *io)
{
	int e = 0, a;

	if (!d->uygulanan) {
		kk_s1_log(io, "geri: uygulanan alan yok; yazma yok");
		return 0;
	}
	for (a = 0; a < 2; a++) {
		if (!(d->uygulanan & (1u << a)))
			continue;
		if (kk_ddr_yaz(d, io, a ? (0xff000000u | (d->eski & 0xff00)) : (0x00ff0000u | (d->eski & 0xff))))
			e = -1;
		else
			d->uygulanan &= ~(1u << a);
	}
	if (e)
		d->kilit = 1;
	kk_s1_log(io, "geri: %s (0x2b8 simdi 0x%08x)", e ? "BASARISIZ, KILIT" : "tamam", io->rd(io->ctx, KK_DDR_OY_OFS));
	return e;
}

static inline int kk_ddr_uygula(struct kk_ddr_durum *d, const struct kk_ddr_io *io)
{
	int a;

	if (d->kilit) {
		kk_s1_log(io, "uygula: reddedildi (kilitli)");
		return -1;
	}
	if (!d->kosul) {
		kk_s1_log(io, "uygula: reddedildi (KOSUL YOK: 0x2b8 acilis 0x%08x, alanlar <= 0x65 ya da ust bitler dolu)", d->eski);
		return -1;
	}
	for (a = 0; a < 2; a++) {
		if (!(d->kosul & (1u << a)) || (d->uygulanan & (1u << a)))
			continue;
		if (kk_ddr_yaz(d, io, a ? KK_DDR_49_YAZ1 : KK_DDR_49_YAZ0)) {
			d->uygulanan |= 1u << a;	/* yazma oldu; geri yazılacak */
			d->kilit = 1;
			kk_s1_log(io, "uygula: geri okuma tutmadi -> acilis degeri geri yaziliyor, KILIT");
			kk_ddr_geri(d, io);
			return -2;
		}
		d->uygulanan |= 1u << a;
	}
	kk_s1_log(io, "uygula: tamam (uygulanan alanlar 0x%x)", d->uygulanan);
	return 0;
}

#endif /* KK_S1_KARAR_H */
