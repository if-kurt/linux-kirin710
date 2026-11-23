/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Kara Kutu G48 (Wi-Fi M2a): kk-hi1102-yukle tablo tipleri, ELLE YAZILMIŞ beyaz liste (TASARIM-M2 §5.3) ve tablo öz
 * denetimi. Saf C (memcmp/strlen dışında bağımlılık yok): çekirdekte A0 adımında, ana makinede üreteç mutasyon testinde
 * (work/g48/wifi-m2a/birim/tablo-mutasyon) AYNI kod koşar. Beyaz liste üreteçten (tablo-uret.py) BAĞIMSIZDIR: üretilen
 * tablo bu listeye uymuyorsa yükleyici hiçbir SDIO erişimi yapmadan durur.
 */
#ifndef KK_HI1102_DENETIM_H
#define KK_HI1102_DENETIM_H

#define KK_H_DOSYA_SAYI		3
#define KK_H_PARCA_SECENEK	3
#define KK_H_BLK		512u
#define KK_H_HIZALA(n)		((n) < KK_H_BLK ? (((n) + 3u) & ~3u) : (((n) + KK_H_BLK - 1) & ~(KK_H_BLK - 1)))

/* adım türü (G51: READM_EFUSE = 4.9 store_efuse_info'nun 64 B READM'i (F3); READM_GERI = RAM geri okuması; G52 A': R6 öncesi) */
enum { KK_HT_READM_EC = 1, KK_HT_VERSION, KK_HT_SETPM, KK_HT_WRITEM, KK_HT_FILES, KK_HT_CALI, KK_HT_ATLA_V120, KK_HT_QUIT,
       KK_HT_READM_EFUSE, KK_HT_READM_GERI };
/* beklenen yanıt (G51: VERI64 = 64 B READM verisi) */
enum { KK_HB_YOK = 0, KK_HB_VERI4, KK_HB_SURUM, KK_HB_SET_OK, KK_HB_WRITEM_OK, KK_HB_READY_FILES_OK, KK_HB_VERI64 };

struct kk_h_adim {
	unsigned char tur, dosya, kip, yanit;
	const char *metin;		/* tel metni (sonda boşluk); FILES için NULL (parça tablosundan) */
	unsigned short metin_len;
	unsigned int adr, deger, boy;
	const char *kaynak;		/* CFG satırı (yalnız log) */
	unsigned int ofs;		/* G51: READM_GERI için beklenen baytların dosya ofseti (dosya 3 = cali); diğerlerinde 0 */
};

/* G51 M2c-A: READM 0x50000744 64 (4.9 plat_efuse.h GET_EFUSE_CMD) ve 6 × READM <adr> 64 geri okuma */
#define KK_H_EFUSE_ADR		0x50000744u
#define KK_H_READM64		64u
#define KK_H_GERI_SAYI		6
#define KK_H_DOSYA_CALI		3	/* READM_GERI dosya alanı: 0..2 firmware, 3 = cali (sıfır) */
#define KK_H_GERI_WM_ONCE	5	/* G52 A': geri okumalardan önce gönderilen WRITEM sayısı (R1..R5); 5 = R6'nın beyaz liste indeksi */

struct kk_h_parca {
	unsigned char dosya;
	unsigned int adr, ofs, n, tel;
	const char *metin;		/* "FILES 1 0x<adr> " (4.9 exec_file_type_cmd biçimi) */
	unsigned short metin_len;
};

