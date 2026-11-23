/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Kara Kutu G55 M3b: HCC tx beyaz listesi (B1/B2; derleme zamanı tablo). Kaynak: m3b/HCC-PROTOKOL.md §1.2, §2, §6.
 * Tel: hcc_tx'e gelen tampon = 3 B frw ext {nest_type, nest_sub, vap/dev/chip} + yük. Yalnız HCC (WIFI, 0), extend 3.
 * frw tip/alt tip AYGIT (4.9 orj) numaraları; DFS/EDCA_OPT_AP açık derlendiği için host enum'u aynı (Kbuild notu).
 * Liste dışı her tx -> kk_m3b_dur (gönderilmez). İzinliler tek başına çerçeve yayını üretmez [Ç firmware = dmac referansı].
 */
#ifndef KK_M3B_TX_LISTE_H
#define KK_M3B_TX_LISTE_H

#define KK_NEST_HOST_CRX	1
#define KK_NEST_WLAN_CTX	7
#define KK_HCRX_CREATE_CFG_VAP	1
#define KK_HCRX_SYN_CFG		2
#define KK_WCTX_SCAN_REQ	6
#define KK_WCTX_SET_RX_FILTER	23

static inline unsigned int kk_u16(const unsigned char *p) { return p[0] | (p[1] << 8); }
static inline unsigned int kk_u32(const unsigned char *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned int)p[3] << 24); }

/* TR kanalları (port wal_regdb TR: 2402-2482, 5170-5330, 5490-5710, 5735-5835) */
static inline int kk_tr_kanal(unsigned int ch, unsigned int band)
{
	if (band == 0)
		return ch >= 1 && ch <= 13;
	if (band == 1)
		return (ch >= 36 && ch <= 64 && !(ch % 4)) || (ch >= 100 && ch <= 140 && !(ch % 4)) ||
		       (ch >= 149 && ch <= 165 && (ch % 4) == 1);
	return 0;
}

/* cfg-id (us_len, gövde b) denetimi; NULL = izin */
static const char *kk_cfg_izinli(unsigned int id, const unsigned char *b, unsigned int ul)
{
	unsigned int i;

	switch (id) {
	case 100:	/* ADD_VAP: STA, p2p 0 */
	case 102:	/* DEL_VAP */
		if (ul != 24) return "ADD/DEL_VAP boy";
		if (id == 100 && b[0] != 1) return "ADD_VAP STA degil";
		if (b[5] != 0) return "ADD/DEL_VAP p2p != 0";
		return NULL;
	case 101:	/* START_VAP */
	case 103:	/* DOWN_VAP */
		if (ul != 16) return "START/DOWN_VAP boy";
		if (b[4] != 0) return "START/DOWN_VAP p2p != 0";
		return NULL;
	case 0:		/* STATION_ID */
		if (ul != 8) return "STATION_ID boy";
		if (b[6] != 0) return "STATION_ID p2p != 0";
		if (b[0] & 1) return "STATION_ID cok noktali MAC";
		return NULL;
	case 115:	/* VAP_STATE_SYN: hmac_fsm durum degisimi (1 B durum). G56: yalniz 4 STA_FAKE_UP, 5 STA_WAIT_SCAN, 6 STA_SCAN_COMP
			   (QEMU tel kaydinda gorulenler). 1 UP firmware'de STA keepalive'i baslatir [D src-m3a dmac_config.c:2194-2196] -> RED. */
		return (ul == 1 && (b[0] == 4 || b[0] == 5 || b[0] == 6)) ? NULL : "VAP_STATE_SYN boy/deger (yalniz 4/5/6)";
	case 503:	/* COUNTRY_FOR_DFS */
		return ul == 1 ? NULL : "COUNTRY_FOR_DFS boy";
	case 247:	/* COUNTRY: yalnız TR */
		if (ul < 12) return "COUNTRY boy";
		if (b[0] != 'T' || b[1] != 'R') return "COUNTRY TR degil";
		if (ul != 12u * b[4] + 12u && ul != 12u * b[4] + 16u) return "COUNTRY boy/rc_num";
		return NULL;
	case 570:	/* SET_PM_SWITCH: yalnız 0 (uyku yok) */
		for (i = 0; i < ul; i++)
			if (b[i]) return "SET_PM_SWITCH 0 degil";
		return NULL;
	case 314:	/* SET_WPS_P2P_IE: yalnız PROBE_REQ_IE tipi (pasifte etkisiz) */
		if (ul < 8) return "SET_WPS_P2P_IE boy";
		return b[4] == 1 ? NULL : "SET_WPS_P2P_IE tip";
	case 287:	/* SCAN_ABORT */
	case 118:	/* STOP_SCHED_SCAN */
	case 607:	/* SET_CUS_DTS_CALI (K1: tek açılışta custom cali, 4.9 akışı) */
	case 608: case 606: case 604: case 603: case 605: case 600: case 601: case 602: case 523: case 662: case 571:
		return NULL;	/* ini / güç / RF / log / hcc yapılandırması (4.9 INI ile aynı) */
	}
	return "cfg-id beyaz listede degil";
}

