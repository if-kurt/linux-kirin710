/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Kara Kutu G52 M3-0.1: Hi1102 HCC (host <-> chip) başlık katmanı — SAF işlevler (G/Ç yok, bellek ayırma yok).
 *
 * DİKKAT: 4.9 yorumu "6bytes" der; packed yapı GERÇEKTE 5 bayttır (sizeof = 5, x86 ve aarch64 GCC; birim testi
 * denetler). Dolayısıyla HCC_HDR_LEN = 5, HCC_HDR_RESERVED_MAX_LEN = 59.
 *
 * Kaynak (4.9 port'u, G43 ağacı work/g43/wifi/src/platform):
 *   - struct hcc_header (5 bayt, packed; inc/oal/oal_hcc_comm.h:37-53), HCC_HDR_TOTAL_LEN 64 (:55);
 *   - hcc_tx_param_check (oal/oal_hcc_host.c:283-300), hcc_tx başlık doldurma (:540-639);
 *   - hcc_check_header_vaild (:387-396), hcc_check_rx_netbuf_vaild (:1540-1556), hcc_rx açma (:1566-1606).
 * Bayt düzeni = 4.9'un GCC little-endian bit alanları (birim testi 4.9 yapısıyla bayt bayt karşılaştırır):
 *   b0 = alt[3:0] | ana[7:4]; b1 = sira; b2 = pad_hdr; b3..b4 (u16 LE) = more b0 | option b1 | pad_payload b3:2 | pay_len b15:4
 * 4.9'a göre BİLİNÇLİ DARALTMALAR (4.9 aynı girdiyi ya bit alanında sessizce kırpar ya BUG_ON'a gider; biz -EINVAL):
 *   D1 alt >= 16 (4.9: 4 bitlik alana kırpar);  D2 pay_len > 4095 (4.9: 12 bite kırpar, sonra hizalı boy denetimine
 *   takılır);  D3 ext_len > 59 (4.9 de -EINVAL; aynı).  Sonuç kümesi: "4.9 kabul ⇔ biz kabul" ve kabulde bayt eşitliği.
 * Kernel ve kullanıcı alanı (birim testi) aynı dosyayı derler.
 */
#ifndef KK_HCC_H
#define KK_HCC_H

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/errno.h>
#else
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
#endif

#define KK_HCC_TOPLAM		64u	/* HCC_HDR_TOTAL_LEN */
#define KK_HCC_BASLIK		5u	/* sizeof(struct hcc_header) = 5 (packed; 4.9 yorumundaki 6 yanlış) */
#define KK_HCC_EK_AZAMI		(KK_HCC_TOPLAM - KK_HCC_BASLIK)	/* HCC_HDR_RESERVED_MAX_LEN = 59 */
#define KK_HCC_PAY_AZAMI	4095u	/* 12 bitlik pay_len */
#define KK_HCC_H2D_HIZA		8u	/* HISDIO_H2D_SCATT_BUFFLEN_ALIGN (oal_sdio_comm.h:109; Hi1102 dalı) */
#define KK_HCC_H2D_PKT_AZAMI	1544u	/* HSDIO_HOST2DEV_PKTS_MAX_LEN (oal_sdio_comm.h:121) */

enum kk_hcc_ana {			/* hcc_action_type */
	KK_HCC_WIFI = 0,
	KK_HCC_OAM = 1,
	KK_HCC_TEST = 2,
	KK_HCC_CHR = 3,
	KK_HCC_ANA_SON = 4,		/* HCC_ACTION_TYPE_BUTT */
};

struct kk_hcc_alan {
	u8 ana, alt, sira, pad_hdr;
	u8 more, option, pad_payload;
	u16 pay_len;
};

/* 5 bayt -> alanlar (her bayt değeri geçerli bir alan kümesine çözülür; geçerlilik ayrı denetlenir) */
static inline void kk_hcc_coz(const u8 *b, struct kk_hcc_alan *a)
{
	u16 w = (u16)(b[3] | ((u16)b[4] << 8));

	a->alt = b[0] & 0x0f;
	a->ana = b[0] >> 4;
	a->sira = b[1];
	a->pad_hdr = b[2];
	a->more = w & 1;
	a->option = (w >> 1) & 1;
	a->pad_payload = (w >> 2) & 3;
	a->pay_len = w >> 4;
}

/* alanlar -> bayt; alan sınır dışıysa -EINVAL ve b'ye dokunulmaz */
static inline int kk_hcc_paketle(const struct kk_hcc_alan *a, u8 *b, unsigned int n)
{
	u16 w;

	if (a->alt > 15 || a->ana > 15 || a->more > 1 || a->option > 1 || a->pad_payload > 3 ||
	    a->pay_len > KK_HCC_PAY_AZAMI || n < KK_HCC_BASLIK)
		return -EINVAL;
	w = (u16)(a->more | (a->option << 1) | (a->pad_payload << 2) | (a->pay_len << 4));
	b[0] = (u8)(a->alt | (a->ana << 4));
	b[1] = a->sira;
	b[2] = a->pad_hdr;
	b[3] = (u8)(w & 0xff);
	b[4] = (u8)(w >> 8);
	return 0;
}