#ifndef KK_H_YALNIZ_KIP1	/* G49 M2b: kip 1 derlemesinde WRITEM beyaz listesi, RAM bölgeleri ve tam tablo denetimi YOK */
/* ---- BEYAZ LİSTE (elle; TASARIM-M2 §5.3, O3) ---- */
struct kk_h_wm { unsigned int adr, deger; const char *ad; };
static const struct kk_h_wm kk_h_wm_izin[] = {
	{ 0x50000140, 0x6,    "R1" },
	{ 0x5000034C, 0x0,    "R2 double ANT" },
	{ 0x50007030, 0x0,    "R3 double ANT" },
	{ 0x50007004, 0x2,    "R4 double ANT" },
	{ 0x50007000, 0x2,    "R5 double ANT" },
	{ 0x50002280, 0x5414, "R6 PMU buck 1,35 V" },
	{ 0x50002284, 0xAC00, "R7 PMU buck frekansi" },
	{ 0x50002210, 0x3518, "R8 PMU RFLDO1 1,20 V" },
	{ 0x50002214, 0x3018, "R9 PMU RFLDO2" },
	{ 0x50000074, 0x1,    "R10" },
	{ 0x50000C04, 0x0,    "R11 SOFT_BCPU_EN" },
	{ 0x50000094, 0x1,    "R12 BCPU_DE_RESET" },
};
#define KK_H_WM_SAYI (sizeof(kk_h_wm_izin) / sizeof(kk_h_wm_izin[0]))

/* çip RAM bölgeleri: D1 (dosya 0), D2 (dosya 1), D3 (dosya 2), D4 (cali; D3'ün içinde, D3'ten sonra) */
struct kk_h_bolge { unsigned int taban, son; };
static const struct kk_h_bolge kk_h_bolge_izin[4] = {
	{ 0x00070000, 0x000D0C00 }, { 0x300E4000, 0x3013C800 }, { 0x30168000, 0x30171E00 }, { 0x30170000, 0x30170400 },
};
#endif /* !KK_H_YALNIZ_KIP1 */
#define KK_H_READM_ADR		0x50000770u
#define KK_H_READM_BOY		4u
#define KK_H_SETPM_DEGER	1u
#define KK_H_CALI_ANLAMLI	520u	/* 4.9 buff_tx[SEND_BUF_LEN]; 208 + 40 B cali + sıfır; hepsi sıfır (band_5g_enable=1) */
#define KK_H_CALI_TEL		1024u
#define KK_H_TEL_VERI_UST	800256u	/* 261 632 parçasında veri toplamı (TASARIM §1.4) */
#define KK_H_SURUM_BEKLENEN	"Hi1102V100"	/* CFG PARA_VERSION (4.9 auc_CfgVersion; DMESG:5720) */

/* metni boşluklardan böler (en çok 5 parça); parça sayısı */
static inline int kk_h_bol(const char *s, unsigned int n, const char **t, unsigned int *tl)
{
	unsigned int i = 0, k = 0;

	while (i < n && k < 5) {
		while (i < n && s[i] == ' ')
			i++;
		if (i >= n)
			break;
		t[k] = &s[i];
		tl[k] = 0;
		while (i < n && s[i] != ' ') {
			i++;
			tl[k]++;
		}
		k++;
	}
	return k;
}

/* onluk ya da 0x onaltılık (büyük/küçük); 0 = tamam */
static inline int kk_h_sayi(const char *s, unsigned int n, unsigned int *v)
{
	unsigned int i = 0, x = 0, b = 10;

	if (n >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
		b = 16;
		i = 2;
	}
	if (i == n || n - i > 8)
		return -1;
	for (; i < n; i++) {
		unsigned int d;

		if (s[i] >= '0' && s[i] <= '9')
			d = s[i] - '0';
		else if (b == 16 && s[i] >= 'a' && s[i] <= 'f')
			d = s[i] - 'a' + 10;
		else if (b == 16 && s[i] >= 'A' && s[i] <= 'F')
			d = s[i] - 'A' + 10;
		else
			return -1;
		x = x * b + d;
	}
	*v = x;
	return 0;
}

static inline int kk_h_es(const char *t, unsigned int tl, const char *s)
{
	unsigned int n = (unsigned int)strlen(s);

	return tl == n && !memcmp(t, s, n);
}