static const char *kk_m3b_tx_izinli(unsigned int main_type, unsigned int sub_type, unsigned int ext_len,
				    const void *pay, unsigned int len)
{
	const unsigned char *d = pay, *p;
	unsigned int nest, nsub, pl, i, n;

	if (main_type != 0 || sub_type != 0)
		return "HCC ana/alt tip (0,0) degil";
	if (ext_len != 3)
		return "extend_len 3 degil (veri/mgmt cercevesi)";
	if (!d || len < 3)
		return "kisa";
	nest = d[0]; nsub = d[1]; p = d + 3; pl = len - 3;
	if (nest == KK_NEST_HOST_CRX) {
		if (nsub == KK_HCRX_CREATE_CFG_VAP)
			return pl == 2 ? NULL : "CREATE_CFG_VAP boy";
		if (nsub == KK_HCRX_SYN_CFG) {
			if (pl < 4)
				return "SYN_CFG kisa";
			if (kk_u16(p + 2) + 4 != pl)
				return "SYN_CFG us_len tutarsiz";
			return kk_cfg_izinli(kk_u16(p), p + 4, kk_u16(p + 2));
		}
		return "HOST_CRX alt tip";
	}
	if (nest == KK_NEST_WLAN_CTX) {
		if (nsub == KK_WCTX_SET_RX_FILTER)
			return pl == 4 ? NULL : "SET_RX_FILTER boy";
		if (nsub == KK_WCTX_SCAN_REQ) {
			if (pl != 520) return "SCAN_REQ boy";
			if (p[1] != 0) return "SCAN_REQ PASIF degil (en_scan_type)";
			if (p[3] != 0) return "SCAN_REQ ssid_num != 0";
			if (p[313] != 0) return "SCAN_REQ probe sayisi != 0";
			n = p[496];
			if (n == 0 || n > 43) return "SCAN_REQ kanal sayisi";
			for (i = 0; i < n; i++)
				if (!kk_tr_kanal(p[324 + 4 * i], p[324 + 4 * i + 1]))
					return "SCAN_REQ kanal TR listesinde degil";
			return NULL;
		}
		return "WLAN_CTX alt tip (ACTION/MGMT/SCHED_SCAN/kullanici/...)";
	}
	return "frw tip (HOST_DRX veri, WLAN_DTX, diger)";
}

#ifdef CONFIG_KK_HI1102_M3C
/*
 * G57 M3c (Beyin G57 çerçevesi; G56 taslağından daraltıldı): M3b listesinin ÜSTÜNE bağlanma türleri. Kaynak TASARIM-M3C.md §1.4
 * (4.9 dosya:satır). Dönüş: NULL = M3c listesinde; *sinif = kayıt adı; *gizli = 1 ise yük onaltılık BASILMAZ.
 * CİHAZDA M3c türlerinin HİÇBİRİ onaltılık basılmaz (SSID / BSSID / anahtar / IP içerebilir); yalnız QEMU'da (M3C_QEMU)
 * gizli olmayan sınıflar basılır (tel düzeni kanıtı). Liste dışı / değer dışı -> RED + DUR (kk_m3b.c).
 * Çerçeve (MGMT/ACTION/HOST_DRX) ext = 3 (frw) + 19 (mac_tx_ctl_cut, packed) + 36 (MAX_MAC_HEAD_LEN) = 58;
 * 802.11 başlığı p[22..45] (FC 22, a1 26, a2 32, a3 38), gövde p[58..] [D hmac_hcc_adapt.c:208-275, hmac_ext_if.h:285-326].
 * Boylar: 4.9 tel düzeni (yapi/yapi-karsilastir-g57.txt; 484 = 44, WLAN_CTX 14 = 128, BA_SYNC = dolgulu).
 */
