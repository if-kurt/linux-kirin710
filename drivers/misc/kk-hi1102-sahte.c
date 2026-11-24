// SPDX-License-Identifier: GPL-2.0
/*
 * Kara Kutu G48 (Wi-Fi M2a): kk-hi1102-yukle için SAHTE Hi1102 (YALNIZ QEMU; CONFIG_KK_HI1102_YUKLE_SAHTE).
 * Cihaz imajında derlenmez. Yükleyicinin SDIO ilkelleri (enable/blksz/readb/writeb/f0_writeb/writesb/readsb/readl/
 * memcpy_fromio) buraya yönlenir; ROM protokolü kk-hi1102-rom.h durum makinesiyle (4.9 harness'iyle ortak) yanıtlanır.
 * Her CMD52/CMD53 yazması tek satır loglanır; CMD53 yazmaları 4.9 harness'iyle aynı "W53 .." biçiminde (akış
 * karşılaştırması: work/g48/wifi-m2a/es49/karsilastir.py). Sahte "çip RAM'i" D1-D4 sonda CRC32 ile loglanır.
 *
 * Hata enjeksiyonu: cmdline kk_hi1102_sahte.bozuk=<a,b,...>
 *   ROM: yanitsiz v100 surum readyyok fokilk fokson wokr6
 *   SDIO: xfer0 xfer513 xferffff (ilk yanıtın uzunluğu) gecikme150 gecikme400 (ilk yanıt N ms sonra)
 *         hazirsiz panik (QUIT sonrası genişletilmiş alan)
 *   kart: kartyok kartkimlik filtre hiz      yükleyici: kmalloc crc kapi_veri kapi_deger
 *   G49 (kip 1 kapısı): kapi_mesaj (ilk mesaj tamponunun 0. baytı), kapi_veri1 (ilk FIFO isteği veri=1),
 *         kapi_adim2 (ilk FIFO isteği tablo adımı 2)
 *   G51 (M2c-A): hazir12 (QUIT'ten 12 sn sonra hazır), geriboz (G3 geri okuma yanıtı bozuk), geriyanitsiz (ilk geri
 *         okuma READM'ine yanıt yok), efuseyok (READM 0x50000744 64 yanıtsız)
 *   G52 (M2c-A'): bcpusifir (R11 + R12 BCPU kapatma dizisinden sonra BFGX 0x300E4000..0x30171E00 READM'i 64 B sıfır;
 *         G51 cihaz sonucunun hipotez modeli: G51 sırasında GERI 3/6, A' sırasında 6/6)
 *   G53 (P-D): QUIT sonrası firmware D2H modeli (G52 cihaz ext ilk'i: int 0x05 DREADY|MFARM, msg 0x800 CREDIT_UPDATE,
 *         xfer 512, credit 0x500, comm 10 00 0c 00). int_stat ext okumasıyla temizlenir; msg F1 0x2B okunana, xfer/comm
 *         FIFO'dan xfer bayt okunana kadar kilitli. Desenler:
 *         kredi512 (ikisi de olunca int 0x04 msg 0x01 = WLAN_READY), kredipanik (ikisi de olunca int 0x04 msg 0x80),
 *         kredirerror (ilk int 0x07: RERROR), kredixfer0 / kredixfer1024 (comm 0x20) / kredihizasiz (xfer 510) /
 *         kreditoplam (comm 0x08 = 256 B, xfer 512), iletisonsuz (int 0x04 msg 0x800; her ACK'ten sonra yeniden),
 *         verisonsuz (int 0x01 xfer 512 comm 0x10; her FIFO okumasından sonra yeniden). Paket i. bayt = 3i + 0x10.
 *   G54 (M3a): m3aoam (ilk paket gerçekçi OAM: 10 00 3b a0 03 + 58 B yük; READY msg 0x19; READY ACK'inden sonra 300 ms
 *         aralıklı betik: credit 0x600 + OAM #2 (pay 100), ALLOW_SLEEP (msg 0x20), WIFI türü paket 256 B (pay 30), sonra
 *         sessiz), m3apanik (A10'da msg 0x80), m3arerror (A10'da int 0x07), m3agecersiz (A10'da ana 5 başlıklı paket),
 *         m3asonsuz (A10'da her ACK'ten sonra yeni ileti), m3auzunluk (A10'da xfer 1024 comm 0x20). M3a derlemesinde
 *         desensiz eski modelin READY'si ilk 0x2B okumasıyla temizlenir.
 *   G50 (M2c): surum2 (önek tutan ama 4.9/G49'dan farklı tam sürüm metni), sha (gömülü CPU_RAM_SCHED.bin sha256 uyuşmazlığı), crc53 (ilk VERI CMD53'ü SDIO CRC hatası -EILSEQ; ROM'a
 *         ulaşmaz), zaman53 (R6 PMU WRITEM CMD53'ü SDIO zaman aşımı -ETIMEDOUT; ROM'a ulaşmaz)
 */
#include <linux/crc32.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/unaligned.h>
#include <linux/vmalloc.h>
#include <linux/mmc/card.h>
#include <linux/random.h>
#include <crypto/sha1.h>
#include <crypto/aes.h>
#include "kk-hi1102-yukle.h"
#include "kk-hi1102-rom.h"
#include "kk-hi1102-denetim.h"
#include "kk-hi1102-tablo.h"

static char *bozuk = "";
module_param(bozuk, charp, 0444);
MODULE_PARM_DESC(bozuk, "virgulle ayrilmis hata enjeksiyonlari (yalniz QEMU)");

static struct kk_rom kk_s_rom;
static u8 *kk_s_wcpu, *kk_s_bfgx;
static int kk_s_c52, kk_s_c53, kk_s_yanit, kk_s_okuma52, kk_s_okuma53;
static ktime_t kk_s_hazir_t, kk_s_quit_t;
static bool kk_s_quit_goruldu, kk_s_ilk_xfer = true, kk_s_kapi_veri_yapildi, kk_s_kapi_deger_yapildi;
static bool kk_s_kapi_mesaj_yapildi, kk_s_kapi_veri1_yapildi, kk_s_kapi_adim2_yapildi;
static bool kk_s_crc53_yapildi;
static bool kk_s_hazir_ack;	/* G54 M3a: desensiz modelde READY ACK'lendi */

#define SK(fmt, ...) pr_info("kk: hi1102 sahte: " fmt "\n", ##__VA_ARGS__)

static bool kk_s_boz(const char *ad)
{
	const char *p = bozuk;
	size_t n = strlen(ad);

	while (p && *p) {
		if (!strncmp(p, ad, n) && (p[n] == ',' || !p[n]))
			return true;
		p = strchr(p, ',');
		if (p)
			p++;
	}
	return false;
}

static u32 kk_s_crc(const u8 *p, u32 n)
{
	return ~crc32_le(~0u, p, n);
}

int kk_hs_kart(struct kk_h_kart *o)
{
	if (kk_s_boz("kartyok"))
		return -EAGAIN;
	memset(o, 0, sizeof(*o));
	o->tip = MMC_TYPE_SDIO;
	o->vendor = o->fn_vendor = 0x0296;
	o->device = o->fn_device = kk_s_boz("kartkimlik") ? 0x1234 : 0x5347;
	o->fonksiyon = 1;
	o->fn_no = 1;
	o->saat = kk_s_boz("hiz") ? 50000000 : 400000;
	o->genislik_bit = 4;
	o->filtre_reddi = kk_s_boz("filtre") ? 1 : 0;
	o->surucu = IS_ENABLED(CONFIG_KK_HI1102_M3A);	/* G54 M3a: kk-wifi (sahte probe) bağlı sayılır */
	SK("kart (sahte): tip %u 0x%04x/0x%04x fonksiyon 1, %u Hz 4 bit, filtre reddi %u (bozuk='%s')", o->tip, o->vendor,
	   o->device, o->saat, o->filtre_reddi, bozuk);
	return 0;
}

int kk_hs_enable_func(void)
{
	kk_s_c52++;
	SK("CMD52 W #%d fn0 CCCR 0x002 IOEx |= 0x02 (sdio_enable_func)", kk_s_c52);
	return 0;
}

int kk_hs_set_block_size(unsigned int b)
{
	kk_s_c52 += 2;
	SK("CMD52 W #%d-%d fn0 FBR1 0x110 <- 0x%02x, 0x111 <- 0x%02x (sdio_set_block_size %u)", kk_s_c52 - 1, kk_s_c52,
	   b & 0xff, b >> 8, b);
	return 0;
}

static bool kk_s_int_var(void)
{
	return kk_s_rom.yanit_var && ktime_compare(ktime_get(), kk_s_hazir_t) >= 0;
}

/* ---- G53 P-D: QUIT sonrası firmware D2H modeli (yalnız kredi..., iletisonsuz, verisonsuz desenlerinde) ---- */
enum { KK_D_YOK, KK_D_KREDI512, KK_D_PANIK, KK_D_RERROR, KK_D_XFER0, KK_D_XFER1024, KK_D_HIZASIZ, KK_D_TOPLAM,
       KK_D_ILETI_SONSUZ, KK_D_VERI_SONSUZ,
       /* G54 M3a: ilk paket gerçekçi OAM (P-D cihaz başlığı 10 00 3b a0 03, yük 58 B), READY 0x19, sonra A10 betiği */
       KK_D_M3A, KK_D_M3A_PANIK, KK_D_M3A_RERROR, KK_D_M3A_GECERSIZ, KK_D_M3A_SONSUZ, KK_D_M3A_UZUNLUK };
static const char * const kk_d_ad[] = { "yok", "kredi512", "kredipanik", "kredirerror", "kredixfer0", "kredixfer1024",
					"kredihizasiz", "kreditoplam", "iletisonsuz", "verisonsuz",
					"m3aoam", "m3apanik", "m3arerror", "m3agecersiz", "m3asonsuz", "m3auzunluk" };
static struct {
	int desen;
	bool basladi, int_bekliyor, ack_bekliyor, veri_bekliyor, hazir_verildi;
	u32 int_stat, msg, xfer, credit;
	u8 comm[64];
	int ext_n, kesme_n, ack_n, veri_n, ack_bos_n, fifo_bek_n;
	u32 veri_bayt;
	u8 paket[2048];		/* G56: 512 -> 2048 (cali yuklemesi 64 + 1544 B; xfer 2048) */
	int a10_adim;		/* G54 M3a: READY'den sonraki betik adımı (0 = yok) */
	ktime_t a10_t;		/* bir sonraki adımın sunulacağı an */
} kk_s_d;

#define KK_D_M3A_ILK(d)	((d) >= KK_D_M3A)
/* G54 M3a: HCC paketi kur (5 B başlık + pad_hdr 59 = 64 B, yük pay_len B; geri kalan 0) — kk-hcc.h bayt düzeni */
static void kk_d_hcc_paket(u8 ana, u8 sira, u16 pay_len, u8 tohum)
{
	u16 w = (u16)(pay_len << 4);

	memset(kk_s_d.paket, 0, sizeof(kk_s_d.paket));
	kk_s_d.paket[0] = (u8)(ana << 4);
	kk_s_d.paket[1] = sira;
	kk_s_d.paket[2] = 59;
	kk_s_d.paket[3] = (u8)(w & 0xff);
	kk_s_d.paket[4] = (u8)(w >> 8);
	for (int i = 0; i < pay_len && 64 + i < (int)sizeof(kk_s_d.paket); i++)
		kk_s_d.paket[64 + i] = (u8)(tohum + i);
}

