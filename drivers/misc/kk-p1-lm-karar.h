/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Kara Kutu G48 P1: LM36274 (i2c3, 0x11) ÜRETİM kipi — KARAR, BEYAZ LİSTE ve SIRA mantığı. Donanım erişimi yok:
 * okuma/yazma çağıranın verdiği geri çağırmalarla. Aynı dosya hem çekirdek sürücüsünde (kk-lm36274.c) hem kullanıcı
 * alanı birim testinde (work/g48/p1/birim-lm) derlenir: sınanan kod = cihazdaki kod.
 * G47 S1'in kk-s1-karar.h LM bölümünden türetildi; farklar (Beyin + Furkan, G48 P1 onayı):
 *  - 0x0F (arıza bayrakları; okununca silinen bayrak yazmacı, hi6523 0x0C-0x0F ilkesi) HİÇBİR YOLDA okunmaz:
 *    kk_lm_okunur() 0x0F'i dışlar, sürücünün tek okuma işlevi bunu ayrıca denetler.
 *  - Beyaz liste yalnız {0x04 <- 0, 0x05 <- 0}. Açılış değerine geri yazma ('geri') YOK: işlev de komut da yok.
 *  - 'kapat' başarısızsa kilit; başka yazma yok (S1 açılış değerlerini geri yazıyordu). Reboot bootloader değerini
 *    (0x04/0x9C = 1252/2047) yeniden kurar.
 *  - Açılış başına en çok 2 yazma (KK_LM_YAZMA_SINIR); durum makinesi zaten bunu sağlar, sayaç ikinci emniyet.
 * Onay (Furkan, aynen): "P1'de arka ışığın bekçiyle otomatik kapanması (yalnız onaylı yazmalar: i2c3 kapısı +
 * LM36274 0x04/0x05 <- 0)."
 */
#ifndef KK_P1_LM_KARAR_H
#define KK_P1_LM_KARAR_H

#define KK_LM_ADRES	0x11
#define KK_LM_REV	0x01
#define KK_LM_LSB	0x04	/* BRIGHTNESS_LSB [2:0] */
#define KK_LM_MSB	0x05	/* BRIGHTNESS_MSB */
#define KK_LM_EN	0x08	/* BL_ENABLE */
#define KK_LM_BIAS1	0x09	/* BIAS_CONFIG_1 */
#define KK_LM_FLAGS	0x0f	/* arıza bayrakları: ÜRETİMDE HİÇ OKUNMAZ */
#define KK_LM_SON	0x11
/* bootloader (normal açılış) değerleri [D, FBL 10-fastbootlog.txt:1731-1736; G47 E1 cihaz okuması aynı] */
#define KK_LM_BL_REV	0x05
#define KK_LM_BL_LSB	0x04
#define KK_LM_BL_MSB	0x9c
#define KK_LM_BL_EN	0x17
#define KK_LM_BL_BIAS1	0x99
#define KK_LM_YOK	(-1000)	/* okunmadı */
#define KK_LM_YAZMA_SINIR	2

/* Okunabilen yazmaçlar: 0x01-0x0E, 0x10, 0x11. 0x0F HİÇBİR YOLDA okunmaz. */
static inline int kk_lm_okunur(int reg)
{
	return reg >= 0x01 && reg <= KK_LM_SON && reg != KK_LM_FLAGS;
}

/* Kimlik: 0x01 = 0x05, 0x08 = 0x17, 0x04 = 0x04, 0x05 = 0x9C, 0x09 = 0x99. Tutmazsa yazma KİLİTLİ. */
static inline int kk_lm_kimlik_ok(const int *r)
{
	return r[KK_LM_REV] == KK_LM_BL_REV && r[KK_LM_EN] == KK_LM_BL_EN && r[KK_LM_LSB] == KK_LM_BL_LSB &&
	       r[KK_LM_MSB] == KK_LM_BL_MSB && r[KK_LM_BIAS1] == KK_LM_BL_BIAS1;
}

/* DERLENMİŞ BEYAZ LİSTE (üretim): yalnız 0x04 <- 0 ve 0x05 <- 0. */
static inline int kk_lm_yazma_izinli(int reg, int val)
{
	return (reg == KK_LM_LSB || reg == KK_LM_MSB) && val == 0;
}

struct kk_lm_io {
	void *ctx;
	int (*rd)(void *ctx, int reg);			/* >= 0 değer, < 0 hata */
	int (*wr)(void *ctx, int reg, int val);		/* 0 / < 0; çağıran ayrıca beyaz listeyi denetler */
	void (*log)(void *ctx, const char *satir);
};

#define KK_LM_LOGN 240
#define kk_lmp_log(io, ...) do { char _s[KK_LM_LOGN]; snprintf(_s, sizeof(_s), __VA_ARGS__); (io)->log((io)->ctx, _s); } while (0)

struct kk_lm_durum {
	int reg[KK_LM_SON + 1];	/* açılış (probe) okumaları; KK_LM_YOK ya da < 0 hata */
	int bus_ok;		/* 0x01 = 0x05 okundu: hat ve IC yanıt veriyor (canlı okuma serbest) */
	int kimlik;		/* kimlik tuttu */
	int kilit;		/* 1: 'kapat' reddedilir (kimlik/I2C hatası/geri okuma) */
	int kapali;		/* 'kapat' tamamlandı (0x04 = 0x05 = 0 geri okundu) */
	unsigned int yazma, hata;
};