#define KK_NEST_HOST_DRX	2
#define KK_M3C_EXT_CERCEVE	58
#define KK_M3C_FC		22
#define KK_M3C_A1		26
#define KK_M3C_A2		32
#define KK_M3C_A3		38
#define KK_M3C_GOVDE		58
#define KK_SIFRE_TKIP		0x000FAC02u
#define KK_SIFRE_CCMP		0x000FAC04u
#define KK_M3C_BA_SYNC_BOY	32	/* dmac_ctx_action_event_stru 4.9 (WMMAC) duzeni; port dolgulu, BUILD_BUG_ON hmac_hcc_adapt.c */

static const char *kk_m3c_cfg(unsigned int id, const unsigned char *b, unsigned int ul, const char **sinif, int *gizli)
{
	unsigned int i, sifre;

	switch (id) {
	case 115:	/* VAP_STATE_SYN: bağlanma durumları + UP (1: keepalive başlar — bağlı STA'da beklenen); 0 VAP silme */
		*sinif = "cfg 115 VAP_STATE_SYN";
		return (ul == 1 && (b[0] == 0 || b[0] == 1 || (b[0] >= 4 && b[0] <= 11))) ? NULL : "VAP_STATE_SYN (0,1,4-11 disi)";
	case 314:	/* SET_WPS_P2P_IE: 1 PROBE_REQ (pasifte etkisiz), 3 ASSOC_REQ (wpa_s IE'leri: RSN/ext cap) */
		*sinif = "cfg 314 APP_IE"; *gizli = 1;
		return (ul >= 8 && (b[4] == 1 || b[4] == 3)) ? NULL : "SET_WPS_P2P_IE tip (1/3 disi)";
	case 484:	/* CONNECT_REQ: 4.9 düzeni 44 B; WEP yok, MFP yok, WPS yok, 11R dolgusu 0; auth OPEN(0)/AUTOMATIC(8) */
		*sinif = "cfg 484 CONNECT_REQ"; *gizli = 1;
		if (ul != 44) return "CONNECT_REQ boy 44 (4.9 duzeni) degil";
		if (b[1] != 0 && b[1] != 8) return "CONNECT_REQ auth acik sistem degil";
		if (b[2] != 0) return "CONNECT_REQ WEP anahtari";
		if (b[17] != 0) return "CONNECT_REQ MFP";
		if (b[19] != 0) return "CONNECT_REQ WPS";
		for (i = 32; i < 40; i++)
			if (b[i]) return "CONNECT_REQ 11R dolgusu 0 degil";
		return NULL;
	case 120:	/* ADD_KEY: mac_addkey_param_stru 68 B {idx, ikili, mac[6], key[32], seq[16], key_len, seq_len, cipher} */
		*sinif = "cfg 120 ADD_KEY"; *gizli = 1;
		if (ul != 68) return "ADD_KEY boy";
		if (b[0] > 3 || b[1] > 1) return "ADD_KEY indeks/ikili";
		sifre = kk_u32(b + 64);
		if (sifre == KK_SIFRE_CCMP && kk_u32(b + 56) == 16) ;
		else if (sifre == KK_SIFRE_TKIP && !b[1] && kk_u32(b + 56) == 32) ;
		else return "ADD_KEY sifre (ikili CCMP / grup CCMP-TKIP) disi";
		if (b[1] && !kk_m3c_bssid_mi(b + 2)) return "ADD_KEY ikili adres onayli BSSID degil";
		return NULL;
	case 121:	/* DEFAULT_KEY */
		*sinif = "cfg 121 DEFAULT_KEY";
		return (ul == 4 && b[0] <= 3) ? NULL : "DEFAULT_KEY boy/indeks";
	case 123:	/* REMOVE_KEY: {idx, ikili, mac[6]} */
		*sinif = "cfg 123 REMOVE_KEY"; *gizli = 1;
		return (ul == 8 && b[0] <= 3) ? NULL : "REMOVE_KEY boy/indeks";
	case 2004:	/* INIT_SECURTIY_PORT {mac[6], rsv[2]} — AP adresi içerir */
		*sinif = "cfg 2004 INIT_SECURITY_PORT"; *gizli = 1;
		if (ul != 8) return "INIT_SECURITY_PORT boy";
		return kk_m3c_bssid_mi(b) ? NULL : "INIT_SECURITY_PORT adres onayli BSSID degil";
	case 352: *sinif = "cfg 352 SET_PROTECTION"; return ul == 28 ? NULL : "SET_PROTECTION boy";
	case 119: *sinif = "cfg 119 STA_VAP_INFO_SYN"; return ul == 4 ? NULL : "STA_VAP_INFO_SYN boy";
	case 524: *sinif = "cfg 524 NSS"; return ul == 4 ? NULL : "NSS boy";
	case 2001: *sinif = "cfg 2001 USR_INFO_SYN"; return ul == 12 ? NULL : "USR_INFO_SYN boy";
	case 2002: *sinif = "cfg 2002 USER_ASOC_STATE_SYN"; return ul == 4 ? NULL : "USER_ASOC_STATE_SYN boy";
	case 2005: *sinif = "cfg 2005 USER_RATE_SYN"; return ul == 104 ? NULL : "USER_RATE_SYN boy";
	case 2007: *sinif = "cfg 2007 USER_CAP_SYN"; return ul == 8 ? NULL : "USER_CAP_SYN boy";
	case 2008:	/* SUSPEND_ACTION_SYN: yalnız uyanık (in_suspend 0) */
		*sinif = "cfg 2008 SUSPEND_ACTION_SYN";
		return (ul == 2 && b[0] == 0) ? NULL : "SUSPEND_ACTION_SYN boy / in_suspend != 0";
	case 494:	/* SET_PS_MODE: yalnız NO_POWERSAVE (0) */
		*sinif = "cfg 494 SET_PS_MODE";
		return (ul == 1 && b[0] == 0) ? NULL : "SET_PS_MODE != NO_POWERSAVE";
	case 232:	/* SET_STA_PM_ON: {ctrl_type, enable}; yalnız enable 0 */
		*sinif = "cfg 232 SET_STA_PM_ON";
		return (ul == 2 && b[1] == 0) ? NULL : "SET_STA_PM_ON enable != 0";
	}
	*sinif = "cfg (M3c listesinde yok)";
	return "cfg-id M3c listesinde degil";
}