/*
 * G49 M2b: kip 1 derlemesinin tablo öz denetimi. Tablo TAM 2 adım: 0 READM_EC "READM 0x50000770 4 " (kip 1, yanıt 4 B),
 * 1 VERSION "VERSION " (kip 1, yanıt sürüm); metin biçimi tam tablodakiyle aynı kurallar; VERSION öneki beklenen.
 * Başka adım (SETPM/WRITEM/FILES/CALI/QUIT) varsa RED.
 */
static inline int kk_h_tablo_kip1_denetle(const struct kk_h_adim *t, int n, const char *surum, const char **neden)
{
	const char *tk[5];
	unsigned int tl[5], v;
	int i, k;

#define KK_HD_RED(s) do { *neden = (s); return -1; } while (0)
	if (n != 2)
		KK_HD_RED("kip 1 tablosu 2 adim degil");
	if (!surum || strlen(surum) != strlen(KK_H_SURUM_BEKLENEN) || memcmp(surum, KK_H_SURUM_BEKLENEN, strlen(surum)))
		KK_HD_RED("VERSION oneki beklenen degil");
	for (i = 0; i < n; i++) {
		const struct kk_h_adim *a = &t[i];

		if (a->kip != 1 || !a->metin)
			KK_HD_RED("kip 1 adimi degil");
		if (a->metin_len != strlen(a->metin) || a->metin_len < 2 || a->metin_len >= KK_H_BLK ||
		    a->metin[a->metin_len - 1] != ' ' || a->metin[a->metin_len - 2] == ' ')
			KK_HD_RED("metin bicimi");
		k = kk_h_bol(a->metin, a->metin_len, tk, tl);
		if (i == 0) {
			if (a->tur != KK_HT_READM_EC || k != 3 || !kk_h_es(tk[0], tl[0], "READM") ||
			    kk_h_sayi(tk[1], tl[1], &v) || v != KK_H_READM_ADR || a->adr != KK_H_READM_ADR ||
			    kk_h_sayi(tk[2], tl[2], &v) || v != KK_H_READM_BOY || a->deger != KK_H_READM_BOY ||
			    a->yanit != KK_HB_VERI4)
				KK_HD_RED("READM");
		} else {
			if (a->tur != KK_HT_VERSION || k != 1 || !kk_h_es(tk[0], tl[0], "VERSION") || a->yanit != KK_HB_SURUM)
				KK_HD_RED("VERSION");
		}
	}
	return 0;
#undef KK_HD_RED
}

#ifndef KK_H_YALNIZ_KIP1
/*
 * Tablo öz denetimi. 0 = tamam; değilse *neden. Kurallar:
 *  - adım 0 READM_EC "READM 0x50000770 4 " (kip 1), adım 1 READM_EFUSE "READM 0x50000744 64 " (kip 2; G51, 4.9 sırası),
 *    adım 2 VERSION "VERSION " (kip 1), diğer hepsi kip 2, son adım QUIT "QUIT ";
 *  - G52 A' READM_GERI: TAM 6 adet, ARDIŞIK, hepsi CALI'den ve R1..R5'ten sonra, R6'dan önce (wm_sira = 5); R6 (beyaz
 *    liste 6. girdi) tam olarak 6. geri okumanın ardındaki adımdır -> R6..R12'den ve QUIT'ten önce; metin "READM 0x%08x 64 ";
 *    dosya 0..2: adr = bölge tabanı + ofs, [ofs, ofs+64) dosyanın anlamlı baytlarında, WBS'te cali tel aralığıyla
 *    (0x30170000..+1024) kesişmez; dosya 3 (cali): [adr, adr+64) cali'nin anlamlı 520 B'ında, ofs = adr - taban;
 *  - her metin: uzunluk = strlen, sonda tek boşluk, < 512; parçaları sayısal olarak adım alanlarıyla aynı;
 *  - VERSION öneki (CFG PARA_VERSION) = KK_H_SURUM_BEKLENEN;
 *  - WRITEM: (adr, değer) beyaz listede, liste SIRASIYLA (R1..R12) ve her girdi TAM BİR kez; SETPM yalnız 1 (bir kez);
 *  - FILES: dosya 0/1/2 bu SIRAYLA her biri bir kez, taban = bölge tabanı; her parça seçeneğinde parçalar ardışık, toplam = dosya boyu,
 *    tel = hizalı(n), n <= parça boyu, [adr, adr+tel) bölgesinin içinde, metin "FILES 1 0x<adr> ";
 *  - CALI: bir kez, 0x30170000, 520 B, WBS FILES'tan SONRA; ATLA_V120 en çok bir;
 *  - parça seçeneği 0 (261 632) için veri teli toplamı <= 800 256.
 */
