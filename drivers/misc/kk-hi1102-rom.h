/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Kara Kutu G48 (Wi-Fi M2a): Hi1102 ROM yükleme protokolünün SAHTE modeli (durum makinesi). YALNIZ SINAMA:
 *   - çekirdekte yalnız CONFIG_KK_HI1102_YUKLE_SAHTE (QEMU) derlemesinde kk-hi1102-sahte.c içerir;
 *   - ana makinede 4.9 eşdeğerlik harness'i (work/g48/wifi-m2a/es49) aynı modeli içerir.
 * Cihaz imajında derlenmez. Saf C: yalnız memcpy/memset/strlen; zaman ve kesme durumu çağıranın işidir.
 *
 * Protokol (TASARIM-M2 §1.3-1.4; 4.9 plat_firmware.c / plat_sdio.c): host F1 FIFO'ya ASCII komut yazar
 * ("VERSION ", "READM a n ", "SETPM v ", "WRITEM 2 a v ", "FILES 1 a ", "QUIT "); FILES'tan sonraki yazma veridir.
 * Yanıtlar: sürüm metni, READM verisi, "SET OK", "WRITEM OK", "READY", "FILES OK"; QUIT yanıtsız.
 * Model "çip RAM'ini" iki bölgeyle tutar: WCPU 0x70000-0xD0C00 ve BFGX 0x300E4000-0x30171E00 (D1-D4).
 */
#ifndef KK_HI1102_ROM_H
#define KK_HI1102_ROM_H

#define KK_ROM_WCPU_TABAN	0x00070000u
#define KK_ROM_WCPU_SON		0x000D0C00u
#define KK_ROM_BFGX_TABAN	0x300E4000u
#define KK_ROM_BFGX_SON		0x30171E00u
#define KK_ROM_SURUM		"Hi1102V100R001C01B161 May 25 2015"	/* 4.9 DMESG:5720 */

/* ROM tarafı hata enjeksiyonu (bit) */
#define KK_ROM_B_YANITSIZ	0x0001	/* hiçbir komuta yanıt yok */
#define KK_ROM_B_V100		0x0002	/* efuse EC = V100 */
#define KK_ROM_B_SURUM		0x0004	/* sürüm metni farklı */
#define KK_ROM_B_READY_YOK	0x0008	/* ilk FILES komutuna READY yok */
#define KK_ROM_B_FOK_ILK	0x0010	/* ilk veri parçasına FILES OK yok */
#define KK_ROM_B_FOK_SON	0x0020	/* son veri parçasına (cali) FILES OK yok */
#define KK_ROM_B_WOK_R6		0x0040	/* WRITEM 0x50002280 (buck) yanıtsız */
#define KK_ROM_B_SURUM2		0x0080	/* G50: önek tutar (Hi1102V100), tam metin 4.9/G49'dan farklı */
#define KK_ROM_B_GERI_BOZ	0x0100	/* G51: READM 0x000d0aac 64 (geri okuma G3) yanıtının 5. baytı ters */
#define KK_ROM_B_GERI_YANITSIZ	0x0200	/* G51: ilk RAM geri okuma READM'ine yanıt yok */
#define KK_ROM_B_EFUSE_YOK	0x0400	/* G51: READM 0x50000744 64 (efuse) yanıtsız */
#define KK_ROM_B_BCPU_SIFIR	0x0800	/* G52: R11 (0x50000c04 <- 0) + R12 (0x50000094 <- 1) görüldükten sonra BFGX
					 * (0x300E4000..0x30171E00) RAM READM'i 64 B SIFIR döner (G51 cihaz hipotezi:
					 * BCPU kapatma dizisinden sonra SCHED/WBS pencereleri sıfır okunur); WCPU etkilenmez */

struct kk_rom {
	unsigned char *wcpu, *bfgx;	/* çağıran ayırır: SON - TABAN bayt */
	unsigned int boz;
	int bekle_veri, cali_bekle;
	unsigned int files_adr;
	unsigned char yanit[64];
	unsigned int yanit_n;
	int yanit_var;
	unsigned int wm_adr[40], wm_deger[40];
	int wm_n, setpm, quit, surum_n, readm_n, files_n, veri_n, mesaj_n, hata, geri_n;
	int bcpu_en0, bcpu_kapali;	/* G52: R11 görüldü / R11 + R12 görüldü (BCPU kapatma dizisi) */
	unsigned int veri_bayt;
	const char *hata_neden;
};