static void kk_d_sun(u32 st, u32 msg, u32 xfer, u8 c0, u8 c2)
{
	kk_s_d.int_stat = st;
	kk_s_d.msg = msg;
	kk_s_d.xfer = xfer;
	memset(kk_s_d.comm, 0, sizeof(kk_s_d.comm));
	kk_s_d.comm[0] = c0;
	kk_s_d.comm[2] = c2;
	kk_s_d.int_bekliyor = true;
	kk_s_d.ack_bekliyor = (st & 0x04) && msg;
	kk_s_d.veri_bekliyor = st & 0x01;
	kk_s_d.kesme_n++;
	SK("D2H #%d sunuldu (%s): int 0x%x msg 0x%x xfer %u credit 0x%08x comm %02x 00 %02x 00", kk_s_d.kesme_n,
	   kk_d_ad[kk_s_d.desen], st, msg, xfer, kk_s_d.credit, c0, c2);
}

/* ACK ve FIFO okuması yapıldıysa firmware'in bir sonraki iletisi */
static void kk_d_ilerle(void)
{
	if (kk_s_d.ack_bekliyor || kk_s_d.veri_bekliyor)
		return;
	if (KK_D_M3A_ILK(kk_s_d.desen)) {	/* G54 M3a */
		if (!kk_s_d.hazir_verildi) {
			kk_s_d.hazir_verildi = true;
			kk_d_sun(0x04, 0x19, 0, 0, 0);	/* P-D cihazı: READY | FC_ON | WAKEUP_SUCC */
		} else {
			kk_s_d.a10_adim++;		/* betikte sonraki adım 300 ms sonra */
			kk_s_d.a10_t = ktime_add_ms(ktime_get(), 300);
		}
		return;
	}
	switch (kk_s_d.desen) {
	case KK_D_KREDI512:
		if (!kk_s_d.hazir_verildi) {
			kk_s_d.hazir_verildi = true;
			kk_d_sun(0x04, 0x01, 0, 0, 0);		/* WLAN_READY */
		}
		break;
	case KK_D_PANIK:
		if (!kk_s_d.hazir_verildi) {
			kk_s_d.hazir_verildi = true;
			kk_d_sun(0x04, 0x80, 0, 0, 0);		/* DEVICE_PANIC */
		}
		break;
	case KK_D_ILETI_SONSUZ:
		kk_d_sun(0x04, 0x800, 0, 0, 0);
		break;
	case KK_D_VERI_SONSUZ:
		kk_d_sun(0x01, 0, 512, 0x10, 0);
		break;
	}
}

static void kk_d_ilk(void)
{
	kk_s_d.basladi = true;
	kk_s_d.credit = 0x00000500;			/* credit large 5 */
	for (int i = 0; i < 512; i++)
		kk_s_d.paket[i] = (u8)(3 * i + 0x10);
	switch (kk_s_d.desen) {
	case KK_D_RERROR:
		kk_d_sun(0x07, 0x800, 512, 0x10, 0x0c);
		break;
	case KK_D_XFER0:
		kk_d_sun(0x05, 0x800, 0, 0x10, 0x0c);
		break;
	case KK_D_XFER1024:
		kk_d_sun(0x05, 0x800, 1024, 0x20, 0x0c);
		break;
	case KK_D_HIZASIZ:
		kk_d_sun(0x05, 0x800, 510, 0x10, 0x0c);
		break;
	case KK_D_TOPLAM:
		kk_d_sun(0x05, 0x800, 512, 0x08, 0x0c);
		break;
	case KK_D_ILETI_SONSUZ:
		kk_d_sun(0x04, 0x800, 0, 0, 0);
		break;
	case KK_D_VERI_SONSUZ:
		kk_d_sun(0x01, 0, 512, 0x10, 0);
		break;
	case KK_D_M3A ... KK_D_M3A_UZUNLUK:		/* G54 M3a: P-D cihaz ext ilk'i + gerçekçi OAM paketi (yük 58 B) */
		kk_d_hcc_paket(1, 0, 58, 0xa0);
		kk_d_sun(0x05, 0x800, 512, 0x10, 0x0c);
		break;
	default:					/* kredi512, kredipanik: G52 cihaz ext ilk'i */
		kk_d_sun(0x05, 0x800, 512, 0x10, 0x0c);
	}
}

#if IS_ENABLED(CONFIG_KK_HI1102_M3B)
/* ---- G55 M3b (YALNIZ QEMU): firmware calisirken H2D'ye tepki (HCC-PROTOKOL §4) ----
 * SET_CUS_DTS_CALI (cfg 607) -> G56: D2H kalibrasyon yuklemesi (DMAC_MISC 13 / CALI_TO_HMAC 2, 2 paket, 4.9 tel duzeni:
 *   pay_len her pakette WLAN_LARGE_NETBUF_SIZE 1544 = {u32 packet_idx; 1540 B veri} [D src-m3a dmac_hcc_adapt.c:1045],
 *   mac_header_len 0; toplam veri OAL_WIFI_CALI_DATA_UPLOAD_LEN 1940 = 1540 + 400) -> sonra D2H WLAN_READY iletisi
 *   (4.9: custom cali sonrasi ikinci READY). G55'te yalniz READY vardi (cihazdaki Oops QEMU'da gorulmedi).
 * SCAN_REQ (WLAN_CTX/6) -> en cok 3 sahte BSS (WLAN_CRX/3 EVERY_SCAN_RESULT; SSID "kk-sahte-N", kanal 1/6/11) + SCAN_COMP (WLAN_CRX/4).
 * Kuyruk: her oge bir oncekinin ACK/FIFO okumasi bitip ext yeniden okununca sunulur. */
static struct { u8 tur, vap, kanal, n; } kk_mq[8];
static int kk_mq_bas, kk_mq_son, kk_mq_seq, kk_mq_tarama_n, kk_mq_sonuc_n;
static bool kk_mq_kredi_bekliyor;
static void kk_mq_ekle(u8 tur, u8 vap, u8 kanal, u8 n)
{
	if (kk_mq_son - kk_mq_bas >= 8)
		return;
	kk_mq[kk_mq_son % 8].tur = tur;
	kk_mq[kk_mq_son % 8].vap = vap;
	kk_mq[kk_mq_son % 8].kanal = kanal;
	kk_mq[kk_mq_son % 8].n = n;
	kk_mq_son++;
}
/* D2H HCC paketi: 5 B baslik + pad_hdr 1 + ext 58 (frw 3 + CB 19 + MAC basligi 36) = 64 B, yuk 64'ten */
static u32 kk_mq_paket(u8 nest, u8 sub, u8 vap, const u8 *cb8, const u8 *mh, unsigned int mhl, const u8 *yuk, unsigned int yl)
{
	u16 w = (u16)(yl << 4);

	memset(kk_s_d.paket, 0, sizeof(kk_s_d.paket));
	kk_s_d.paket[0] = 0x00;
	kk_s_d.paket[1] = (u8)kk_mq_seq++;
	kk_s_d.paket[2] = 1;
	kk_s_d.paket[3] = (u8)(w & 0xff);
	kk_s_d.paket[4] = (u8)(w >> 8);
	kk_s_d.paket[6] = nest;
	kk_s_d.paket[7] = sub;
	kk_s_d.paket[8] = (u8)(vap << 4);
	if (cb8)
		memcpy(kk_s_d.paket + 9, cb8, 8);
	if (mh)
		memcpy(kk_s_d.paket + 28, mh, mhl);
	memcpy(kk_s_d.paket + 64, yuk, yl);
	return ALIGN(64 + yl, 32);
}
#define KK_MQ_CALI_PAY	1544	/* WLAN_LARGE_NETBUF_SIZE (platform_spec_1102.h:262; dmac hcc_slave_tx boyu) */
#define KK_MQ_CALI_TOPLAM	1940	/* OAL_WIFI_CALI_DATA_UPLOAD_LEN = sizeof(oal_cali_param_stru) 1944 - 4 */
static int kk_mq_cali_n;
#ifdef CONFIG_KK_HI1102_M3C_QEMU
/* G56 M3c TASLAGI: en kucuk sahte AP yaniti. H2D MGMT AUTH (acik sistem, sira 1) -> D2H AUTH sira 2 durum 0;
   H2D ASSOC_REQ -> D2H ASSOC_RSP (durum 0, AID 1, 8 hiz). Yalniz kayit icin; sifreleme / EAPOL / veri yok. */
static u8 kk_m3c_sta[6], kk_m3c_bssid[6];
static int kk_m3c_auth_n, kk_m3c_asoc_n, kk_m3c_deauth_n;
/* G57 M3c (YALNIZ QEMU): sahte AP'nin arkasinda sahte ag gecidi (192.168.1.1, 02:4b:4b:5a:00:fe): DHCP sunucusu (192.168.1.77/24,
   yonlendirici + DNS 192.168.1.1, kira 600 sn), ARP yaniti, ICMP echo yaniti. H2D HOST_DRX cercevesinden (LLC/SNAP + L3) cozulur,
   D2H WLAN_DRX/RX_STA (FromDS) olarak sunulur. */
#define KK_VY_N		6
#define KK_VY_BOY	620
static struct { u8 da[6], sa[6]; u16 n; u8 b[KK_VY_BOY]; } kk_vy[KK_VY_N];
static int kk_vy_i, kk_m3c_kullanici = 1, kk_m3c_dhcp_n, kk_m3c_arp_n, kk_m3c_icmp_n, kk_m3c_eapol_n, kk_m3c_veri_n;
static const u8 kk_gw_mac[6] = { 0x02, 0x4b, 0x4b, 0x5a, 0x00, 0xfe };
static const u8 kk_gw_ip[4] = { 192, 168, 1, 1 }, kk_sta_ip[4] = { 192, 168, 1, 77 };
static const u8 kk_w_rsn[22] = { 0x30, 0x14, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04,
				 0x01, 0x00, 0x00, 0x0f, 0xac, 0x02, 0x00, 0x00 };
