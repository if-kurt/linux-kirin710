// SPDX-License-Identifier: GPL-2.0
/*
 * Kara Kutu G57 M3c: ana hatta Hi1102 ile TEK AP'ye bağlanma (açık / WPA2-PSK), Beyin G57 çerçevesi.
 * Bu dosya port başlığı kullanmaz (yalnız çekirdek API'si): kapıların ortak durumu + veri (L3) süzgeci + sayaçlar.
 *
 *  - kendi MAC (wlan0 = MIB StationID; wal_set_mac_addr / wal_set_random_mac_to_mib) ve onaylı BSSID (connect kapısı)
 *    HCC kapısının adres denetimi içindir; HİÇBİR satırda basılmaz (BSSID dosyaya düşmez).
 *  - Veri süzgeci (ndo_start_xmit önü + HCC kapısında yeniden): yalnız EAPOL (888E), ARP (gönderen MAC = kendi),
 *    IPv4 UDP 68 -> 67 (DHCP istemcisi) ve ICMP echo isteği (hedef wlan0 alt ağında). Başka her çerçeve DÜŞER
 *    (tele inmez, sayılır; yük basılmaz). HCC kapısında liste dışı = RED + DUR (son savunma hattı).
 *  - IPv6 çekirdekte yok (CONFIG_IPV6 kapalı); çok noktalı IP / IGMP yok.
 */
#include <linux/kernel.h>
#include <linux/atomic.h>
#include <linux/string.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <linux/icmp.h>
#include <net/ip.h>
#include <linux/inetdevice.h>
#include <linux/rcupdate.h>
#include <linux/spinlock.h>
#include <linux/moduleparam.h>
#include <linux/tcp.h>
#include "kk_m3b_kapi.h"

static u8 kk_kendi[ETH_ALEN], kk_bssid[ETH_ALEN];
static bool kk_kendi_var, kk_bssid_var;
static struct net_device *kk_dev;
static DEFINE_SPINLOCK(kk_m3c_kilit);

atomic_t kk_m3c_sayac[KK_M3C_S_SAYI];
static const char *const kk_m3c_sayac_ad[KK_M3C_S_SAYI] = {
	[KK_M3C_S_VERI_IZIN] = "veri_izin", [KK_M3C_S_VERI_RED] = "veri_red", [KK_M3C_S_EAPOL] = "eapol",
	[KK_M3C_S_DHCP] = "dhcp", [KK_M3C_S_ARP] = "arp", [KK_M3C_S_ICMP] = "icmp",
	[KK_M3C_S_AUTH] = "auth", [KK_M3C_S_ASSOC] = "assoc", [KK_M3C_S_DEAUTH] = "deauth", [KK_M3C_S_DISASSOC] = "disassoc",
	[KK_M3C_S_BA] = "ba", [KK_M3C_S_ANAHTAR] = "anahtar_ekle", [KK_M3C_S_ANAHTAR_SIL] = "anahtar_sil",
	[KK_M3C_S_VARSAYILAN] = "varsayilan_anahtar", [KK_M3C_S_ANAHTAR_RED] = "anahtar_red",
	[KK_M3C_S_PS_KURULMADI] = "ps_zamanlayici_kurulmadi", [KK_M3C_S_HATA_AYIKLAMA] = "hata_ayiklama_gonderilmedi",
	[KK_M3C_S_MAC_MASKE] = "oam_mac_maskeli",
	[KK_M3C_S_TCP] = "tcp", [KK_M3C_S_UDP] = "udp",
};

void kk_m3c_say(int i)
{
	if (i >= 0 && i < KK_M3C_S_SAYI)
		atomic_inc(&kk_m3c_sayac[i]);
}

int kk_m3c_sayac_yaz(char *b, int boy)
{
	int i, n = 0;

	for (i = 0; i < KK_M3C_S_SAYI && n < boy; i++)
		n += scnprintf(b + n, boy - n, "%s%s=%d", i ? " " : "", kk_m3c_sayac_ad[i], atomic_read(&kk_m3c_sayac[i]));
	return n;
}

void kk_m3c_kendi_mac(struct net_device *dev, const unsigned char *mac)
{
	unsigned long f;

	spin_lock_irqsave(&kk_m3c_kilit, f);
	memcpy(kk_kendi, mac, ETH_ALEN);
	kk_kendi_var = is_valid_ether_addr(mac);
	if (dev)
		kk_dev = dev;
	spin_unlock_irqrestore(&kk_m3c_kilit, f);
}

