/* SPDX-License-Identifier: GPL-2.0 */
/* Kara Kutu G55 M3b: port <-> kk yapıştırıcı arayüzü (kk_m3b.c). Port nesnelerine kk_port_on.h üzerinden girer. */
#ifndef KK_M3B_KAPI_H
#define KK_M3B_KAPI_H
struct sdio_func;
struct oal_sdio;
struct sk_buff;
struct net_device;
int kk_m3b_guc_acik(void);
int kk_m3b_durdu(void);
void kk_m3b_dur(const char *neden);
void kk_m3b_fn_ver(struct sdio_func *f);
int kk_m3b_sdio_devral(struct oal_sdio *hi_sdio);
int kk_m3b_baslat(struct sdio_func *f);
int kk_m3b_msg_red(unsigned int val);
/* HCC tx beyaz listesi: 0 izin, <0 red (red -> kk_m3b_dur) */
int kk_m3b_tx_kapi(unsigned int main_type, unsigned int sub_type, unsigned int ext_len, const void *pay, unsigned int pay_len);
/* G56 M3b-2: cali olayı kaydı / düşürme (DUR yok) ve init'i atlanmış yol koruması (sayaç + kayıt + iş kuyruğundan DUR) */
void kk_m3b_cali_kayit(unsigned int idx, unsigned int netbuf_len, unsigned int kopya, unsigned int ofs, int son);
void kk_m3b_cali_dus(const char *neden, unsigned int idx, unsigned int netbuf_len);
void kk_m3b_koruma(const char *ad);
/* G61-4: SDIO FIFO aktarim sayaci (rw 0 okuma/rx, 1 yazma/tx); yalniz sayar */
void kk_m3b_sdio_say(int rw, unsigned int bayt, unsigned int paket);
#ifdef CONFIG_KK_HI1102_M3C
/* G57 M3c (platform/kk_m3c.c): ortak durum, veri (L3) süzgeci, sayaçlar. Adresler hiçbir satırda basılmaz. */
enum { KK_M3C_S_VERI_IZIN, KK_M3C_S_VERI_RED, KK_M3C_S_EAPOL, KK_M3C_S_DHCP, KK_M3C_S_ARP, KK_M3C_S_ICMP,
       KK_M3C_S_AUTH, KK_M3C_S_ASSOC, KK_M3C_S_DEAUTH, KK_M3C_S_DISASSOC, KK_M3C_S_BA, KK_M3C_S_ANAHTAR,
       KK_M3C_S_ANAHTAR_SIL, KK_M3C_S_VARSAYILAN, KK_M3C_S_ANAHTAR_RED, KK_M3C_S_PS_KURULMADI, KK_M3C_S_HATA_AYIKLAMA,
       KK_M3C_S_MAC_MASKE, KK_M3C_S_TCP, KK_M3C_S_UDP,
       /* G60-2: veri RED turleri (xmit suzgeci; toplami veri_red'e esit) */
       KK_M3C_S_RED_DUR, KK_M3C_S_RED_KISA, KK_M3C_S_RED_MAC, KK_M3C_S_RED_ETHERTYPE, KK_M3C_S_RED_IPV6, KK_M3C_S_RED_BASLIK,
       KK_M3C_S_RED_PARCA, KK_M3C_S_RED_PROTOKOL, KK_M3C_S_RED_UDP, KK_M3C_S_RED_ICMP, KK_M3C_S_RED_HEDEF, KK_M3C_S_RED_KAYNAK,
       KK_M3C_S_SAYI };
void kk_m3c_say(int i);
int kk_m3c_sayac_yaz(char *b, int boy);
void kk_m3c_kendi_mac(struct net_device *dev, const unsigned char *mac);
void kk_m3c_bssid_ayarla(const unsigned char *bssid);
int kk_m3c_adres_denetle(const unsigned char *a1, const unsigned char *a2, const unsigned char *a3);
int kk_m3c_bssid_mi(const unsigned char *a);
const char *kk_m3c_l3_izinli(unsigned int tur, const unsigned char *l3, unsigned int n, const unsigned char *src, int *sinif);
int kk_m3c_veri_suz(struct sk_buff *skb, struct net_device *dev);
const char *kk_m3c_mac_kaynak(void);
struct cfg80211_bss;
struct cfg80211_bss *kk_m3c_bss_al(void);
#endif
#endif