static void kk_w_msg1(u8 vap);
static int kk_w_durum;
#endif
static void kk_mq_sun(void)
{
	static const u8 hiz[8] = { 0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24 };
	static const u8 bssid0[6] = { 0x02, 0x4b, 0x4b, 0x5a, 0x00, 0x00 };
	u8 cb[8] = { 0 }, mh[24] = { 0 }, y[96], ssid[12];
	unsigned int yl = 0, f;
	int t, sl;
	s32 rssi;
	u32 x;

	if (kk_mq_bas == kk_mq_son || kk_s_d.int_bekliyor || kk_s_d.ack_bekliyor || kk_s_d.veri_bekliyor)
		return;
	t = kk_mq_bas++ % 8;
	switch (kk_mq[t].tur) {
	case 4:		/* CREDIT_UPDATE (bit 11): host hi-priority kredisi (hcc_credit_update_callback) = large 5 */
		kk_mq_kredi_bekliyor = false;
#ifdef CONFIG_KK_HI1102_M3C_QEMU
		/* G56 M3c (yalniz QEMU): large 32 — 5 kredide host her tur <= 3 paket yollayip guncellemeyi bekliyor (hcc_credit_bottom_value 2);
		   D2H kuyrugu tarama sonuclariyla doluyken H2D AUTH saniyelerce gecikip hmac'in ~160 ms auth zamanlayicisini kaciriyordu (k1). */
		kk_s_d.credit = 0x00002000;
		SK("M3c: D2H CREDIT_UPDATE (large 32) sunuluyor");
#else
		kk_s_d.credit = 0x00000500;
		SK("M3b: D2H CREDIT_UPDATE (large 5) sunuluyor");
#endif
		kk_d_sun(0x04, 0x800, 0, 0, 0);
		return;
	case 5: {	/* G56: CALI_TO_HMAC paketi #n: yuk {u32 idx; veri}; veri deseni (bayt ^ 0x5a) belirleyici */
		static u8 cy[KK_MQ_CALI_PAY];
		unsigned int i, ofs = (KK_MQ_CALI_PAY - 4) * kk_mq[t].n, an = min(KK_MQ_CALI_PAY - 4u, KK_MQ_CALI_TOPLAM - ofs);

		memset(cy, 0, sizeof(cy));
		put_unaligned_le32(kk_mq[t].n, cy);
		for (i = 0; i < an; i++)
			cy[4 + i] = (u8)((ofs + i) ^ 0x5a);
		x = kk_mq_paket(13, 2, 0, NULL, NULL, 0, cy, KK_MQ_CALI_PAY);
		kk_mq_cali_n++;
		SK("M3b: D2H CALI_TO_HMAC #%d (packet_idx %u, pay_len %u, anlamli veri %u B @%u, xfer %u, comm[0] %u) sunuluyor",
		   kk_mq_cali_n, kk_mq[t].n, KK_MQ_CALI_PAY, an, ofs, ALIGN(x, 512), x / 32);
		kk_d_sun(0x01, 0, ALIGN(x, 512), (u8)(x / 32), 0);	/* xfer = HISDIO_ALIGN_4_OR_BLK(1632) = 2048 */
		return;
	}
	case 1:		/* WLAN_READY */
		SK("M3b: D2H WLAN_READY (DTS cali sonrasi) sunuluyor");
		kk_d_sun(0x04, 0x01, 0, 0, 0);
		return;
	case 2:		/* EVERY_SCAN_RESULT: beacon; govde + 8 B {rssi, bss_type} */
		sl = snprintf(ssid, sizeof(ssid), "kk-sahte-%u", kk_mq[t].n);
		rssi = -40 - 10 * kk_mq[t].n;
		memset(y, 0, sizeof(y));
		y[8] = 100;
		y[10] = 0x01;
		y[11] = 0x04;
		yl = 12;
		y[yl++] = 0;
		y[yl++] = sl;
		memcpy(y + yl, ssid, sl);
		yl += sl;
		y[yl++] = 1;
		y[yl++] = 8;
		memcpy(y + yl, hiz, 8);
		yl += 8;
		y[yl++] = 3;
		y[yl++] = 1;
		y[yl++] = kk_mq[t].kanal;
#ifdef CONFIG_KK_HI1102_M3C_QEMU
		if (kk_mq[t].n == 2) {		/* G57: kk-sahte-2 WPA2-PSK (CCMP/CCMP/PSK) */
			y[10] |= 0x10;
			memcpy(y + yl, kk_w_rsn, sizeof(kk_w_rsn));
			yl += sizeof(kk_w_rsn);
		}
#endif
		put_unaligned_le32((u32)rssi, y + yl);
		y[yl + 4] = 1;
		yl += 8;
		mh[0] = 0x80;
		memset(mh + 4, 0xff, 6);
		memcpy(mh + 10, bssid0, 6);
		mh[15] = kk_mq[t].n;
		memcpy(mh + 16, mh + 10, 6);
		f = 24 + yl;
		cb[0] = kk_mq[t].vap & 0x1f;
		cb[1] = 1;
		cb[3] = 24 | (1 << 6);
		cb[4] = f & 0xff;
		cb[5] = f >> 8;
		cb[6] = kk_mq[t].vap & 0x0f;
		cb[7] = kk_mq[t].kanal;
		x = kk_mq_paket(5, 3, kk_mq[t].vap, cb, mh, 24, y, yl);
		kk_mq_sonuc_n++;
		SK("M3b: D2H tarama sonucu #%d (BSS %u, kanal %u, rssi %d, cerceve %u B, xfer %u) sunuluyor", kk_mq_sonuc_n,
		   kk_mq[t].n, kk_mq[t].kanal, rssi, f, x);
		kk_d_sun(0x01, 0, x, (u8)(x / 32), 0);
		return;
#ifdef CONFIG_KK_HI1102_M3C_QEMU
	case 6:		/* AUTH yaniti (sira 2, durum 0) */
	case 7: {	/* ASSOC yaniti (durum 0, AID 1, hizlar) */
		static const u8 kanal_n[4] = { 1, 1, 6, 11 };
		u8 kn = kanal_n[kk_m3c_bssid[5] & 3];

		memset(y, 0, sizeof(y));
		mh[0] = kk_mq[t].tur == 6 ? 0xb0 : 0x10;
		memcpy(mh + 4, kk_m3c_sta, 6);
		memcpy(mh + 10, kk_m3c_bssid, 6);
		memcpy(mh + 16, kk_m3c_bssid, 6);
		if (kk_mq[t].tur == 6) {
			y[2] = 2;		/* alg 0, sira 2, durum 0 */
			yl = 6;
		} else {
			y[0] = (kk_m3c_bssid[5] == 2) ? 0x11 : 0x01; y[1] = 0x04;	/* cap: ESS (+ Privacy kk-sahte-2) + kisa yuva (beacon ile ayni) */
			y[4] = 0x01; y[5] = 0xc0;	/* AID 1 | 0xc000 */
			yl = 6;
			y[yl++] = 1;
			y[yl++] = 8;
			memcpy(y + yl, hiz, 8);
			yl += 8;
		}
		f = 24 + yl;
		cb[0] = kk_mq[t].vap & 0x1f;
		cb[1] = 1;
		cb[3] = 24;
		cb[4] = f & 0xff;
		cb[5] = f >> 8;
		cb[6] = kk_mq[t].vap & 0x0f;
		cb[7] = kn;
		x = kk_mq_paket(5, 1, kk_mq[t].vap, cb, mh, 24, y, yl);
		SK("M3c: D2H %s (vap %u, kanal %u, cerceve %u B, xfer %u) sunuluyor", kk_mq[t].tur == 6 ? "AUTH sira 2 durum 0" :
		   "ASSOC_RSP durum 0 AID 1", kk_mq[t].vap, kn, f, x);
		kk_d_sun(0x01, 0, x, (u8)(x / 32), 0);
		return;
	}
	case 8: {	/* G57: veri yaniti (FromDS): a1 = da, a2 = BSSID, a3 = sa; govde LLC/SNAP + L3 */
		static u8 vb[KK_VY_BOY];
		int i = kk_mq[t].n % KK_VY_N;
		static const u8 kanal_n[4] = { 1, 1, 6, 11 };

		mh[0] = 0x08;
		mh[1] = 0x02;
		memcpy(mh + 4, kk_vy[i].da, 6);
		memcpy(mh + 10, kk_m3c_bssid, 6);
		memcpy(mh + 16, kk_vy[i].sa, 6);
		memcpy(vb, kk_vy[i].b, kk_vy[i].n);
		f = 24 + kk_vy[i].n;
		cb[0] = kk_mq[t].vap & 0x1f;
		cb[1] = 1;
		cb[2] = (u8)((kk_m3c_kullanici & 0x0f) << 4);
		cb[3] = 24;
		cb[4] = f & 0xff;
		cb[5] = f >> 8;
		cb[6] = (u8)((kk_mq[t].vap & 0x0f) | (1 << 4));
		cb[7] = kanal_n[kk_m3c_bssid[5] & 3];
		x = kk_mq_paket(6, 1, kk_mq[t].vap, cb, mh, 24, vb, kk_vy[i].n);
		kk_m3c_veri_n++;
		SK("M3c: D2H veri #%d (ethertype 0x%02x%02x, govde %u B, kullanici %d, xfer %u) sunuluyor", kk_m3c_veri_n, vb[6], vb[7],
		   kk_vy[i].n, kk_m3c_kullanici, x);
		kk_d_sun(0x01, 0, x, (u8)(x / 32), 0);
		return;
	}
#endif
	case 3:		/* SCAN_COMP: status 0, cookie 0 */
		memset(y, 0, 16);
		x = kk_mq_paket(5, 4, kk_mq[t].vap, NULL, NULL, 0, y, 16);
		SK("M3b: D2H SCAN_COMP (vap %u, status 0, xfer %u) sunuluyor", kk_mq[t].vap, x);
		kk_d_sun(0x01, 0, x, (u8)(x / 32), 0);
		return;
	}
}
/* tek HCC paketi: b = 64 B baslik + yuk */
static void kk_mq_h2d_tek(const u8 *b, unsigned int n)
{
	const u8 *p = b + 64;
	unsigned int i, k = 0;

	if (n < 68)
		return;
#ifdef CONFIG_KK_HI1102_M3C_QEMU
	/* G57: tel duzeni kaniti — 4.9 ofsetleriyle cozum (CONNECT_REQ 44 B, STA_SET_EDCA_REG 128 B, BA_SYNC 32 B); kullanici indeksi */
	if (b[61] == 1 && b[62] == 2 && (p[0] | (p[1] << 8)) == 484) {
		const u8 *c = p + 4;
		unsigned int cl = p[2] | (p[3] << 8), sf = 0;

		for (i = 32; i < 40 && i < cl; i++)
			sf |= c[i];
		SK("M3c: H2D CONNECT_REQ (cfg 484) us_len %u (4.9 duzeni 44): privacy %u auth %u wep_len %u mfp %u pmf %u wps %u | crypto@20: wpa %u grup %u ikili_n %u ikili0 %u akm_n %u akm0 %u | 11R dolgusu @32..39 %s | c_rssi@40 %d",
		   cl, c[0], c[1], c[2], c[17], c[18], c[19], c[20], c[21], c[22], c[23], c[28], c[29], sf ? "SIFIR DEGIL" : "sifir",
		   cl >= 41 ? (s8)c[40] : 0);
	}
	if (b[61] == 7 && b[62] == 14) {
		unsigned int sf = 0;

		for (i = 116; i < 128 && 64 + i < n; i++)
			sf |= p[i];
		SK("M3c: H2D STA_SET_EDCA_REG (WLAN_CTX 14) yuk %u B (4.9 duzeni 128): vap %u tip %u kullanici %u | edca[0] @4: idx %u cwmin %u cwmax %u aifsn %u txop %u | WMMAC dolgusu @116..127 %s",
		   n - 64, p[0], p[1], p[2] | (p[3] << 8), get_unaligned_le32(p + 4), get_unaligned_le32(p + 8), get_unaligned_le32(p + 12),
		   get_unaligned_le32(p + 16), get_unaligned_le32(p + 20), sf ? "SIFIR DEGIL" : "sifir");
	}
	if (b[61] == 7 && b[62] == 4)
		SK("M3c: H2D BA_SYNC (WLAN_CTX 4) yuk %u B (4.9 duzeni 32)", n - 64);
	if (b[61] == 7 && b[62] == 1 && n >= 66) {
		kk_m3c_kullanici = p[0] | (p[1] << 8);
		SK("M3c: H2D ADD_USER: kullanici indeksi %d (D2H veri ta_user_idx)", kk_m3c_kullanici);
	}
	if (b[61] == 1 && b[62] == 2 && (p[0] | (p[1] << 8)) == 115 && n >= 69 && p[4] == 1 && kk_m3c_bssid[5] == 2 && !kk_w_durum)
		kk_w_msg1(b[63] >> 4);
#endif
	if (b[61] == 1 && b[62] == 2 && (p[0] | (p[1] << 8)) == 607) {
		SK("M3b: H2D SET_CUS_DTS_CALI (607) -> CALI_TO_HMAC x2 + WLAN_READY kuyrukta (G56)");
		kk_mq_ekle(5, 0, 0, 0);
		kk_mq_ekle(5, 0, 0, 1);
		kk_mq_ekle(1, 0, 0, 0);
	}
	if (b[61] == 7 && b[62] == 6 && n >= 64 + 520) {
		kk_mq_tarama_n++;
		SK("M3b: H2D SCAN_REQ #%d cozum: bss_type %u scan_type %u (%s) ssid_num %u probe_per_kanal %u kanal_sayisi %u scan_time %u func 0x%02x vap %u",
		   kk_mq_tarama_n, p[0], p[1], p[1] == 0 ? "PASIF" : "AKTIF", p[3], p[313], p[496], p[498] | (p[499] << 8),
		   p[502], p[503]);
		for (i = 0; i < p[496] && i < 43; i++)
			SK("M3b: H2D SCAN_REQ #%d kanal %u: num %u band %u bw %u idx %u", kk_mq_tarama_n, i, p[324 + 4 * i],
			   p[325 + 4 * i], p[326 + 4 * i], p[327 + 4 * i]);
		for (i = 0; i < p[496] && i < 43 && k < 3; i++)
			if (p[325 + 4 * i] == 0 && (p[324 + 4 * i] == 1 || p[324 + 4 * i] == 6 || p[324 + 4 * i] == 11))
				kk_mq_ekle(2, b[63] >> 4, p[324 + 4 * i], ++k);
		kk_mq_ekle(3, b[63] >> 4, 0, 0);
	}
}
#ifdef CONFIG_KK_HI1102_M3C_QEMU
/* ---- G57 M3c (YALNIZ QEMU): sahte WPA2-PSK AP (kk-sahte-2, kanal 6) — 4 yollu el sikisma (AP tarafi) ----
 * PMK = PBKDF2-HMAC-SHA1(parola, SSID, 4096, 32); PTK = PRF-384(PMK, "Pairwise key expansion", min/max(AA,SPA) || min/max(ANonce,SNonce));
 * MIC = HMAC-SHA1-128(KCK) (anahtar tanimlayici surum 2); msg3 anahtar verisi = RSN IE + GTK KDE, AES anahtar sarma (RFC 3394, KEK).
 * Sahte parola/SSID yalniz QEMU'dadir (gercek ag bilgisi degil). Anahtarlar basilmaz; yalniz MIC tuttu/tutmadi ve adim satiri. */