void kk_m3c_bssid_ayarla(const unsigned char *bssid)
{
	unsigned long f;

	spin_lock_irqsave(&kk_m3c_kilit, f);
	memcpy(kk_bssid, bssid, ETH_ALEN);
	kk_bssid_var = is_valid_ether_addr(bssid);
	spin_unlock_irqrestore(&kk_m3c_kilit, f);
}

/* 0: a1 = onaylı BSSID ve a2 = kendi MAC (a3 NULL değilse a3 = BSSID); ayrıca *a1_bssid / *a2_kendi */
int kk_m3c_adres_denetle(const unsigned char *a1, const unsigned char *a2, const unsigned char *a3)
{
	unsigned long f;
	int r = 0;

	spin_lock_irqsave(&kk_m3c_kilit, f);
	if (!kk_bssid_var || !kk_kendi_var)
		r = -ENOENT;
	else if (!ether_addr_equal(a1, kk_bssid))
		r = -1;
	else if (!ether_addr_equal(a2, kk_kendi))
		r = -2;
	else if (a3 && !ether_addr_equal(a3, kk_bssid))
		r = -3;
	spin_unlock_irqrestore(&kk_m3c_kilit, f);
	return r;
}

int kk_m3c_bssid_mi(const unsigned char *a)
{
	unsigned long f;
	int r;

	spin_lock_irqsave(&kk_m3c_kilit, f);
	r = kk_bssid_var && ether_addr_equal(a, kk_bssid);
	spin_unlock_irqrestore(&kk_m3c_kilit, f);
	return r;
}

/* hedef IPv4 wlan0'ın (birincil) alt ağında mı */
static bool kk_alt_agda(__be32 d)
{
	struct in_device *in;
	const struct in_ifaddr *ifa;
	bool r = false;

	if (!kk_dev)
		return false;
	rcu_read_lock();
	in = __in_dev_get_rcu(kk_dev);
	if (in)
		in_dev_for_each_ifa_rcu(ifa, in)
			if (ifa->ifa_mask && !((ifa->ifa_local ^ d) & ifa->ifa_mask)) {
				r = true;
				break;
			}
	rcu_read_unlock();
	return r;
}

#ifdef CONFIG_KK_HI1102_M3C_L3_GENIS
/* G59-C: genis L3 suzgeci anahtari. 0 (varsayilan, parametre yok) = M3c listesi aynen; 1 = IPv4 unicast TCP/UDP/ICMP de gecer. */
static int kk_m3c_l3_genis;
core_param(kk_m3c_l3_genis, kk_m3c_l3_genis, int, 0444);

/* d wlan0'in yerel adreslerinden (yayin = true: alt ag yayin adreslerinden) biri mi */
static bool kk_adres_bu(__be32 d, bool yayin)
{
	struct in_device *in;
	const struct in_ifaddr *ifa;
	bool r = false;

	if (!kk_dev)
		return false;
	rcu_read_lock();
	in = __in_dev_get_rcu(kk_dev);
	if (in)
		in_dev_for_each_ifa_rcu(ifa, in)
			if (yayin ? ifa->ifa_broadcast == d : ifa->ifa_local == d) {
				r = true;
				break;
			}
	rcu_read_unlock();
	return r;
}

/* genis kip: NULL = izin. Kaynak = wlan0'in kendi IPv4'u; hedef unicast (coklu nokta / yayin / sifir ag / loopback degil) */
static const char *kk_genis_unicast(const struct iphdr *ip)
{
	if (ipv4_is_multicast(ip->daddr) || ipv4_is_lbcast(ip->daddr) || ipv4_is_zeronet(ip->daddr) || ipv4_is_loopback(ip->daddr) ||
	    kk_adres_bu(ip->daddr, true))
		return "IPv4 hedef unicast degil (genis kip)";
	if (!kk_adres_bu(ip->saddr, false))
		return "IPv4 kaynak wlan0 adresi degil (genis kip)";
	return NULL;
}
#else
#define kk_m3c_l3_genis 0
#endif

/*
 * L3 beyaz listesi. tur = ethertype (konak düzeni), l3 = ethertype'tan sonraki ilk bayt, n = kalan boy, src = 802.3 kaynak MAC.
 * NULL = izin (*sinif sayaç indeksi), değilse neden.
 */