static inline void kk_rom_baslat(struct kk_rom *r, unsigned char *wcpu, unsigned char *bfgx, unsigned int boz)
{
	memset(r, 0, sizeof(*r));
	r->wcpu = wcpu;
	r->bfgx = bfgx;
	r->boz = boz;
	memset(wcpu, 0, KK_ROM_WCPU_SON - KK_ROM_WCPU_TABAN);
	memset(bfgx, 0, KK_ROM_BFGX_SON - KK_ROM_BFGX_TABAN);
}

static inline void kk_rom_yanitla(struct kk_rom *r, const void *b, unsigned int n)
{
	if (r->boz & KK_ROM_B_YANITSIZ)
		return;
	if (n > sizeof(r->yanit))
		n = sizeof(r->yanit);
	memcpy(r->yanit, b, n);
	r->yanit_n = n;
	r->yanit_var = 1;
}

/* "0x..." onaltılık ya da onluk; geçersizse -1 */
static inline int kk_rom_sayi(const char *s, unsigned int n, unsigned int *v)
{
	unsigned int i = 0, x = 0, taban = 10;

	if (n >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
		taban = 16;
		i = 2;
	}
	if (i == n)
		return -1;
	for (; i < n; i++) {
		unsigned int d;
		char c = s[i];

		if (c >= '0' && c <= '9')
			d = c - '0';
		else if (taban == 16 && c >= 'a' && c <= 'f')
			d = c - 'a' + 10;
		else if (taban == 16 && c >= 'A' && c <= 'F')
			d = c - 'A' + 10;
		else
			return -1;
		x = x * taban + d;
	}
	*v = x;
	return 0;
}