#define KK_W_SSID	"kk-sahte-2"
#define KK_W_PAROLA	"kk-sahte-parola-2"
static u8 kk_w_pmk[32], kk_w_ptk[48], kk_w_anonce[32], kk_w_gtk[16];
static int kk_w_pmk_var;	/* kk_w_durum: 0 yok, 1 msg1 gitti, 2 msg3 gitti, 3 tamam */
static u64 kk_w_replay;
static void kk_sha1(const u8 *const *v, const unsigned int *l, int n, u8 *out)
{
	u32 h[SHA1_DIGEST_WORDS], w[SHA1_WORKSPACE_WORDS];
	u8 blk[64];
	unsigned int bl = 0, i, j;
	u64 top = 0;

	sha1_init(h);
	for (i = 0; i < n; i++)
		for (j = 0; j < l[i]; j++) {
			blk[bl++] = v[i][j];
			top++;
			if (bl == 64) { sha1_transform(h, (const char *)blk, w); bl = 0; }
		}
	blk[bl++] = 0x80;
	if (bl > 56) { memset(blk + bl, 0, 64 - bl); sha1_transform(h, (const char *)blk, w); bl = 0; }
	memset(blk + bl, 0, 56 - bl);
	put_unaligned_be64(top * 8, blk + 56);
	sha1_transform(h, (const char *)blk, w);
	for (i = 0; i < 5; i++)
		put_unaligned_be32(h[i], out + 4 * i);
}
static void kk_hmac_sha1(const u8 *k, unsigned int kl, const u8 *const *v, const unsigned int *l, int n, u8 *out)
{
	u8 ip[64], op[64], ic[20];
	const u8 *vv[8];
	unsigned int ll[8], i;

	memset(ip, 0, 64);
	memcpy(ip, k, min(kl, 64u));
	memcpy(op, ip, 64);
	for (i = 0; i < 64; i++) { ip[i] ^= 0x36; op[i] ^= 0x5c; }
	vv[0] = ip; ll[0] = 64;
	for (i = 0; i < n && i < 7; i++) { vv[i + 1] = v[i]; ll[i + 1] = l[i]; }
	kk_sha1(vv, ll, n + 1, ic);
	vv[0] = op; ll[0] = 64; vv[1] = ic; ll[1] = 20;
	kk_sha1(vv, ll, 2, out);
}
static void kk_w_pmk_hesapla(void)
{
	u8 u[20], t[20], sx[40], blok;
	const u8 *v[1];
	unsigned int l[1], i, j, sl = strlen(KK_W_SSID);

	for (blok = 1; blok <= 2; blok++) {
		memcpy(sx, KK_W_SSID, sl);
		sx[sl] = 0; sx[sl + 1] = 0; sx[sl + 2] = 0; sx[sl + 3] = blok;
		v[0] = sx; l[0] = sl + 4;
		kk_hmac_sha1((const u8 *)KK_W_PAROLA, strlen(KK_W_PAROLA), v, l, 1, u);
		memcpy(t, u, 20);
		for (i = 1; i < 4096; i++) {
			v[0] = u; l[0] = 20;
			kk_hmac_sha1((const u8 *)KK_W_PAROLA, strlen(KK_W_PAROLA), v, l, 1, u);
			for (j = 0; j < 20; j++) t[j] ^= u[j];
		}
		memcpy(kk_w_pmk + 20 * (blok - 1), t, blok == 1 ? 20 : 12);
	}
	kk_w_pmk_var = 1;
}
static void kk_w_ptk_hesapla(const u8 *aa, const u8 *spa, const u8 *an, const u8 *sn)
{
	static const char et[] = "Pairwise key expansion";
	u8 b[76], z = 0, i, o[20 * 3];
	const u8 *v[4];
	unsigned int l[4], k;

	memcpy(b, memcmp(aa, spa, 6) < 0 ? aa : spa, 6);
	memcpy(b + 6, memcmp(aa, spa, 6) < 0 ? spa : aa, 6);
	memcpy(b + 12, memcmp(an, sn, 32) < 0 ? an : sn, 32);
	memcpy(b + 44, memcmp(an, sn, 32) < 0 ? sn : an, 32);
	for (k = 0; k < 3; k++) {
		i = (u8)k;
		v[0] = (const u8 *)et; l[0] = sizeof(et) - 1;
		v[1] = &z; l[1] = 1;
		v[2] = b; l[2] = 76;
		v[3] = &i; l[3] = 1;
		kk_hmac_sha1(kk_w_pmk, 32, v, l, 4, o + 20 * k);
	}
	memcpy(kk_w_ptk, o, 48);
}
static void kk_w_mic(u8 *e, unsigned int el)	/* e = 802.1X basligindan itibaren; MIC alani (81..96) 0 iken hesaplanip yazilir */
{
	u8 d[20];
	const u8 *v[1] = { e };
	unsigned int l[1] = { el };

	memset(e + 81, 0, 16);
	kk_hmac_sha1(kk_w_ptk, 16, v, l, 1, d);
	memcpy(e + 81, d, 16);
}
static int kk_w_mic_tutar(const u8 *e, unsigned int el)
{
	static u8 c[512];
	u8 d[20];
	const u8 *v[1] = { c };
	unsigned int l[1] = { el };

	if (el > sizeof(c))
		return 0;
	memcpy(c, e, el);
	memset(c + 81, 0, 16);
	kk_hmac_sha1(kk_w_ptk, 16, v, l, 1, d);
	return !memcmp(d, e + 81, 16);
}
static void kk_aes_sar(const u8 *kek, const u8 *p, unsigned int n8, u8 *out)	/* RFC 3394; n8 = 64 bitlik blok sayisi */
{
	struct crypto_aes_ctx ctx;
	u8 a[8], bb[16], r[64];
	unsigned int i, j;
	u64 t;

	aes_expandkey(&ctx, kek, 16);
	memset(a, 0xa6, 8);
	memcpy(r, p, 8 * n8);
	for (j = 0; j < 6; j++)
		for (i = 0; i < n8; i++) {
			memcpy(bb, a, 8);
			memcpy(bb + 8, r + 8 * i, 8);
			aes_encrypt(&ctx, bb, bb);
			t = (u64)n8 * j + i + 1;
			put_unaligned_be64(get_unaligned_be64(bb) ^ t, a);
			memcpy(r + 8 * i, bb + 8, 8);
		}
	memcpy(out, a, 8);
	memcpy(out + 8, r, 8 * n8);
	memzero_explicit(&ctx, sizeof(ctx));
}
static u8 *kk_vy_al(u8 vap, const u8 *da, const u8 *sa, u16 et, unsigned int n);
/* EAPOL-Key (RSN) cercevesi: 4 B 802.1X + 95 B + veri */
static void kk_w_gonder(u8 vap, u16 bilgi, const u8 *veri, unsigned int vl, int mic)
{
	u8 *e = kk_vy_al(vap, kk_m3c_sta, kk_m3c_bssid, 0x888e, 99 + vl);

	if (!e)
		return;
	e[0] = 2; e[1] = 3; put_unaligned_be16(95 + vl, e + 2);
	e[4] = 2; put_unaligned_be16(bilgi, e + 5); put_unaligned_be16(16, e + 7);
	put_unaligned_be64(++kk_w_replay, e + 9);
	memcpy(e + 17, kk_w_anonce, 32);
	put_unaligned_be16(vl, e + 97);
	if (vl)
		memcpy(e + 99, veri, vl);
	if (mic)
		kk_w_mic(e, 99 + vl);
}
static void kk_w_msg1(u8 vap)
{
	if (!kk_w_pmk_var)
		kk_w_pmk_hesapla();
	get_random_bytes(kk_w_anonce, 32);
	get_random_bytes(kk_w_gtk, 16);
	kk_w_durum = 1;
	kk_w_gonder(vap, 0x008a, NULL, 0, 0);
	SK("M3c: WPA2 sahte AP: D2H EAPOL msg1 (ANonce) kuyrukta (replay %llu)", kk_w_replay);
}
static void kk_m3c_eapol(u8 vap, const u8 *sa, const u8 *e, unsigned int n)
{
	u16 bilgi;
	u8 duz[48], sar[56];

	if (n < 99 || e[1] != 3 || e[4] != 2)
		return;
	bilgi = get_unaligned_be16(e + 5);
	if (kk_w_durum == 1 && (bilgi & 0x0108) == 0x0108 && !(bilgi & 0x0200)) {	/* msg2: ikili + MIC */
		kk_w_ptk_hesapla(kk_m3c_bssid, sa, kk_w_anonce, e + 17);
		if (!kk_w_mic_tutar(e, min_t(unsigned int, n, 4 + get_unaligned_be16(e + 2)))) {
			SK("M3c: WPA2 sahte AP: msg2 MIC TUTMADI (parola/PTK) — el sikisma durdu");
			kk_w_durum = 0;
			return;
		}
		SK("M3c: WPA2 sahte AP: msg2 MIC tuttu (SNonce alindi, PTK turetildi); msg3 (RSN IE + GTK KDE, AES sarma) kuyrukta");
		memcpy(duz, kk_w_rsn, 22);
		duz[22] = 0xdd; duz[23] = 22; duz[24] = 0x00; duz[25] = 0x0f; duz[26] = 0xac; duz[27] = 0x01;
		duz[28] = 0x01; duz[29] = 0;
		memcpy(duz + 30, kk_w_gtk, 16);
		duz[46] = 0xdd; duz[47] = 0;
		kk_aes_sar(kk_w_ptk + 16, duz, 6, sar);
		kk_w_gonder(vap, 0x13ca, sar, 56, 1);
		kk_w_durum = 2;
	} else if (kk_w_durum == 2 && (bilgi & 0x0308) == 0x0308) {	/* msg4: ikili + MIC + guvenli */
		if (!kk_w_mic_tutar(e, min_t(unsigned int, n, 4 + get_unaligned_be16(e + 2)))) {
			SK("M3c: WPA2 sahte AP: msg4 MIC TUTMADI");
			return;
		}
		kk_w_durum = 3;
		SK("M3c: WPA2 sahte AP: msg4 MIC tuttu — 4 yollu el sikisma TAMAM (PTK + GTK kuruldu kabul edilir)");
	} else {
		SK("M3c: WPA2 sahte AP: beklenmeyen EAPOL-Key (bilgi 0x%04x, durum %d)", bilgi, kk_w_durum);
	}
}
static u16 kk_ip_sag(const u8 *p, unsigned int n)
{
	u32 t = 0;
	unsigned int i;

	for (i = 0; i + 1 < n; i += 2)
		t += (p[i] << 8) | p[i + 1];
	if (n & 1)
		t += p[n - 1] << 8;
	while (t >> 16)
		t = (t & 0xffff) + (t >> 16);
	return (u16)~t;
}
/* yanit: govde = LLC/SNAP + ethertype + l3 (n B) */
static u8 *kk_vy_al(u8 vap, const u8 *da, const u8 *sa, u16 et, unsigned int n)
{
	int i = kk_vy_i++ % KK_VY_N;

	if (8 + n > KK_VY_BOY)
		return NULL;
	memcpy(kk_vy[i].da, da, 6);
	memcpy(kk_vy[i].sa, sa, 6);
	kk_vy[i].b[0] = 0xaa; kk_vy[i].b[1] = 0xaa; kk_vy[i].b[2] = 0x03;
	kk_vy[i].b[3] = kk_vy[i].b[4] = kk_vy[i].b[5] = 0;
	kk_vy[i].b[6] = et >> 8; kk_vy[i].b[7] = et & 0xff;
	kk_vy[i].n = 8 + n;
	memset(kk_vy[i].b + 8, 0, n);
	kk_mq_ekle(8, vap, 0, (u8)i);
	return kk_vy[i].b + 8;
}
static void kk_ip_baslik(u8 *ip, unsigned int toplam, u8 proto, const u8 *src, const u8 *dst)
{
	ip[0] = 0x45; ip[2] = toplam >> 8; ip[3] = toplam & 0xff; ip[8] = 64; ip[9] = proto;
	memcpy(ip + 12, src, 4); memcpy(ip + 16, dst, 4);
	ip[10] = ip[11] = 0;
	put_unaligned_be16(kk_ip_sag(ip, 20), ip + 10);
}
static void kk_m3c_dhcp(u8 vap, const u8 *sa, const u8 *ip, unsigned int n)
{
	static const u8 bc[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff }, bcip[4] = { 255, 255, 255, 255 };
	const u8 *d = ip + 28, *o;
	unsigned int dl = n - 28, k;
	u8 tip = 0, *y, *dy;
	bool yayin;

	if (n < 28 + 240 || d[0] != 1 || get_unaligned_be32(d + 236) != 0x63825363)
		return;
	for (o = d + 240; o < d + dl && o[0] != 255; o += o[0] ? 2 + o[1] : 1)
		if (o[0] == 53 && o[1] == 1)
			tip = o[2];
	kk_m3c_dhcp_n++;
	SK("M3c: H2D DHCP #%d istemci iletisi %u (%s), xid %08x, bayrak 0x%04x", kk_m3c_dhcp_n, tip,
	   tip == 1 ? "DISCOVER" : tip == 3 ? "REQUEST" : tip == 7 ? "RELEASE" : tip == 4 ? "DECLINE" : "?",
	   get_unaligned_be32(d + 4), get_unaligned_be16(d + 10));
	if (tip != 1 && tip != 3)
		return;
	yayin = get_unaligned_be16(d + 10) & 0x8000;
	y = kk_vy_al(vap, yayin ? bc : d + 28, kk_gw_mac, 0x0800, 28 + 240 + 40);	/* secenekler 34 B + dolgu */
	if (!y)
		return;
	dy = y + 28;
	dy[0] = 2; dy[1] = 1; dy[2] = 6;
	memcpy(dy + 4, d + 4, 4);		/* xid */
	memcpy(dy + 10, d + 10, 2);		/* bayrak */
	memcpy(dy + 16, kk_sta_ip, 4);		/* yiaddr */
	memcpy(dy + 20, kk_gw_ip, 4);		/* siaddr */
	memcpy(dy + 28, d + 28, 16);		/* chaddr */
	put_unaligned_be32(0x63825363, dy + 236);
	k = 240;
	dy[k++] = 53; dy[k++] = 1; dy[k++] = tip == 1 ? 2 : 5;
	dy[k++] = 54; dy[k++] = 4; memcpy(dy + k, kk_gw_ip, 4); k += 4;
	dy[k++] = 51; dy[k++] = 4; put_unaligned_be32(600, dy + k); k += 4;
	dy[k++] = 1; dy[k++] = 4; dy[k++] = 255; dy[k++] = 255; dy[k++] = 255; dy[k++] = 0;
	dy[k++] = 3; dy[k++] = 4; memcpy(dy + k, kk_gw_ip, 4); k += 4;
	dy[k++] = 6; dy[k++] = 4; memcpy(dy + k, kk_gw_ip, 4); k += 4;
	dy[k++] = 255;
	put_unaligned_be16(67, y + 20); put_unaligned_be16(68, y + 22); put_unaligned_be16(8 + 280, y + 24);
	kk_ip_baslik(y, 28 + 280, 17, kk_gw_ip, yayin ? bcip : kk_sta_ip);
	SK("M3c: D2H DHCP %s (yiaddr 192.168.1.77/24, yonlendirici/DNS 192.168.1.1, kira 600 sn, %s) kuyrukta",
	   tip == 1 ? "OFFER" : "ACK", yayin ? "yayin" : "tek noktali");
}
/* G60-2 L3_GENIS (YALNIZ QEMU): sahte ag gecidi her IPv4 unicast hedefe yanit verir (yonlendirici gibi).
   UDP: dport 53 -> sahte DNS (A = 192.0.2.80; AAAA/diger = bos NOERROR), baska UDP (67 haric) -> yanki. TCP: durumsuz minimal sunucu
   (SYN -> SYN|ACK; ilk veri -> "KK-TCP-YANIT\n" + FIN; FIN -> ACK). ICMP echo her hedefe (asagida). */