static inline int kk_h_tablo_denetle(const struct kk_h_adim *t, int n, const struct kk_h_parca * const *pp,
				     const unsigned int *psay, const unsigned int *pboy, const unsigned int *dboy,
				     const char *surum, const char **neden)
{
	unsigned int wm_kul[KK_H_WM_SAYI] = { 0 }, wm_sira = 0;
	int i, j, setpm = 0, cali = 0, atla = 0, dosya_g[KK_H_DOSYA_SAYI] = { 0 }, wbs_sira = -1, dosya_sira = 0;
	int efuse = 0, geri = 0;
	const char *tk[5];
	unsigned int tl[5], v;

#define KK_HD_RED(s) do { *neden = (s); return -1; } while (0)
	if (n < 3 || n > 64)
		KK_HD_RED("adim sayisi");
	if (!surum || strlen(surum) != strlen(KK_H_SURUM_BEKLENEN) || memcmp(surum, KK_H_SURUM_BEKLENEN, strlen(surum)))
		KK_HD_RED("VERSION oneki beklenen degil");
	for (i = 0; i < n; i++) {
		const struct kk_h_adim *a = &t[i];
		int k;

		if (a->kip != ((i == 0 || i == 2) ? 1 : 2))
			KK_HD_RED("kip sirasi");
		if (a->metin) {
			if (a->metin_len != strlen(a->metin) || a->metin_len < 2 || a->metin_len >= KK_H_BLK ||
			    a->metin[a->metin_len - 1] != ' ' || a->metin[a->metin_len - 2] == ' ')
				KK_HD_RED("metin bicimi");
			k = kk_h_bol(a->metin, a->metin_len, tk, tl);
		} else {
			k = 0;
		}
		switch (a->tur) {
		case KK_HT_READM_EC:
			if (i != 0 || k != 3 || !kk_h_es(tk[0], tl[0], "READM") || kk_h_sayi(tk[1], tl[1], &v) ||
			    v != KK_H_READM_ADR || a->adr != KK_H_READM_ADR || kk_h_sayi(tk[2], tl[2], &v) ||
			    v != KK_H_READM_BOY || a->deger != KK_H_READM_BOY || a->yanit != KK_HB_VERI4)
				KK_HD_RED("READM");
			break;
		case KK_HT_READM_EFUSE:
			if (i != 1 || efuse++ || k != 3 || !kk_h_es(tk[0], tl[0], "READM") || kk_h_sayi(tk[1], tl[1], &v) ||
			    v != KK_H_EFUSE_ADR || a->adr != KK_H_EFUSE_ADR || kk_h_sayi(tk[2], tl[2], &v) ||
			    v != KK_H_READM64 || a->deger != KK_H_READM64 || a->yanit != KK_HB_VERI64)
				KK_HD_RED("READM efuse");
			break;
		case KK_HT_VERSION:
			if (i != 2 || k != 1 || !kk_h_es(tk[0], tl[0], "VERSION") || a->yanit != KK_HB_SURUM)
				KK_HD_RED("VERSION");
			break;
		case KK_HT_READM_GERI: {
			unsigned int ofs = a->ofs;

			if (!cali || wm_sira != KK_H_GERI_WM_ONCE || geri >= KK_H_GERI_SAYI ||
			    (geri > 0 && t[i - 1].tur != KK_HT_READM_GERI))
				KK_HD_RED("geri okuma sirasi (CALI ve R1-R5'ten sonra, R6'dan hemen once ardisik 6)");
			geri++;
			if (k != 3 || !kk_h_es(tk[0], tl[0], "READM") || tl[1] != 10 || tk[1][1] != 'x' ||
			    kk_h_sayi(tk[1], tl[1], &v) || v != a->adr || kk_h_sayi(tk[2], tl[2], &v) || v != KK_H_READM64 ||
			    a->deger != KK_H_READM64 || a->yanit != KK_HB_VERI64 || (a->adr & 3))
				KK_HD_RED("geri okuma bicimi");
			if (a->dosya < KK_H_DOSYA_SAYI) {
				if (a->adr != kk_h_bolge_izin[a->dosya].taban + ofs || ofs + KK_H_READM64 > dboy[a->dosya] ||
				    ofs + KK_H_READM64 < ofs)
					KK_HD_RED("geri okuma dosya araligi");
				if (a->dosya == 2 && a->adr + KK_H_READM64 > kk_h_bolge_izin[3].taban &&
				    a->adr < kk_h_bolge_izin[3].taban + KK_H_CALI_TEL)
					KK_HD_RED("geri okuma cali ile ortusuyor");
			} else if (a->dosya == KK_H_DOSYA_CALI) {
				if (a->adr < kk_h_bolge_izin[3].taban || ofs != a->adr - kk_h_bolge_izin[3].taban ||
				    ofs + KK_H_READM64 > KK_H_CALI_ANLAMLI)
					KK_HD_RED("geri okuma cali araligi");
			} else {
				KK_HD_RED("geri okuma dosya");
			}
			break;
		}
		case KK_HT_SETPM:
			if (setpm++ || k != 2 || !kk_h_es(tk[0], tl[0], "SETPM") || kk_h_sayi(tk[1], tl[1], &v) ||
			    v != KK_H_SETPM_DEGER || a->deger != KK_H_SETPM_DEGER || a->yanit != KK_HB_SET_OK)
				KK_HD_RED("SETPM");
			break;
		case KK_HT_WRITEM:
			if (k != 4 || !kk_h_es(tk[0], tl[0], "WRITEM") || !kk_h_es(tk[1], tl[1], "2") ||
			    kk_h_sayi(tk[2], tl[2], &v) || v != a->adr || kk_h_sayi(tk[3], tl[3], &v) || v != a->deger ||
			    a->yanit != KK_HB_WRITEM_OK)
				KK_HD_RED("WRITEM bicimi");
			for (j = 0; j < (int)KK_H_WM_SAYI; j++)
				if (kk_h_wm_izin[j].adr == a->adr && kk_h_wm_izin[j].deger == a->deger)
					break;
			if (j == (int)KK_H_WM_SAYI)
				KK_HD_RED("WRITEM beyaz listede degil");
			if (wm_kul[j]++)
				KK_HD_RED("WRITEM iki kez");
			if ((unsigned int)j != wm_sira++)
				KK_HD_RED("WRITEM sirasi beyaz listeyle ayni degil");
			if (j == KK_H_GERI_WM_ONCE && (geri != KK_H_GERI_SAYI || t[i - 1].tur != KK_HT_READM_GERI))
				KK_HD_RED("R6 6. geri okumanin hemen ardinda degil");
			break;
		case KK_HT_FILES:
			if (a->metin || a->dosya >= KK_H_DOSYA_SAYI || dosya_g[a->dosya]++ ||
			    a->adr != kk_h_bolge_izin[a->dosya].taban || a->yanit != KK_HB_READY_FILES_OK)
				KK_HD_RED("FILES");
			if (a->dosya != dosya_sira++)
				KK_HD_RED("FILES sirasi (cr4 -> SCHED -> WBS) degil");
			if (a->dosya == 2)
				wbs_sira = i;
			break;
		case KK_HT_CALI:
			if (cali++ || k != 3 || !kk_h_es(tk[0], tl[0], "FILES") || !kk_h_es(tk[1], tl[1], "1") ||
			    kk_h_sayi(tk[2], tl[2], &v) || v != kk_h_bolge_izin[3].taban || a->adr != v ||
			    a->boy != KK_H_CALI_ANLAMLI || wbs_sira < 0 || a->yanit != KK_HB_READY_FILES_OK)
				KK_HD_RED("CALI");
			break;
		case KK_HT_ATLA_V120:
			if (atla++ || a->metin)
				KK_HD_RED("ATLA_V120");
			break;
		case KK_HT_QUIT:
			if (i != n - 1 || k != 1 || !kk_h_es(tk[0], tl[0], "QUIT") || a->yanit != KK_HB_YOK)
				KK_HD_RED("QUIT");
			break;
		default:
			KK_HD_RED("bilinmeyen adim turu");
		}
	}
	if (t[n - 1].tur != KK_HT_QUIT || !setpm || !cali)
		KK_HD_RED("QUIT/SETPM/CALI eksik");
	if (efuse != 1 || geri != KK_H_GERI_SAYI)
		KK_HD_RED("READM efuse 1 / geri okuma 6 degil");
	for (j = 0; j < (int)KK_H_WM_SAYI; j++)
		if (wm_kul[j] != 1)
			KK_HD_RED("beyaz listedeki bir WRITEM tabloda yok");
	for (j = 0; j < KK_H_DOSYA_SAYI; j++)
		if (dosya_g[j] != 1)
			KK_HD_RED("dosya sayisi");
	for (i = 0; i < KK_H_PARCA_SECENEK; i++) {
		unsigned int ofs[KK_H_DOSYA_SAYI] = { 0 }, veri = 0;

		for (j = 0; j < (int)psay[i]; j++) {
			const struct kk_h_parca *p = &pp[i][j];
			const struct kk_h_bolge *b;
			int k;

			if (p->dosya >= KK_H_DOSYA_SAYI)
				KK_HD_RED("parca dosya");
			b = &kk_h_bolge_izin[p->dosya];
			if (p->ofs != ofs[p->dosya] || !p->n || p->n > pboy[i] || p->tel != KK_H_HIZALA(p->n) ||
			    p->adr != b->taban + p->ofs || p->adr + p->tel > b->son || p->adr + p->tel < p->adr)
				KK_HD_RED("parca araligi");
			if (!p->metin || p->metin_len != strlen(p->metin))
				KK_HD_RED("parca metni");
			k = kk_h_bol(p->metin, p->metin_len, tk, tl);
			if (k != 3 || p->metin[p->metin_len - 1] != ' ' || !kk_h_es(tk[0], tl[0], "FILES") ||
			    !kk_h_es(tk[1], tl[1], "1") || kk_h_sayi(tk[2], tl[2], &v) || v != p->adr ||
			    tk[2][1] != 'x')
				KK_HD_RED("parca metni");
			ofs[p->dosya] += p->n;
			veri += p->tel;
		}
		for (j = 0; j < KK_H_DOSYA_SAYI; j++)
			if (ofs[j] != dboy[j])
				KK_HD_RED("parca toplami dosya boyu degil");
		if (i == 0 && veri + KK_H_CALI_TEL > KK_H_TEL_VERI_UST)
			KK_HD_RED("veri teli 800256 ustu");
	}
	return 0;
#undef KK_HD_RED
}
#endif /* !KK_H_YALNIZ_KIP1 */

#endif