/* 802.11 adresleri: a1 = onaylı BSSID (tek noktalı), a2 = kendi MAC, (yönetimde) a3 = BSSID */
static const char *kk_m3c_adres(const unsigned char *d, int a3_bssid)
{
	switch (kk_m3c_adres_denetle(d + KK_M3C_A1, d + KK_M3C_A2, a3_bssid ? d + KK_M3C_A3 : NULL)) {
	case 0: return NULL;
	case -1: return "addr1 onayli BSSID degil";
	case -2: return "addr2 kendi MAC degil";
	case -3: return "addr3 onayli BSSID degil";
	}
	return "onayli BSSID / kendi MAC yok";
}

static const char *kk_m3c_tx_izinli(unsigned int ext_len, const void *pay, unsigned int len, const char **sinif, int *gizli)
{
	const unsigned char *d = pay, *p, *g;
	unsigned int nest, nsub, pl, fc, gl;
	const char *n;
	int s;

	*sinif = "?";
	*gizli = 0;
	if (!d || len < 3)
		return "kisa";
	nest = d[0]; nsub = d[1]; p = d + 3; pl = len - 3;
	if (ext_len == 3 && nest == KK_NEST_HOST_CRX && nsub == KK_HCRX_SYN_CFG && pl >= 4 && kk_u16(p + 2) + 4 == pl)
		return kk_m3c_cfg(kk_u16(p), p + 4, kk_u16(p + 2), sinif, gizli);
	if (ext_len == 3 && nest == KK_NEST_WLAN_CTX) {
		*gizli = 1;
		switch (nsub) {
		case 1: *sinif = "WLAN_CTX 1 ADD_USER"; return pl == 80 ? NULL : "ADD_USER boy";
		case 2: *sinif = "WLAN_CTX 2 NOTIFY_ALG_ADD_USER"; return pl == 80 ? NULL : "NOTIFY_ALG_ADD_USER boy";
		case 3: *sinif = "WLAN_CTX 3 DEL_USER"; return pl == 80 ? NULL : "DEL_USER boy";
		case 4: *sinif = "WLAN_CTX 4 BA_SYNC"; return pl == KK_M3C_BA_SYNC_BOY ? NULL : "BA_SYNC boy (4.9 duzeni)";
		case 10:	/* JOIN_SET_REG {bssid[6] @0, beacon_period, kanal {num @8, band @9, ..}, .., ssid[33] @20} */
			*sinif = "WLAN_CTX 10 JOIN_SET_REG";
			if (pl != 56) return "JOIN_SET_REG boy";
			if (!kk_m3c_bssid_mi(p)) return "JOIN_SET_REG BSSID onayli degil";
			if (p[9] != 0 || !kk_tr_kanal(p[8], 0)) return "JOIN_SET_REG kanal 2,4 GHz TR degil";
			return NULL;
		case 11: *sinif = "WLAN_CTX 11 JOIN_DTIM_TSF_REG"; *gizli = 0; return pl == 16 ? NULL : "JOIN_DTIM_TSF_REG boy";
		case 12: *sinif = "WLAN_CTX 12 CONN_RESULT"; *gizli = 0; return pl == 4 ? NULL : "CONN_RESULT boy";
		case 13: *sinif = "WLAN_CTX 13 ASOC_WRITE_REG"; *gizli = 0; return pl == 4 ? NULL : "ASOC_WRITE_REG boy";
		case 14: *sinif = "WLAN_CTX 14 STA_SET_EDCA_REG"; *gizli = 0;
			if (pl != 128) return "STA_SET_EDCA_REG boy 128 (4.9 duzeni) degil";
			for (gl = 116; gl < 128; gl++)
				if (p[gl]) return "STA_SET_EDCA_REG WMMAC dolgusu 0 degil";
			return NULL;
		}
		*gizli = 0;
		*sinif = "WLAN_CTX (M3c listesinde yok)";
		return "WLAN_CTX alt tip M3c listesinde degil";
	}
	if (ext_len == KK_M3C_EXT_CERCEVE && len >= KK_M3C_GOVDE) {
		fc = d[KK_M3C_FC];
		g = d + KK_M3C_GOVDE; gl = len - KK_M3C_GOVDE;
		*gizli = 1;	/* çerçevelerin hiçbiri basılmaz (adres / SSID / veri) */
		if (nest == KK_NEST_WLAN_CTX && nsub == 8) {	/* MGMT */
			*sinif = "MGMT";
			if (d[KK_M3C_FC + 1] & 0x03) return "MGMT ToDS/FromDS";
			switch (fc) {
			case 0xb0:
				*sinif = "MGMT AUTH";
				if (gl < 6) return "AUTH kisa";
				if (kk_u16(g) != 0) return "AUTH algoritma acik sistem degil";
				if (kk_u16(g + 2) != 1) return "AUTH sira 1 degil";
				break;
			case 0x00: *sinif = "MGMT ASSOC_REQ"; break;	/* gövdede SSID IE */
			case 0x20: *sinif = "MGMT REASSOC_REQ"; break;
			case 0xc0: *sinif = "MGMT DEAUTH"; break;
			case 0xa0: *sinif = "MGMT DISASSOC"; break;
			default: return "MGMT alt tip (probe/beacon/action/...) M3c listesinde degil";
			}
			return kk_m3c_adres(d, 1);
		}
		if (nest == KK_NEST_WLAN_CTX && nsub == 0) {	/* ACTION: yalnız BlockAck (3) */
			*sinif = "ACTION BA";
			if (fc != 0xd0 || (d[KK_M3C_FC + 1] & 0x03)) return "ACTION FC";
			if (gl < 1 || g[0] != 3) return "ACTION kategori BA (3) degil";
			return kk_m3c_adres(d, 1);
		}
		if (nest == KK_NEST_HOST_DRX && nsub == 0) {	/* veri: STA -> AP (ToDS), yalnız Data/QoS Data */
			*sinif = "HOST_DRX veri";
			if (fc != 0x08 && fc != 0x88) return "HOST_DRX Data/QoS Data degil";
			if ((d[KK_M3C_FC + 1] & 3) != 1) return "HOST_DRX ToDS/FromDS != 1/0";
			if (d[KK_M3C_FC + 1] & 0x40) return "HOST_DRX Protected biti (host sifrelemez)";
			n = kk_m3c_adres(d, 0);
			if (n) return n;
			if (gl < 8 || g[0] != 0xaa || g[1] != 0xaa || g[2] != 0x03 || g[3] || g[4] || g[5])
				return "HOST_DRX LLC/SNAP degil (A-MSDU / ham)";
			n = kk_m3c_l3_izinli((g[6] << 8) | g[7], g + 8, gl - 8, NULL, &s);
			if (n) return n;
			switch (s) {
			case KK_M3C_S_EAPOL: *sinif = "HOST_DRX EAPOL"; break;
			case KK_M3C_S_ARP: *sinif = "HOST_DRX ARP"; break;
			case KK_M3C_S_DHCP: *sinif = "HOST_DRX DHCP"; break;
			case KK_M3C_S_ICMP: *sinif = "HOST_DRX ICMP"; break;
			case KK_M3C_S_TCP: *sinif = "HOST_DRX TCP"; break;
			case KK_M3C_S_UDP: *sinif = "HOST_DRX UDP"; break;
			}
			return NULL;
		}
	}
	*gizli = 0;
	*sinif = "(M3c listesinde yok)";
	return "frw tip/alt tip M3c listesinde degil";
}
#endif /* CONFIG_KK_HI1102_M3C */
#endif