static int kk_m3c_udp_n, kk_m3c_tcp_n;
static u16 kk_tcp_cks(const u8 *src, const u8 *dst, const u8 *t, unsigned int tl)
{
	u8 b[12 + 64];

	if (tl > 64)
		return 0;
	memcpy(b, src, 4); memcpy(b + 4, dst, 4);
	b[8] = 0; b[9] = 6; b[10] = tl >> 8; b[11] = tl & 0xff;
	memcpy(b + 12, t, tl);
	return kk_ip_sag(b, 12 + tl);
}
static void kk_m3c_udp(u8 vap, const u8 *sa, const u8 *l3, unsigned int ln)
{
	const u8 *u = l3 + 20, *q;
	unsigned int tl = min_t(unsigned int, get_unaligned_be16(l3 + 2), ln), dport = get_unaligned_be16(u + 2), pl, qe, rl;
	u8 *y;

	if (l3[0] != 0x45 || tl < 28)
		return;
	pl = tl - 28;
	kk_m3c_udp_n++;
	if (kk_m3c_udp_n <= 5 || !(kk_m3c_udp_n % 50))
		SK("M3c: H2D UDP #%d %u.%u.%u.%u:%u -> %u.%u.%u.%u:%u (%u B)", kk_m3c_udp_n, l3[12], l3[13], l3[14], l3[15],
		   get_unaligned_be16(u), l3[16], l3[17], l3[18], l3[19], dport, pl);
	if (dport == 53) {
		q = u + 8;
		if (pl < 17)
			return;
		for (qe = 12; qe < pl && q[qe]; qe += q[qe] + 1)
			;
		qe += 5;	/* sifir bayti + qtype + qclass */
		if (qe > pl || qe + 16 > 400)
			return;
		rl = qe + (get_unaligned_be16(q + qe - 4) == 1 ? 16 : 0);
		y = kk_vy_al(vap, sa, kk_gw_mac, 0x0800, 28 + rl);
		if (!y)
			return;
		kk_ip_baslik(y, 28 + rl, 17, l3 + 16, l3 + 12);
		memcpy(y + 20, u + 2, 2); memcpy(y + 22, u, 2);
		put_unaligned_be16(8 + rl, y + 24);
		memcpy(y + 28, q, qe);
		y[30] = 0x81; y[31] = 0x80; y[32] = 0; y[33] = 1; y[34] = 0; y[35] = rl > qe ? 1 : 0;	/* QR RD RA, qd 1, an 0/1 */
		y[36] = y[37] = y[38] = y[39] = 0;	/* ns/ar 0 (EDNS OPT atilir) */
		if (rl > qe) {
			u8 *a = y + 28 + qe;

			a[0] = 0xc0; a[1] = 0x0c; a[3] = 1; a[5] = 1; a[9] = 60; a[11] = 4;
			a[12] = 192; a[13] = 0; a[14] = 2; a[15] = 80;
		}
		SK("M3c: D2H DNS yaniti (%s) kuyrukta", rl > qe ? "A 192.0.2.80" : "bos");
		return;
	}
	if (dport == 67 || pl > 400)
		return;
	y = kk_vy_al(vap, sa, kk_gw_mac, 0x0800, tl);
	if (!y)
		return;
	kk_ip_baslik(y, tl, 17, l3 + 16, l3 + 12);
	memcpy(y + 20, u + 2, 2); memcpy(y + 22, u, 2);
	put_unaligned_be16(8 + pl, y + 24);
	memcpy(y + 28, u + 8, pl);
}
static void kk_m3c_tcp(u8 vap, const u8 *sa, const u8 *l3, unsigned int ln)
{
	static const u8 yan[] = "KK-TCP-YANIT\n";
	const u8 *t = l3 + 20;
	unsigned int tl = min_t(unsigned int, get_unaligned_be16(l3 + 2), ln), off = (t[12] >> 4) * 4, pl, yl = 0;
	u32 seq = get_unaligned_be32(t + 4), ack = get_unaligned_be32(t + 8), s2, a2;
	u8 fl = t[13], f2, *y;

	if (l3[0] != 0x45 || tl < 40 || off < 20 || tl < 20 + off)
		return;
	pl = tl - 20 - off;
	kk_m3c_tcp_n++;
	if (kk_m3c_tcp_n <= 8 || !(kk_m3c_tcp_n % 50))
		SK("M3c: H2D TCP #%d %u.%u.%u.%u:%u -> %u.%u.%u.%u:%u bayrak 0x%02x (%u B)", kk_m3c_tcp_n, l3[12], l3[13], l3[14], l3[15],
		   get_unaligned_be16(t), l3[16], l3[17], l3[18], l3[19], get_unaligned_be16(t + 2), fl, pl);
	if (fl & 4)
		return;
	if ((fl & 2) && !(fl & 0x10)) {
		f2 = 0x12; s2 = 0x4b4b0000; a2 = seq + 1;
	} else if (fl & 1) {
		f2 = 0x10; s2 = ack; a2 = seq + pl + 1;
	} else if (pl) {
		f2 = 0x19; s2 = ack; a2 = seq + pl; yl = sizeof(yan) - 1;
	} else {
		return;
	}
	y = kk_vy_al(vap, sa, kk_gw_mac, 0x0800, 40 + yl);
	if (!y)
		return;
	kk_ip_baslik(y, 40 + yl, 6, l3 + 16, l3 + 12);
	memcpy(y + 20, t + 2, 2); memcpy(y + 22, t, 2);
	put_unaligned_be32(s2, y + 24); put_unaligned_be32(a2, y + 28);
	y[32] = 0x50; y[33] = f2; y[34] = 0x16; y[35] = 0xd0;	/* ofset 5, pencere 5840 */
	memcpy(y + 40, yan, yl);
	put_unaligned_be16(kk_tcp_cks(l3 + 16, l3 + 12, y + 20, 20 + yl), y + 36);
}
static void kk_m3c_h2d_veri(const u8 *b, unsigned int n)
{
	const u8 *g = b + 64, *l3 = b + 72, *sa = b + 38;
	unsigned int gl = n - 64, ln;
	u8 vap = b[8] >> 4, *y;
	u16 et;

	if (gl < 8 || g[0] != 0xaa || g[1] != 0xaa || g[2] != 0x03) {
		SK("M3c: H2D HOST_DRX veri (fc 0x%02x, govde %u) LLC/SNAP degil — yanit yok", b[28], gl);
		return;
	}
	et = (g[6] << 8) | g[7];
	ln = gl - 8;
	SK("M3c: H2D HOST_DRX veri fc 0x%02x fc1 0x%02x ethertype 0x%04x govde %u B", b[28], b[29], et, gl);
	if (et == 0x0806 && ln >= 28 && l3[7] == 1) {
		kk_m3c_arp_n++;
		SK("M3c: H2D ARP #%d istek hedef %u.%u.%u.%u (gonderen %u.%u.%u.%u)", kk_m3c_arp_n, l3[24], l3[25], l3[26], l3[27],
		   l3[14], l3[15], l3[16], l3[17]);
		if (memcmp(l3 + 24, kk_gw_ip, 4) || !memcmp(l3 + 14, "\0\0\0\0", 4))
			return;
		y = kk_vy_al(vap, sa, kk_gw_mac, 0x0806, 28);
		if (!y)
			return;
		memcpy(y, l3, 6); y[7] = 2;
		memcpy(y + 8, kk_gw_mac, 6); memcpy(y + 14, kk_gw_ip, 4);
		memcpy(y + 18, l3 + 8, 6); memcpy(y + 24, l3 + 14, 4);
		SK("M3c: D2H ARP yaniti (192.168.1.1 is-at ag gecidi) kuyrukta");
	} else if (et == 0x0800 && ln >= 28 && l3[9] == 17 && get_unaligned_be16(l3 + 22) == 67) {
		kk_m3c_dhcp(vap, sa, l3, ln);
	} else if (et == 0x0800 && ln >= 28 && l3[9] == 1 && l3[20] == 8) {
		unsigned int tl = min_t(unsigned int, get_unaligned_be16(l3 + 2), ln);

		kk_m3c_icmp_n++;
		SK("M3c: H2D ICMP #%d echo istegi %u.%u.%u.%u -> %u.%u.%u.%u (%u B)", kk_m3c_icmp_n, l3[12], l3[13], l3[14], l3[15],
		   l3[16], l3[17], l3[18], l3[19], tl);
		if (tl < 28)	/* G60-2: her hedefe yanit (sahte ag gecidi yonlendirici gibi); G59'da yalniz 192.168.1.1 */
			return;
		y = kk_vy_al(vap, sa, kk_gw_mac, 0x0800, tl);
		if (!y)
			return;
		memcpy(y, l3, tl);
		memcpy(y + 12, l3 + 16, 4); memcpy(y + 16, l3 + 12, 4);
		y[10] = y[11] = 0; y[8] = 64;
		put_unaligned_be16(kk_ip_sag(y, 20), y + 10);
		y[20] = 0; y[22] = y[23] = 0;
		put_unaligned_be16(kk_ip_sag(y + 20, tl - 20), y + 22);
		SK("M3c: D2H ICMP echo yaniti kuyrukta");
	} else if (et == 0x0800 && ln >= 28 && l3[9] == 17) {
		kk_m3c_udp(vap, sa, l3, ln);
	} else if (et == 0x0800 && ln >= 40 && l3[9] == 6) {
		kk_m3c_tcp(vap, sa, l3, ln);
	} else if (et == 0x888e) {
		kk_m3c_eapol_n++;
		SK("M3c: H2D EAPOL #%d (%u B)", kk_m3c_eapol_n, ln);
		kk_m3c_eapol(vap, sa, l3, ln);
	}
}
/* G56 M3c: cerceve paketi (pad_hdr 1, ext 58 = frw 3 + mac_tx_ctl_cut 19 + MAC basligi 36): frw b[6..8], FC b[28], govde b[64] */
static void kk_m3c_h2d_cerceve(const u8 *b, unsigned int n)
{
	u8 fc = b[28], vap = b[8] >> 4;

	if (b[6] == 2) {
		kk_m3c_h2d_veri(b, n);
		return;
	}
	if (b[6] != 7)
		return;
	SK("M3c: H2D frw 7/%u cerceve fc 0x%02x vap %u govde %u B", b[7], fc, vap, n - 64);
	if (b[7] != 8)
		return;
	if (fc == 0xb0 && n >= 64 + 6 && b[64] == 0 && b[65] == 0 && b[66] == 1) {
		memcpy(kk_m3c_bssid, b + 32, 6);
		memcpy(kk_m3c_sta, b + 38, 6);
		kk_w_durum = 0;
		kk_m3c_auth_n++;
		SK("M3c: H2D AUTH #%d (acik sistem, sira 1) -> AUTH yaniti kuyrukta", kk_m3c_auth_n);
		kk_mq_ekle(6, vap, 0, 0);
	} else if (fc == 0x00 || fc == 0x20) {
		kk_m3c_asoc_n++;
		SK("M3c: H2D %s #%d -> ASSOC_RSP kuyrukta", fc ? "REASSOC_REQ" : "ASSOC_REQ", kk_m3c_asoc_n);
		kk_mq_ekle(7, vap, 0, 0);
	} else if (fc == 0xc0 || fc == 0xa0) {
		kk_m3c_deauth_n++;
		SK("M3c: H2D %s #%d (neden %u) — yanit yok", fc == 0xc0 ? "DEAUTH" : "DISASSOC", kk_m3c_deauth_n, b[64] | (b[65] << 8));
	}
}
#endif
/* H2D aktarimi: toplanmis (scatt) aktarimda paketler 32 B hizali; HCC basligi b0 = 0x00 (WIFI), pad_hdr 56 (ext 3) */
static void kk_mq_h2d(const u8 *b, unsigned int n)
{
	unsigned int o = 0, pl;

	/* gercek firmware tamponlari bosaldikca CREDIT_UPDATE gonderir; sahte her H2D veri aktariminda bir tane (kuyrukta yoksa) */
	if (n > 64 && !kk_mq_kredi_bekliyor) {
		kk_mq_kredi_bekliyor = true;
		kk_mq_ekle(4, 0, 0, 0);
	}
	while (o + 68 <= n) {
		if (b[o] == 0x00 && b[o + 2] == 56 && (b[o + 61] == 1 || b[o + 61] == 7)) {
			pl = (b[o + 3] | (b[o + 4] << 8)) >> 4;
			SK("H2D HCC paketi @%u: frw %u/%u vap %u pay_len %u", o, b[o + 61], b[o + 62], b[o + 63] >> 4, pl);
			kk_mq_h2d_tek(b + o, min(n - o, 64 + pl));
			o += ALIGN(64 + pl, 32);
#ifdef CONFIG_KK_HI1102_M3C_QEMU
		} else if (b[o] == 0x00 && b[o + 2] == 1 && (b[o + 6] == 2 || b[o + 6] == 7)) {
			pl = (b[o + 3] | (b[o + 4] << 8)) >> 4;
			SK("H2D HCC cerceve paketi @%u: frw %u/%u vap %u pay_len %u", o, b[o + 6], b[o + 7], b[o + 8] >> 4, pl);
			kk_m3c_h2d_cerceve(b + o, min(n - o, 64 + pl));
			o += ALIGN(64 + pl, 32);
#endif
		} else {
			o += 32;
		}
	}
}
#endif