/* = hcc_check_header_vaild */
static inline bool kk_hcc_baslik_gecerli(const struct kk_hcc_alan *a)
{
	return a->ana < KK_HCC_ANA_SON &&
	       KK_HCC_BASLIK + (u32)a->pad_hdr + a->pad_payload <= KK_HCC_TOPLAM;
}

/* = hcc_check_rx_netbuf_vaild (b = alınan tamponun başı, uzun = tampon boyu) */
static inline bool kk_hcc_rx_gecerli(const u8 *b, u32 uzun)
{
	struct kk_hcc_alan a;

	if (uzun > 0x7fffffffu)		/* 4.9 netbuf_len'i oal_int32'ye çevirir (:1546): bu boylar orada eksi -> geçersiz */
		return false;
	kk_hcc_coz(b, &a);
	if (!kk_hcc_baslik_gecerli(&a))
		return false;
	return (unsigned long long)a.pad_payload + KK_HCC_TOPLAM + a.pay_len <= uzun;
}

/*
 * = hcc_rx açma hesabı (yalnız uzunluk/ofset; memmove ve netbuf işlemleri çağıranda):
 *   ek = 64 - 5 - pad_hdr;  pad_payload > 0 ise ek veri (b + 5 + pad_hdr) pad_payload kadar sağa kaydırılır;
 *   çek = 5 + pad_hdr + pad_payload;  kalan = pay_len + ek  (sonuç tampon: [ek veri][yük]).
 * 4.9 rx_gecerli'den sonra ayrıca "5 + pad_hdr + pad_payload > uzun" ise -EINVAL döner (:1587); rx_gecerli bunu kapsar.
 */
static inline int kk_hcc_rx_ac(const u8 *b, u32 uzun, u32 *ek, u32 *cek, u32 *kalan)
{
	struct kk_hcc_alan a;

	if (!kk_hcc_rx_gecerli(b, uzun))
		return -EINVAL;
	kk_hcc_coz(b, &a);
	*ek = KK_HCC_TOPLAM - KK_HCC_BASLIK - a.pad_hdr;
	*cek = KK_HCC_BASLIK + a.pad_hdr + a.pad_payload;
	*kalan = a.pay_len + *ek;
	return 0;
}

/*
 * = hcc_tx_param_check + hcc_tx başlık doldurma (netbuf'sız hesap).
 *   girdi: ana, alt, ek_len (extend), uzun = netbuf boyu (ek dahil), adr4 = (veri adresi + ek_len) & 3 (= 4.9 pad_payload),
 *          sira, azami = g_hcc_tx_max_buf_len (4.9 açılışta min(round_down(1544, 8), tx_max)).
 *   çıktı: alanlar, itme = başlığa itilen bayt (5 + pad_hdr + pad_payload), son = gönderilecek netbuf boyu,
 *          *kaydir = 1 ise 4.9'un "pad_payload + pay_len > azami" düzeltmesi (yük 64'e kaydırılır, pad_payload 0).
 * Hata kodları 4.9 ile aynı küme: -EINVAL.
 */
static inline int kk_hcc_tx_kur(u32 ana, u32 alt, u32 ek_len, u32 uzun, u32 adr4, u8 sira, u32 azami,
				struct kk_hcc_alan *a, u32 *itme, u32 *son, int *kaydir)
{
	u32 pay, padp, padh, boy;

	if (ek_len > KK_HCC_EK_AZAMI || ana >= KK_HCC_ANA_SON)		/* param_check :286, :296 */
		return -EINVAL;
	if (alt > 15)							/* D1 */
		return -EINVAL;
	if (uzun < ek_len)						/* 4.9: payload_len eksiye düşer (unsigned) */
		return -EINVAL;
	pay = uzun - ek_len;
	if (pay > KK_HCC_PAY_AZAMI)					/* D2 */
		return -EINVAL;
	padp = adr4 & 3;
	padh = KK_HCC_EK_AZAMI - ek_len;
	if (pay > azami)						/* :605 */
		return -EINVAL;
	a->ana = (u8)ana;
	a->alt = (u8)alt;
	a->sira = sira;
	a->pad_hdr = (u8)padh;
	a->more = 0;
	a->option = 0;
	a->pad_payload = (u8)padp;
	a->pay_len = (u16)pay;
	*itme = KK_HCC_BASLIK + padh + padp;
	boy = uzun + *itme;
	*kaydir = 0;
	if (padp + pay > azami) {					/* :622-629 */
		boy -= padp;
		a->pad_payload = 0;
		*kaydir = 1;
	}
	if (((boy + KK_HCC_H2D_HIZA - 1) / KK_HCC_H2D_HIZA) * KK_HCC_H2D_HIZA > azami + KK_HCC_TOPLAM)	/* :633 */
		return -EINVAL;
	*son = boy;
	return 0;
}

#endif /* KK_HCC_H */