/* Bir host -> FIFO yazması (tel baytları, hizalı uzunluk). Yanıt r->yanit'e konur. */
static inline void kk_rom_yaz(struct kk_rom *r, const unsigned char *b, unsigned int tel)
{
	const char *tok[6];
	unsigned int tl[6], v[6], i, n = 0, m;

	r->yanit_var = 0;
	if (r->bekle_veri) {	/* FILES verisi */
		unsigned int a = r->files_adr;
		int cali = r->cali_bekle, son = 0;

		r->bekle_veri = 0;
		r->cali_bekle = 0;
		r->veri_n++;
		r->veri_bayt += tel;
		if (a >= KK_ROM_WCPU_TABAN && a + tel <= KK_ROM_WCPU_SON && a + tel > a)
			memcpy(r->wcpu + (a - KK_ROM_WCPU_TABAN), b, tel);
		else if (a >= KK_ROM_BFGX_TABAN && a + tel <= KK_ROM_BFGX_SON && a + tel > a)
			memcpy(r->bfgx + (a - KK_ROM_BFGX_TABAN), b, tel);
		else {
			r->hata++;
			r->hata_neden = "veri bolge disi";
			return;
		}
		son = cali;
		if ((r->boz & KK_ROM_B_FOK_ILK) && r->veri_n == 1)
			return;
		if ((r->boz & KK_ROM_B_FOK_SON) && son)
			return;
		kk_rom_yanitla(r, "FILES OK", 8);
		return;
	}
	r->mesaj_n++;
	/* metin: ilk NUL'a kadar, boşlukla ayrılmış en çok 6 parça */
	for (i = 0; i < tel && b[i] && n < 6; ) {
		while (i < tel && b[i] == ' ')
			i++;
		if (i >= tel || !b[i])
			break;
		tok[n] = (const char *)&b[i];
		m = 0;
		while (i < tel && b[i] && b[i] != ' ') {
			i++;
			m++;
		}
		tl[n++] = m;
	}
	if (!n) {
		r->hata++;
		r->hata_neden = "bos mesaj";
		return;
	}
#define KK_ROM_ES(k, s) (tl[k] == sizeof(s) - 1 && !memcmp(tok[k], s, sizeof(s) - 1))
	for (i = 1; i < n; i++)
		if (kk_rom_sayi(tok[i], tl[i], &v[i]))
			v[i] = 0xffffffffu;
	if (KK_ROM_ES(0, "VERSION") && n == 1) {
		r->surum_n++;
		if (r->boz & KK_ROM_B_SURUM)
			kk_rom_yanitla(r, "Hi1101V100R001C01B099 Jan 01 2014", 33);
		else if (r->boz & KK_ROM_B_SURUM2)
			kk_rom_yanitla(r, "Hi1102V100R001C01B162 Jun 01 2015", 33);
		else
			kk_rom_yanitla(r, KK_ROM_SURUM, sizeof(KK_ROM_SURUM) - 1);
	} else if (KK_ROM_ES(0, "READM") && n == 3) {
		unsigned char d[64];

		r->readm_n++;
		if (v[1] == 0x50000770u && v[2] == 4) {
			d[0] = 0x00;
			d[1] = (r->boz & KK_ROM_B_V100) ? 0x00 : 0x01;	/* bit[185:184] EC: 1 = V120 */
			d[2] = 0x00;
			d[3] = 0x00;
			kk_rom_yanitla(r, d, 4);
		} else if (v[1] == 0x50000744u && v[2] == 64) {
			if (r->boz & KK_ROM_B_EFUSE_YOK)
				return;
			for (i = 0; i < 64; i++)
				d[i] = (unsigned char)(0xa0 + i);
			kk_rom_yanitla(r, d, 64);
		} else if (v[2] == 64 && ((v[1] >= KK_ROM_WCPU_TABAN && v[1] + 64 <= KK_ROM_WCPU_SON) ||
					  (v[1] >= KK_ROM_BFGX_TABAN && v[1] + 64 <= KK_ROM_BFGX_SON))) {
			/* G51: RAM geri okuması (4.9 ram_reg_test_cfg PARA_READM türü): sahte RAM'den 64 B */
			r->geri_n++;
			if ((r->boz & KK_ROM_B_GERI_YANITSIZ) && r->geri_n == 1)
				return;
			memcpy(d, v[1] < KK_ROM_BFGX_TABAN ? r->wcpu + (v[1] - KK_ROM_WCPU_TABAN) :
			       r->bfgx + (v[1] - KK_ROM_BFGX_TABAN), 64);
			if ((r->boz & KK_ROM_B_BCPU_SIFIR) && r->bcpu_kapali && v[1] >= KK_ROM_BFGX_TABAN)
				memset(d, 0, 64);	/* G52: BCPU kapandıktan sonra BFGX RAM sıfır okunur (hipotez modeli) */
			if ((r->boz & KK_ROM_B_GERI_BOZ) && v[1] == 0x000d0aacu)
				d[5] ^= 0xff;
			kk_rom_yanitla(r, d, 64);
		} else {
			r->hata++;
			r->hata_neden = "READM adres/boy";
		}
	} else if (KK_ROM_ES(0, "SETPM") && n == 2) {
		r->setpm = (int)v[1];
		kk_rom_yanitla(r, "SET OK", 6);
	} else if (KK_ROM_ES(0, "WRITEM") && n == 4 && v[1] == 2) {
		if (r->wm_n < 40) {
			r->wm_adr[r->wm_n] = v[2];
			r->wm_deger[r->wm_n] = v[3];
		}
		r->wm_n++;
		if (v[2] == 0x50000c04u && v[3] == 0)
			r->bcpu_en0 = 1;			/* R11 SOFT_BCPU_EN <- 0 */
		if (v[2] == 0x50000094u && v[3] == 1 && r->bcpu_en0)
			r->bcpu_kapali = 1;			/* R12 BCPU_DE_RESET <- 1 */
		if ((r->boz & KK_ROM_B_WOK_R6) && v[2] == 0x50002280u)
			return;
		kk_rom_yanitla(r, "WRITEM OK", 9);
	} else if (KK_ROM_ES(0, "FILES") && n == 3 && v[1] == 1) {
		r->files_n++;
		r->bekle_veri = 1;
		r->files_adr = v[2];
		r->cali_bekle = v[2] == 0x30170000u;
		if ((r->boz & KK_ROM_B_READY_YOK) && r->files_n == 1)
			return;
		kk_rom_yanitla(r, "READY", 5);
	} else if (KK_ROM_ES(0, "QUIT") && n == 1) {
		r->quit = 1;
	} else {
		r->hata++;
		r->hata_neden = "bilinmeyen komut";
	}
#undef KK_ROM_ES
}

#endif