/* Probe: önce yalnız 0x01. Hata ya da 0x05 değilse DUR (başka okuma yok). Sonra 0x02-0x0E, 0x10, 0x11. YAZMA YOK. */
static inline void kk_lm_probe_oku(struct kk_lm_durum *d, const struct kk_lm_io *io)
{
	int r, v;

	for (r = 0; r <= KK_LM_SON; r++)
		d->reg[r] = KK_LM_YOK;
	d->bus_ok = d->kimlik = d->kapali = 0;
	d->kilit = 1;
	d->yazma = d->hata = 0;
	v = io->rd(io->ctx, KK_LM_REV);
	d->reg[KK_LM_REV] = v;
	if (v < 0) {
		d->hata++;
		kk_lmp_log(io, "DUR: 0x01 okunamadi (%d); baska okuma yok, yazma KILITLI", v);
		return;
	}
	if (v != KK_LM_BL_REV) {
		kk_lmp_log(io, "DUR: 0x01 = 0x%02x (beklenen 0x05); baska okuma yok, yazma KILITLI", v);
		return;
	}
	d->bus_ok = 1;
	for (r = 0x02; r <= KK_LM_SON; r++) {
		if (!kk_lm_okunur(r))
			continue;
		v = io->rd(io->ctx, r);
		d->reg[r] = v;
		if (v < 0) {
			d->hata++;
			kk_lmp_log(io, "DUR: 0x%02x okunamadi (%d); yazma KILITLI", r, v);
			return;
		}
	}
	kk_lmp_log(io, "acilis: 01=%02x 02=%02x 03=%02x 04=%02x 05=%02x 06=%02x 07=%02x 08=%02x 09=%02x 0a=%02x 0b=%02x 0c=%02x 0d=%02x 0e=%02x 10=%02x 11=%02x (0x0F okunmaz)",
		  d->reg[1], d->reg[2], d->reg[3], d->reg[4], d->reg[5], d->reg[6], d->reg[7], d->reg[8], d->reg[9],
		  d->reg[10], d->reg[11], d->reg[12], d->reg[13], d->reg[14], d->reg[16], d->reg[17]);
	d->kimlik = kk_lm_kimlik_ok(d->reg);
	d->kilit = !d->kimlik;
	kk_lmp_log(io, "kimlik (01=05 08=17 04=04 05=9c 09=99) %s; parlaklik %d/2047; yazma %s",
		  d->kimlik ? "TUTTU" : "TUTMADI", ((d->reg[KK_LM_MSB] & 0xff) << 3) | (d->reg[KK_LM_LSB] & 7),
		  d->kimlik ? "ACIK (yalniz sysfs 'kapat': 0x04/0x05 <- 0)" : "KILITLI");
}

/* Beyaz listeli tek yazma (değer hep 0) + geri okuma. 0 = AYNI. */
static inline int kk_lm_yaz0(struct kk_lm_durum *d, const struct kk_lm_io *io, int reg)
{
	int r, g;

	if (!kk_lm_yazma_izinli(reg, 0) || d->yazma >= KK_LM_YAZMA_SINIR) {
		d->hata++;
		kk_lmp_log(io, "IZINSIZ yazma reddedildi: 0x%02x <- 0x00 (yazma %u)", reg, d->yazma);
		return -1;
	}
	d->yazma++;
	r = io->wr(io->ctx, reg, 0);
	if (r < 0) {
		d->hata++;
		kk_lmp_log(io, "YAZMA %u: 0x%02x <- 0x00 I2C HATA %d", d->yazma, reg, r);
		return -2;
	}
	g = io->rd(io->ctx, reg);
	kk_lmp_log(io, "YAZMA %u: 0x%02x <- 0x00; geri 0x%02x %s", d->yazma, reg, g & 0xff,
		  g == 0 ? "AYNI" : (g < 0 ? "OKUMA HATA" : "FARKLI"));
	if (g != 0) {
		d->hata++;
		return -3;
	}
	return 0;
}

/*
 * 'kapat' (4.9 lcdkit_backlight_ic_set_brightness(0) dizisi, 0x0F okuması OLMADAN): 0x04 <- 0, 0x05 <- 0 (LSB -> MSB),
 * her yazmadan sonra geri okuma. Tutmazsa KİLİT ve başka yazma YOK (geri yazma yok; reboot bootloader değerini kurar).
 */
static inline int kk_lm_kapat(struct kk_lm_durum *d, const struct kk_lm_io *io)
{
	if (d->kilit || !d->kimlik) {
		kk_lmp_log(io, "kapat: reddedildi (%s)", d->kimlik ? "kilitli" : "kimlik tutmadi");
		return -1;
	}
	if (d->kapali) {
		kk_lmp_log(io, "kapat: zaten kapali; yazma yok");
		return 0;
	}
	if (kk_lm_yaz0(d, io, KK_LM_LSB) || kk_lm_yaz0(d, io, KK_LM_MSB)) {
		d->kilit = 1;
		kk_lmp_log(io, "kapat: BASARISIZ -> KILIT; baska yazma yok (geri yazma yok; reboot bootloader degerini kurar)");
		return -4;
	}
	d->kapali = 1;
	kk_lmp_log(io, "kapat: tamam (0x04=0x00 0x05=0x00; parlaklik 0)");
	return 0;
}

#endif /* KK_P1_LM_KARAR_H */