/* G54 M3a: READY'den sonraki betik (her adım bir öncekinin ACK/FIFO'su bittikten 300 ms sonra sunulur) */
static void kk_d_a10_sun(void)
{
	int a = kk_s_d.a10_adim;

	switch (kk_s_d.desen) {
	case KK_D_M3A:		/* credit değişimi + OAM #2; ALLOW_SLEEP iletisi; WIFI türü paket (256 B); sonra sessiz */
		if (a == 1) {
			kk_s_d.credit = 0x00000600;
			kk_d_hcc_paket(1, 1, 100, 0x40);
			kk_d_sun(0x05, 0x800, 512, 0x10, 0);
		} else if (a == 2) {
			kk_d_sun(0x04, 0x20, 0, 0, 0);
		} else if (a == 3) {
			kk_d_hcc_paket(0, 2, 30, 0x60);
			kk_d_sun(0x01, 0, 256, 0x08, 0);
		}
		break;
	case KK_D_M3A_PANIK:
		if (a == 1)
			kk_d_sun(0x04, 0x80, 0, 0, 0);
		break;
	case KK_D_M3A_RERROR:
		if (a == 1)
			kk_d_sun(0x07, 0x800, 512, 0x10, 0);
		break;
	case KK_D_M3A_GECERSIZ:	/* ana 5 (>= HCC_ACTION_TYPE_BUTT) -> başlık geçersiz; sonra sessiz */
		if (a == 1) {
			kk_d_hcc_paket(5, 1, 40, 0x70);
			kk_d_sun(0x01, 0, 512, 0x10, 0);
		}
		break;
	case KK_D_M3A_SONSUZ:
		kk_d_sun(0x04, 0x800, 0, 0, 0);
		break;
	case KK_D_M3A_UZUNLUK:
		if (a == 1)
			kk_d_sun(0x05, 0x800, 1024, 0x20, 0);
		break;
	}
}