const char *kk_m3c_l3_izinli(unsigned int tur, const unsigned char *l3, unsigned int n, const unsigned char *src, int *sinif)
{
	const struct iphdr *ip;
	unsigned int ihl;

	*sinif = -1;
	if (src && (!kk_kendi_var || !ether_addr_equal(src, kk_kendi)))
		return "kaynak MAC kendi MAC degil";
	switch (tur) {
	case ETH_P_PAE:
		*sinif = KK_M3C_S_EAPOL;
		return n >= 4 ? NULL : "EAPOL kisa";
	case ETH_P_ARP:
		*sinif = KK_M3C_S_ARP;
		if (n < 28) return "ARP kisa";
		if (l3[0] != 0 || l3[1] != 1 || l3[2] != 0x08 || l3[3] != 0x00 || l3[4] != 6 || l3[5] != 4) return "ARP Ethernet/IPv4 degil";
		if (l3[6] != 0 || (l3[7] != 1 && l3[7] != 2)) return "ARP islem 1/2 degil";
		if (!kk_kendi_var || !ether_addr_equal(l3 + 8, kk_kendi)) return "ARP gonderen MAC kendi degil";
		return NULL;
	case ETH_P_IP:
		if (n < sizeof(struct iphdr)) return "IPv4 kisa";
		ip = (const struct iphdr *)l3;
		ihl = ip->ihl * 4;
		if (ip->version != 4 || ihl < 20 || n < ihl) return "IPv4 baslik";
		if (ip->frag_off & htons(IP_MF | IP_OFFSET)) return "IPv4 parca";
		if (ip->protocol == IPPROTO_UDP) {
			const struct udphdr *u = (const void *)(l3 + ihl);

			*sinif = KK_M3C_S_DHCP;
			if (n < ihl + 8) return "UDP kisa";
			if (u->source == htons(68) && u->dest == htons(67))
				return NULL;
#ifdef CONFIG_KK_HI1102_M3C_L3_GENIS
			if (kk_m3c_l3_genis) {
				const char *g = kk_genis_unicast(ip);

				if (!g) *sinif = KK_M3C_S_UDP;
				return g;
			}
#endif
			return "UDP 68->67 (DHCP istemcisi) degil";
		}
#ifdef CONFIG_KK_HI1102_M3C_L3_GENIS
		if (kk_m3c_l3_genis && ip->protocol == IPPROTO_TCP) {
			const char *g;

			*sinif = KK_M3C_S_TCP;
			if (n < ihl + sizeof(struct tcphdr)) return "TCP kisa";
			g = kk_genis_unicast(ip);
			if (g) return g;
			return NULL;
		}
#endif
		if (ip->protocol == IPPROTO_ICMP) {
			const struct icmphdr *c = (const void *)(l3 + ihl);

			*sinif = KK_M3C_S_ICMP;
			if (n < ihl + 8) return "ICMP kisa";
			if (c->type == ICMP_ECHO && c->code == 0 && kk_alt_agda(ip->daddr))
				return NULL;
#ifdef CONFIG_KK_HI1102_M3C_L3_GENIS
			if (kk_m3c_l3_genis)
				return kk_genis_unicast(ip);
#endif
			if (c->type != ICMP_ECHO || c->code != 0) return "ICMP echo istegi degil";
			return "ICMP hedefi wlan0 alt aginda degil";
		}
		return kk_m3c_l3_genis ? "IPv4 protokol (UDP / TCP / ICMP disi)" : "IPv4 protokol (UDP-DHCP / ICMP disi)";
	}
	return "ethertype (EAPOL/ARP/IPv4 disi)";
}

/* ndo_start_xmit önü: 0 = ilet; 1 = düştü (skb serbest) */
int kk_m3c_veri_suz(struct sk_buff *skb, struct net_device *dev)
{
	const struct ethhdr *e;
	const char *neden;
	int s, n;

	if (kk_m3b_durdu()) {
		neden = "DUR";
		s = -1;
	} else if (!skb || skb->len < ETH_HLEN || skb_linearize(skb)) {
		neden = "kisa / dogrusal degil";
		s = -1;
	} else {
		e = (const struct ethhdr *)skb->data;
		neden = kk_m3c_l3_izinli(ntohs(e->h_proto), skb->data + ETH_HLEN, skb->len - ETH_HLEN, e->h_source, &s);
	}
	if (!neden) {
		atomic_inc(&kk_m3c_sayac[KK_M3C_S_VERI_IZIN]);
		kk_m3c_say(s);
		return 0;
	}
	n = atomic_inc_return(&kk_m3c_sayac[KK_M3C_S_VERI_RED]);
	if (n <= 10 || !(n % 100))
		pr_info("kk: m3c: veri DUSURULDU #%d: %s (%u B, ethertype 0x%04x); tele inmez\n", n, neden, skb ? skb->len : 0,
			(skb && skb->len >= ETH_HLEN) ? ntohs(((const struct ethhdr *)skb->data)->h_proto) : 0);
	if (skb)
		dev_kfree_skb_any(skb);
	return 1;
}