u8 kk_hs_readb(unsigned int fn, unsigned int adr, int *err)
{
	*err = 0;
	kk_s_okuma52++;
	if (fn == 1 && adr == 0x08 && !kk_s_quit_goruldu)
		return kk_s_int_var() ? 0x01 : 0x00;	/* DREADY */
	if (fn == 1 && adr == 0x2B) {
		SK("CMD52 R fn1 0x2B (D2H ack)");
		/* G54 M3a: desensiz eski modelde READY ilk ACK'le temizlenir (A10 yoklaması sonsuz READY görmesin) */
		if (IS_ENABLED(CONFIG_KK_HI1102_M3A) && !kk_s_d.desen && kk_s_quit_goruldu)
			kk_s_hazir_ack = true;
		if (kk_s_d.desen && kk_s_quit_goruldu) {
			if (kk_s_d.ack_bekliyor) {
				kk_s_d.ack_bekliyor = false;
				kk_s_d.ack_n++;
				SK("D2H ACK #%d alindi (msg 0x%x temizlendi)", kk_s_d.ack_n, kk_s_d.msg);
				kk_s_d.msg = 0;
				kk_d_ilerle();
			} else {
				kk_s_d.ack_bos_n++;
				SK("D2H ACK: bekleyen ileti YOK (#%d)", kk_s_d.ack_bos_n);
			}
		}
		return 0;
	}
	if (fn == 1 && adr == 0x08 && kk_s_quit_goruldu) {
		SK("CMD52 R fn1 0x008 (A9 teshis)");
		return kk_s_int_var() ? 0x01 : 0x00;
	}
	SK("BEKLENMEYEN CMD52 R fn%u 0x%03x", fn, adr);
	*err = -EINVAL;
	return 0xff;
}

/* G51: A9 teşhisi — CCCR 0x03 IORx (fn1 hazır = 0x02), 0x05 INTx (0); başka adres beklenmeyen */
u8 kk_hs_f0_readb(unsigned int adr, int *err)
{
	*err = 0;
	kk_s_okuma52++;
	if (adr == 0x03 || adr == 0x05) {
		SK("CMD52 R fn0 CCCR 0x%03x (A9 teshis)", adr);
		return adr == 0x03 ? 0x02 : 0x00;
	}
	SK("BEKLENMEYEN CMD52 R fn0 0x%03x", adr);
	*err = -EINVAL;
	return 0xff;
}

void kk_hs_writeb(unsigned int fn, u8 v, unsigned int adr, int *err)
{
	*err = 0;
	kk_s_c52++;
	SK("CMD52 W #%d fn%u 0x%03x <- 0x%02x", kk_s_c52, fn, adr, v);
	if (fn == 1 && adr == 0x08 && (v & 0x01) && kk_s_rom.yanit_var)
		kk_s_yanit++;
}

int kk_hs_writesb(unsigned int adr, const void *buf, unsigned int tel)
{
	const u8 *b = buf;
	unsigned int i;

	kk_s_c53++;
	if (adr != 0x00) {
		SK("BEKLENMEYEN CMD53 W adr 0x%x", adr);
		return -EINVAL;
	}
#if IS_ENABLED(CONFIG_KK_HI1102_M3B)
	/* G55 M3b: firmware calisiyor (QUIT goruldu) -> FIFO yazmasi = H2D HCC tx; TEL KAYDI (cozum kk-m3b-tel.py'de) */
	if (kk_s_quit_goruldu) {
		static int kk_s_h2d_n;
		unsigned int k;

		kk_s_h2d_n++;
		SK("H2D HCC #%d tel=%u (W53 %02d) ilk4 %02x%02x%02x%02x", kk_s_h2d_n, tel, kk_s_c53, tel > 0 ? b[0] : 0,
		   tel > 1 ? b[1] : 0, tel > 2 ? b[2] : 0, tel > 3 ? b[3] : 0);
#ifdef CONFIG_KK_HI1102_M3C_QEMU
		for (k = 0; k < tel && k < 0; k += 64)	/* M3c: onaltilik dokum yok (k2: seri konsol bogulmasi, RCU stall) */
#else
		for (k = 0; k < tel && k < 1024; k += 64)
#endif
			SK("H2D HCC #%d [%u] %*phN", kk_s_h2d_n, k, (int)min(64u, tel - k), b + k);
		kk_mq_h2d(b, tel);
		return 0;
	}
#endif
	if (kk_s_boz("crc53") && kk_s_rom.bekle_veri && !kk_s_crc53_yapildi) {
		kk_s_crc53_yapildi = true;
		SK("W53 %02d SDIO CRC HATASI gosterildi (-EILSEQ; ilk VERI, tel %u; ROM'a ulasmadi)", kk_s_c53, tel);
		return -EILSEQ;
	}
	if (kk_s_boz("zaman53") && !kk_s_rom.bekle_veri && tel >= 20 && !memcmp(b, "WRITEM 2 0x50002280 ", 20)) {
		SK("W53 %02d SDIO ZAMAN ASIMI gosterildi (-ETIMEDOUT; R6 WRITEM; ROM'a ulasmadi)", kk_s_c53);
		return -ETIMEDOUT;
	}
	if (kk_s_rom.bekle_veri) {
		unsigned int a = kk_s_rom.files_adr, ofs = 0, n = tel, d;
		const char *ad = "?";
		bool sifir = true;

		if (a >= 0x30170000 && tel == KK_H_CALI_TEL) {
			ad = "cali";
			n = KK_H_CALI_ANLAMLI;
		} else {
			for (d = 0; d < KK_H_DOSYA_SAYI; d++)
				if (a >= kk_h_bolge_izin[d].taban && a < kk_h_bolge_izin[d].son) {
					ad = kk_h_dosya_ad[d];
					ofs = a - kk_h_bolge_izin[d].taban;
					n = min(tel, kk_h_dosya_boy[d] - ofs);
					break;
				}
		}
		for (i = n; i < tel; i++)
			if (b[i])
				sifir = false;
		SK("W53 %02d VERI dosya=%s ofs=%u anlamli=%u tel=%u crc32_anlamli=%08x dolgu=%s", kk_s_c53, ad, ofs, n, tel,
		   kk_s_crc(b, n), tel == n ? "yok" : (sifir ? "sifir" : "SIFIR-DEGIL"));
	} else {
		char hex[2 * 32 + 1];

		for (i = 0; i < tel && i < 32; i++)
			snprintf(hex + 2 * i, 3, "%02x", b[i]);
		SK("W53 %02d MESAJ tel=%u hex=%s metin='%.*s'", kk_s_c53, tel, hex, (int)strnlen(buf, tel), b);
	}
	{
		int bk = kk_s_rom.bcpu_kapali;

		kk_rom_yaz(&kk_s_rom, b, tel);
		if (!bk && kk_s_rom.bcpu_kapali && kk_s_boz("bcpusifir"))
			SK("bcpusifir: R11 + R12 (BCPU kapatma) goruldu; bundan sonra BFGX 0x300e4000-0x30171e00 READM 64 B SIFIR doner");
	}
	if (kk_s_rom.hata)
		SK("ROM HATA: %s", kk_s_rom.hata_neden);
	kk_s_hazir_t = ktime_get();
	if (kk_s_rom.yanit_var && kk_s_yanit == 0) {
		if (kk_s_boz("gecikme150"))
			kk_s_hazir_t = ktime_add_ms(kk_s_hazir_t, 150);
		if (kk_s_boz("gecikme400"))
			kk_s_hazir_t = ktime_add_ms(kk_s_hazir_t, 400);
	}
	if (kk_s_rom.quit && !kk_s_quit_goruldu) {
		kk_s_quit_goruldu = true;
		kk_s_quit_t = ktime_get();
	}
	return 0;
}

u32 kk_hs_readl(unsigned int adr, int *err)
{
	u32 x = kk_s_rom.yanit_n;

	*err = 0;
	kk_s_okuma53++;
	if (adr != 0x0C) {
		*err = -EINVAL;
		return 0xffffffff;
	}
	if (kk_s_ilk_xfer) {
		kk_s_ilk_xfer = false;
		if (kk_s_boz("xfer0"))
			x = 0;
		if (kk_s_boz("xfer513"))
			x = 513;
		if (kk_s_boz("xferffff"))
			x = 0xffffffff;
		if (x != kk_s_rom.yanit_n)
			SK("xfer_count %u gosterildi (gercek %u; bozuk='%s')", x, kk_s_rom.yanit_n, bozuk);
	}
	return x;
}

int kk_hs_readsb(void *dst, unsigned int adr, unsigned int n)
{
	kk_s_okuma53++;
	/* G56: QUIT sonrası firmware FIFO'su 2048 B'a kadar (cali paketi xfer 2048); öncesi (ROM yanıtı) 512 aynen */
	if (adr != 0x00 || n > (kk_s_quit_goruldu ? sizeof(kk_s_d.paket) : 512))
		return -EINVAL;
	if (kk_s_quit_goruldu) {	/* G53 P-D: QUIT sonrası FIFO = firmware D2H paketi */
		memset(dst, 0, n);
		if (kk_s_d.desen && kk_s_d.veri_bekliyor && n == kk_s_d.xfer) {
			memcpy(dst, kk_s_d.paket, n);
			kk_s_d.veri_bekliyor = false;
			kk_s_d.veri_n++;
			kk_s_d.veri_bayt += n;
			SK("D2H FIFO paket #%d okundu: %u B (xfer %u); ilk 16 B %*phN", kk_s_d.veri_n, n, kk_s_d.xfer, 16,
			   kk_s_d.paket);
			kk_s_d.xfer = 0;
			memset(kk_s_d.comm, 0, sizeof(kk_s_d.comm));
			kk_d_ilerle();
		} else {
			kk_s_d.fifo_bek_n++;
			SK("BEKLENMEYEN FIFO okuma %u B (desen %s, bekleyen veri %d, xfer %u)", n, kk_d_ad[kk_s_d.desen],
			   kk_s_d.veri_bekliyor, kk_s_d.xfer);
		}
		return 0;
	}
	memset(dst, 0, n);
	memcpy(dst, kk_s_rom.yanit, min(n, kk_s_rom.yanit_n));
	if (memchr(kk_s_rom.yanit, 0, min(n, kk_s_rom.yanit_n)) || n == 64)	/* G51: ikili yanıt (READM) onaltılık */
		SK("yanit #%d: ikili %u B %*phN", kk_s_yanit, n, (int)min(n, 64u), kk_s_rom.yanit);
	else
		SK("yanit #%d: '%.*s' (%u B)", kk_s_yanit, (int)min(n, kk_s_rom.yanit_n), kk_s_rom.yanit, n);
	kk_s_rom.yanit_var = 0;
	return 0;
}

int kk_hs_memcpy_fromio(void *dst, unsigned int adr, unsigned int n)
{
	u8 *d = dst;

	kk_s_okuma53++;
	if (adr != 0x30 || n != 80)
		return -EINVAL;
	memset(d, 0, n);
	if (kk_s_d.desen) {		/* G53 P-D: firmware D2H modeli; int_stat ext okumasıyla temizlenir */
		if (kk_s_quit_goruldu) {	/* G52 cihaz: QUIT'ten sonraki İLK ext okuması kesmeyi gösterdi */
			if (!kk_s_d.basladi)
				kk_d_ilk();
			if (KK_D_M3A_ILK(kk_s_d.desen) && kk_s_d.a10_adim && kk_s_d.a10_t && !kk_s_d.int_bekliyor &&
			    !kk_s_d.ack_bekliyor && !kk_s_d.veri_bekliyor && ktime_compare(ktime_get(), kk_s_d.a10_t) >= 0) {
				kk_s_d.a10_t = 0;
				kk_d_a10_sun();		/* G54 M3a */
			}
#if IS_ENABLED(CONFIG_KK_HI1102_M3B)
			kk_mq_sun();
#endif
			kk_s_d.ext_n++;
			if (kk_s_d.int_bekliyor)
				put_unaligned_le32(kk_s_d.int_stat, d);
			put_unaligned_le32(kk_s_d.msg, d + 4);
			put_unaligned_le32(kk_s_d.xfer, d + 8);
			put_unaligned_le32(kk_s_d.credit, d + 12);
			memcpy(d + 16, kk_s_d.comm, 64);
			if (kk_s_d.int_bekliyor)
				SK("D2H ext okundu (#%d): int 0x%x gosterildi ve temizlendi", kk_s_d.ext_n, kk_s_d.int_stat);
			kk_s_d.int_bekliyor = false;
		}
		return 0;
	}
	if (kk_s_quit_goruldu && !kk_s_boz("hazirsiz") && !kk_s_hazir_ack &&
	    ktime_ms_delta(ktime_get(), kk_s_quit_t) >= (kk_s_boz("hazir12") ? 12000 : 30)) {
		d[0] = 0x04;					/* int_stat MFARM */
		d[4] = kk_s_boz("panik") ? 0x80 : 0x01;		/* msg_stat: DEVICE_PANIC / WLAN_READY */
	}
	return 0;
}

bool kk_hs_kmalloc_boz(size_t boy)
{
	if (kk_s_boz("kmalloc") && boy == 261632) {
		SK("kmalloc(%zu) basarisiz gosterildi (bozuk='%s')", boy, bozuk);
		return true;
	}
	return false;
}

bool kk_hs_crc_boz(int d)
{
	return kk_s_boz("crc") && d == 0;
}

bool kk_hs_sha_boz(int d)
{
	return kk_s_boz("sha") && d == 1;
}

void kk_hs_kapi_boz(int *tur, u32 *deger, int *ai, int *veri, u8 *tx, unsigned int tel)
{
	if (kk_s_boz("kapi_mesaj") && *tur == 6 /* KK_HY_FIFO */ && !*veri && !kk_s_kapi_mesaj_yapildi && tx && tel >= 4) {
		kk_s_kapi_mesaj_yapildi = true;
		tx[0] ^= 0x01;
		SK("kapi mutasyonu: ilk mesaj tamponunun 0. bayti degistirildi (0x%02x)", tx[0]);
	}
	if (kk_s_boz("kapi_veri1") && *tur == 6 && !kk_s_kapi_veri1_yapildi) {
		kk_s_kapi_veri1_yapildi = true;
		*veri = 1;
		SK("kapi mutasyonu: ilk FIFO istegi veri=1 yapildi");
	}
	if (kk_s_boz("kapi_adim2") && *tur == 6 && !kk_s_kapi_adim2_yapildi) {
		kk_s_kapi_adim2_yapildi = true;
		*ai = 3;	/* G51: SETPM (tam tabloda 2 = VERSION, kip 1) */
		SK("kapi mutasyonu: ilk FIFO istegi tablo adimi 3 yapildi");
	}
	if (kk_s_boz("kapi_veri") && *veri && !kk_s_kapi_veri_yapildi && tx && tel > 100) {
		kk_s_kapi_veri_yapildi = true;
		tx[100] ^= 0x01;
		SK("kapi mutasyonu: ilk veri tamponunun 100. bayti degistirildi");
	}
	if (kk_s_boz("kapi_deger") && *tur == 4 /* KK_HY_INTAC */ && !kk_s_kapi_deger_yapildi) {
		kk_s_kapi_deger_yapildi = true;
		*deger = 0x0f;
		SK("kapi mutasyonu: F1 0x09 degeri 0x0f istendi");
	}
}

void kk_hs_ozet(void)
{
	char wm[12 * 24 + 1] = "";
	int i, n = 0;

	for (i = 0; i < kk_s_rom.wm_n && i < 12; i++)
		n += scnprintf(wm + n, sizeof(wm) - n, " 0x%08x=0x%x", kk_s_rom.wm_adr[i], kk_s_rom.wm_deger[i]);
	SK("OZET sahte: CMD52 yazma %d, CMD53 yazma %d, yanit %d, CMD52 okuma %d, CMD53 okuma %d", kk_s_c52, kk_s_c53,
	   kk_s_yanit, kk_s_okuma52, kk_s_okuma53);
	SK("OZET rom: mesaj %d veri %d (%u B) readm %d (geri %d) surum %d setpm %d files %d quit %d writem %d hata %d (%s)",
	   kk_s_rom.mesaj_n, kk_s_rom.veri_n, kk_s_rom.veri_bayt, kk_s_rom.readm_n, kk_s_rom.geri_n, kk_s_rom.surum_n, kk_s_rom.setpm,
	   kk_s_rom.files_n, kk_s_rom.quit, kk_s_rom.wm_n, kk_s_rom.hata, kk_s_rom.hata_neden ? kk_s_rom.hata_neden : "-");
	SK("OZET writem:%s", kk_s_rom.wm_n ? wm : " yok");
	if (kk_s_d.desen)
		SK("OZET d2h: desen %s; kesme sunuldu %d, ext okuma %d, ACK %d (bos %d), FIFO paket %d (%u B, beklenmeyen %d), hazir %s",
		   kk_d_ad[kk_s_d.desen], kk_s_d.kesme_n, kk_s_d.ext_n, kk_s_d.ack_n, kk_s_d.ack_bos_n, kk_s_d.veri_n,
		   kk_s_d.veri_bayt, kk_s_d.fifo_bek_n, kk_s_d.hazir_verildi ? "verildi" : "yok");
	SK("OZET ram: D1 0x70000-0xd0c00 crc32 %08x; D2 0x300e4000-0x3013c800 crc32 %08x; D3+D4 0x30168000-0x30171e00 crc32 %08x; wcpu %08x bfgx %08x",
	   kk_s_crc(kk_s_wcpu, 0xD0C00 - 0x70000),
	   kk_s_crc(kk_s_bfgx, 0x3013C800 - KK_ROM_BFGX_TABAN),
	   kk_s_crc(kk_s_bfgx + (0x30168000 - KK_ROM_BFGX_TABAN), 0x30171E00 - 0x30168000),
	   kk_s_crc(kk_s_wcpu, KK_ROM_WCPU_SON - KK_ROM_WCPU_TABAN), kk_s_crc(kk_s_bfgx, KK_ROM_BFGX_SON - KK_ROM_BFGX_TABAN));
}

static int __init kk_hs_init(void)
{
	unsigned int b = 0;

	kk_s_wcpu = vzalloc(KK_ROM_WCPU_SON - KK_ROM_WCPU_TABAN);
	kk_s_bfgx = vzalloc(KK_ROM_BFGX_SON - KK_ROM_BFGX_TABAN);
	if (!kk_s_wcpu || !kk_s_bfgx)
		return -ENOMEM;
	if (kk_s_boz("yanitsiz"))
		b |= KK_ROM_B_YANITSIZ;
	if (kk_s_boz("v100"))
		b |= KK_ROM_B_V100;
	if (kk_s_boz("surum"))
		b |= KK_ROM_B_SURUM;
	if (kk_s_boz("readyyok"))
		b |= KK_ROM_B_READY_YOK;
	if (kk_s_boz("fokilk"))
		b |= KK_ROM_B_FOK_ILK;
	if (kk_s_boz("fokson"))
		b |= KK_ROM_B_FOK_SON;
	if (kk_s_boz("wokr6"))
		b |= KK_ROM_B_WOK_R6;
	if (kk_s_boz("surum2"))
		b |= KK_ROM_B_SURUM2;
	if (kk_s_boz("geriboz"))
		b |= KK_ROM_B_GERI_BOZ;
	if (kk_s_boz("geriyanitsiz"))
		b |= KK_ROM_B_GERI_YANITSIZ;
	if (kk_s_boz("efuseyok"))
		b |= KK_ROM_B_EFUSE_YOK;
	if (kk_s_boz("bcpusifir"))
		b |= KK_ROM_B_BCPU_SIFIR;
	kk_rom_baslat(&kk_s_rom, kk_s_wcpu, kk_s_bfgx, b);
	for (unsigned int d = 1; d < ARRAY_SIZE(kk_d_ad); d++)	/* G53 P-D: D2H deseni (en çok biri) */
		if (kk_s_boz(kk_d_ad[d]))
			kk_s_d.desen = d;
	SK("hazir (YALNIZ QEMU; bozuk='%s' rom bitleri 0x%x)", bozuk, b);
	return 0;
}
subsys_initcall(kk_hs_init);

MODULE_DESCRIPTION("Kara Kutu: Hi1102 ROM sahte modeli (YALNIZ QEMU)");
MODULE_LICENSE("GPL");
