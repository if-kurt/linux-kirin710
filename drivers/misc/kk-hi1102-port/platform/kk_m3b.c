// SPDX-License-Identifier: GPL-2.0
/*
 * Kara Kutu G55 Wi-Fi M3b: port host katmanlarının (oal/hcc/frw/oam + wal/hmac) M3a'nın üstüne bağlanması.
 *
 * Eller kararları (G55): K1 tek güç açılışı (P-D motoru indirir + WLAN_READY; burada GPIO, mmc_hw_reset, dosyadan
 * firmware YOK), K2 CFG80211=y / PM-PM_SLEEP-GPIOLIB kapalı, K3 tek SDIO sürücüsü (kk-wifi; port fn1'i devralır,
 * alım YOKLAMA), B1/B2 HCC tx derleme zamanı beyaz listesi (dışı -> DUR), B3 0xF0<-0 yalnız motorda (1 kez).
 *
 * Akış: kk-hi1102-yukle (A0-A10 M3A TAMAM) -> kk_m3b_baslat(fn1) -> "kk-m3b" iş parçacığı:
 *   ini_cfg_init (gömülü INI) -> oal_main_init (bellek havuzu + hcc_module_init -> kk_m3b_sdio_devral) -> oam -> frw ->
 *   low_power_init (pm verisi; uyku yok) -> hcc_enable (yalnız host durumu) -> yoklama iş parçacığı -> host_main_init
 *   (hwifi_config_init DTS/NV, hmac, wal: wiphy + wlan0, wlan_pm_open = K1 + custom cali).
 * DUR: kk_m3b_dur(neden) -> yoklama durur, sdio durumu kapanır, hcc tx kapısı kapanır; başka erişim yok, reboot eski hâl.
 */
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/atomic.h>
#include <linux/workqueue.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/host.h>
#include "oal_sdio.h"
#include "oal_sdio_comm.h"
#include "oal_sdio_host_if.h"
#include "plat_type.h"
#include "plat_pm_wlan.h"
#include "plat_pm.h"
#include "oal_hcc_host_if.h"
#include "plat_cali.h"
#include "kk_m3b_kapi.h"
netdev_tx_t kk_m3b_xmit_dusur(struct sk_buff *skb, struct net_device *dev);

extern int ini_cfg_init(void);
extern oal_uint32 oal_mem_init_pool(oal_void);
extern int low_power_init(void);
extern oal_int32 oal_main_init(oal_void);
extern oal_int32 oam_main_init(oal_void);
extern oal_int32 frw_main_init(oal_void);
extern oal_int32 hi1102_host_main_init(oal_void);
extern oal_int32 oal_sdio_do_isr(struct oal_sdio *hi_sdio);

#define KK_M3B_YOKLAMA_MS	10
#define KK_M3B_BOS_MS		100	/* 1 sn iletisiz -> 100 ms (A10 ile aynı) */

static struct sdio_func *kk_fn;
struct net_device *kk_m3b_cfg_netdev;	/* G55: kaydedilmeyen cfg VAP netdev (Hisilicon0) ic tutamaci */
static atomic_t kk_dur_var = ATOMIC_INIT(0);
static char kk_dur_neden[160] = "-";
static int kk_asama;			/* 0 bekliyor, 1 baslatiliyor, 2 host init, 3 hazir, 9 DUR */
static const char *kk_asama_ad[] = { "bekliyor", "baslatiliyor", "host-init", "HAZIR", "", "", "", "", "", "DUR" };
static struct task_struct *kk_yokla_ts, *kk_bas_ts;
static bool kk_baslatildi;

/* sayaçlar (sysfs durum + dur ölçütleri) */
atomic_t kk_m3b_tx_izin = ATOMIC_INIT(0), kk_m3b_tx_red = ATOMIC_INIT(0);
atomic_t kk_m3b_xmit_dusen = ATOMIC_INIT(0), kk_m3b_tarama_istek = ATOMIC_INIT(0);
atomic_t kk_m3b_tarama_ssid_atilan = ATOMIC_INIT(0), kk_m3b_tarama_red = ATOMIC_INIT(0);
atomic_t kk_m3b_isr_tur = ATOMIC_INIT(0), kk_m3b_isr_hata = ATOMIC_INIT(0), kk_m3b_msg_red_n = ATOMIC_INIT(0);
/* G56 M3b-2: cali (D2H CALI_TO_HMAC) ve koruma sayaçları */
static atomic_t kk_cali_paket = ATOMIC_INIT(0), kk_cali_bayt = ATOMIC_INIT(0), kk_cali_tamam = ATOMIC_INIT(0);
static atomic_t kk_cali_dusen = ATOMIC_INIT(0), kk_koruma_n = ATOMIC_INIT(0);

int kk_m3b_durdu(void)
{
	return atomic_read(&kk_dur_var);
}

int kk_m3b_guc_acik(void)
{
	return kk_baslatildi && !kk_m3b_durdu();
}

void kk_m3b_dur(const char *neden)
{
	struct oal_sdio *s;

	if (atomic_xchg(&kk_dur_var, 1))
		return;
	snprintf(kk_dur_neden, sizeof(kk_dur_neden), "%s", neden ? neden : "?");
	kk_asama = 9;
	pr_err("kk: m3b: DUR: %s (surucu durur; yoklama ve hcc tx kapandi, baska erisim yok)\n", kk_dur_neden);
	s = oal_get_sdio_default_handler();
	if (s)
		oal_disable_sdio_state(s, OAL_SDIO_ALL);
}

/*
 * G56 M3b-2: D2H kalibrasyon yüklemesi (hmac_save_cali_event). Yalnız uzunluk/sayaç kaydı (veri içeriği basılmaz).
 * Tampon (g_pucCaliDataBuf) M3b init'inde kk_bas'ta ayrılır; yoksa ya da paket sınır dışıysa olay DÜŞER (DUR yok).
 */
void kk_m3b_cali_kayit(unsigned int idx, unsigned int netbuf_len, unsigned int kopya, unsigned int ofs, int son)
{
	int n = atomic_inc_return(&kk_cali_paket);
	int t = atomic_add_return(kopya, &kk_cali_bayt);

	if (son)
		atomic_inc(&kk_cali_tamam);
	pr_info("kk: m3b: cali paketi #%d: idx %u, netbuf %u B, kopya %u B @%u, son %d; toplam %d/%u B\n", n, idx, netbuf_len,
		kopya, ofs, son, t, (unsigned int)OAL_WIFI_CALI_DATA_UPLOAD_LEN);
}

void kk_m3b_cali_dus(const char *neden, unsigned int idx, unsigned int netbuf_len)
{
	int n = atomic_inc_return(&kk_cali_dusen);

	pr_err("kk: m3b: cali olayi DUSURULDU #%d (%s): idx %u, netbuf %u B\n", n, neden, idx, netbuf_len);
}

/* Init'i M3b'de atlanmış (4.9 plat init / GPIO / BFGX) bir yola girildi: sayaç + kayıt; DUR iş kuyruğundan (atomik bağlam olabilir). */
static void kk_koruma_dur_is(struct work_struct *w)
{
	kk_m3b_dur("koruma: init'i atlanmis yol (statik tarama tablosu)");
}
static DECLARE_WORK(kk_koruma_work, kk_koruma_dur_is);
void kk_m3b_koruma(const char *ad)
{
	int n = atomic_inc_return(&kk_koruma_n);

	if (n <= 5)
		pr_err("kk: m3b: KORUMA #%d: %s (init kurulmadi; cagri atlandi) -> DUR\n", n, ad);
	schedule_work(&kk_koruma_work);
}

/* ndo_start_xmit yerine: hiçbir çerçeve tele inmez (M3b'de bağlanma yok); sayılır ve düşürülür. */
netdev_tx_t kk_m3b_xmit_dusur(struct sk_buff *skb, struct net_device *dev)
{
	int n = atomic_inc_return(&kk_m3b_xmit_dusen);

	if (n <= 3 || !(n % 100))
		pr_info("kk: m3b: xmit DUSURULDU #%d (%s, %u B, proto 0x%04x); tele inmez\n", n, dev ? dev->name : "-",
			skb ? skb->len : 0, skb ? ntohs(skb->protocol) : 0);
	if (skb)
		dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;
}

struct file *kk_m3b_dosya_red(const char *ad)
{
	pr_err("kk: m3b: port dosya erisimi (%s) REDDEDILDI\n", ad);
	kk_m3b_dur("port dosya erisimi istendi (derlenmedi)");
	return ERR_PTR(-EPERM);
}
ssize_t kk_m3b_dosya_io_red(const char *ad)
{
	kk_m3b_dosya_red(ad);
	return -EPERM;
}

/* Port'un H2D ileti yolu (oal_sdio_send_msg: CMD53 fn1 0x24) M3b'de TAMAMEN kapalı (Y9/B8/B9 RED). */
int kk_m3b_msg_red(unsigned int val)
{
	atomic_inc(&kk_m3b_msg_red_n);
	pr_err("kk: m3b: H2D ileti 0x%x REDDEDILDI (oal_sdio_send_msg derlenmis ama kapali)\n", val);
	kk_m3b_dur("H2D ileti istegi (tablo disi yazma turu)");
	return -EPERM;
}

/*
 * B1/B2 HCC tx kapısı. Her tx kayda geçer (ilk 48 B onaltılık; tarama isteği çözülür). Beyaz liste kk_m3b_tx_izinli()
 * (derleme zamanı tablo). Liste dışı -> kk_m3b_dur, gönderilmez.
 */
#include "kk_m3b_tx_liste.h"
#ifdef CONFIG_KK_HI1102_M3C
static atomic_t kk_m3c_izin = ATOMIC_INIT(0), kk_m3c_disi = ATOMIC_INIT(0);
#ifdef CONFIG_KK_HI1102_M3C_QEMU
/* G56/G57 (YALNIZ QEMU): kk_m3c_kayit=1 -> M3c listesi disi da GECER ama "LISTE DISI" kaydi dusulur (olcum araci).
   0 (varsayilan) -> liste disi RED + DUR (cihaz davranisi; cihaz yapisinda bu parametre YOK). */
static int kk_m3c_kayit;
core_param(kk_m3c_kayit, kk_m3c_kayit, int, 0444);
#else
#define kk_m3c_kayit 0
#endif
static void kk_m3c_sinif_say(const char *sinif)
{
	if (!strcmp(sinif, "MGMT AUTH")) kk_m3c_say(KK_M3C_S_AUTH);
	else if (!strcmp(sinif, "MGMT ASSOC_REQ") || !strcmp(sinif, "MGMT REASSOC_REQ")) kk_m3c_say(KK_M3C_S_ASSOC);
	else if (!strcmp(sinif, "MGMT DEAUTH")) kk_m3c_say(KK_M3C_S_DEAUTH);
	else if (!strcmp(sinif, "MGMT DISASSOC")) kk_m3c_say(KK_M3C_S_DISASSOC);
	else if (!strcmp(sinif, "ACTION BA")) kk_m3c_say(KK_M3C_S_BA);
}
#endif
int kk_m3b_tx_kapi(unsigned int main_type, unsigned int sub_type, unsigned int ext_len, const void *pay, unsigned int pay_len)
{
	const char *neden = NULL;
	int n;

	if (kk_m3b_durdu()) {
		atomic_inc(&kk_m3b_tx_red);
		return -EIO;
	}
	neden = kk_m3b_tx_izinli(main_type, sub_type, ext_len, pay, pay_len);
#ifdef CONFIG_KK_HI1102_M3C
	if (neden && main_type == 0 && sub_type == 0) {
		const char *sinif, *n3;
		int gizli;
		const u8 *b = pay;

		n3 = kk_m3c_tx_izinli(ext_len, pay, pay_len, &sinif, &gizli);
		if (!n3) {
			n = atomic_inc_return(&kk_m3c_izin);
			atomic_inc(&kk_m3b_tx_izin);
			kk_m3c_sinif_say(sinif);
#ifdef CONFIG_KK_HI1102_M3C_QEMU
			if (!gizli) {
				pr_info("kk: m3c: HCC tx M3c #%d: %s; frw %u/%u ext %u len %u: %*phN\n", n, sinif,
					pay_len > 1 ? b[0] : 0, pay_len > 1 ? b[1] : 0, ext_len, pay_len,
					(int)min(pay_len, 160u), pay);
				return 0;
			}
#endif
			/* cihaz: M3c turlerinin yuku basilmaz (SSID / BSSID / anahtar / IP) */
			pr_info("kk: m3c: HCC tx M3c #%d: %s; frw %u/%u ext %u len %u (yuk basilmaz)\n", n, sinif,
				pay_len > 1 ? b[0] : 0, pay_len > 1 ? b[1] : 0, ext_len, pay_len);
			return 0;
		}
		if (kk_m3c_kayit) {
			n = atomic_inc_return(&kk_m3c_disi);
			atomic_inc(&kk_m3b_tx_izin);
			pr_warn("kk: m3c: LISTE DISI #%d (kayit kipi, QEMU, GECIRILDI): %s; %s; frw %u/%u ext %u len %u cfg %u fc 0x%02x\n",
				n, sinif, n3, pay_len > 1 ? b[0] : 0, pay_len > 1 ? b[1] : 0, ext_len, pay_len,
				(ext_len == 3 && pay_len > 4 && b[0] == 1 && b[1] == 2) ? (b[3] | b[4] << 8) : 0xffff,
				(ext_len == 58 && pay_len > 22) ? b[22] : 0xff);
			return 0;
		}
		neden = n3;
	}
#endif
	if (neden) {
		n = atomic_inc_return(&kk_m3b_tx_red);
#ifdef CONFIG_KK_HI1102_M3C
		/* G57 M3c: RED satirinda yalniz frw ve (SYN_CFG ise) cfg-id; yuk basilmaz (cerceve adres / SSID / anahtar icerebilir) */
		pr_err("kk: m3b: HCC tx RED #%d: ana %u alt %u ext %u len %u: %s; frw %u/%u cfg %u (yuk basilmaz)\n", n, main_type, sub_type,
		       ext_len, pay_len, neden, pay_len > 1 ? ((const u8 *)pay)[0] : 0, pay_len > 1 ? ((const u8 *)pay)[1] : 0,
		       (ext_len == 3 && pay_len > 4 && ((const u8 *)pay)[0] == 1 && ((const u8 *)pay)[1] == 2) ?
		       (((const u8 *)pay)[3] | ((const u8 *)pay)[4] << 8) : 0xffff);
#else
		pr_err("kk: m3b: HCC tx RED #%d: ana %u alt %u ext %u len %u: %s; ilk %u B: %*phN\n", n, main_type, sub_type, ext_len, pay_len,
		       neden, min(pay_len, 48u), (int)min(pay_len, 48u), pay);
#endif
		kk_m3b_dur("HCC tx beyaz liste disi");
		return -EPERM;
	}
	n = atomic_inc_return(&kk_m3b_tx_izin);
	pr_info("kk: m3b: HCC tx #%d: frw %u/%u cfg %u len %u: %*phN\n", n, pay_len > 1 ? ((const u8 *)pay)[0] : 0, pay_len > 1 ? ((const u8 *)pay)[1] : 0, (pay_len > 4 && ((const u8 *)pay)[0] == 1 && ((const u8 *)pay)[1] == 2) ? (((const u8 *)pay)[3] | ((const u8 *)pay)[4] << 8) : 0xffff, pay_len,
		(int)min(pay_len, 48u), pay);
	return 0;
}

/* K3: port'un oal_sdio_do_isr'ı GPIO/sdio irq yerine yoklamayla (A10 ile aynı aralık); 4.9 oal_gpio_rxdata_thread deseni. */
static int kk_yokla(void *v)
{
	struct oal_sdio *s = oal_get_sdio_default_handler();
	unsigned long son = jiffies;
	int ret, once = 0;

	pr_info("kk: m3b: yoklama basladi (%d ms; 1 sn iletisiz -> %d ms); kesme kaydi YOK\n", KK_M3B_YOKLAMA_MS, KK_M3B_BOS_MS);
	while (!kthread_should_stop() && !kk_m3b_durdu()) {
		if (s && s->func && oal_sdio_get_state(s, OAL_SDIO_RX)) {
			u32 dr = s->func1_stat.func1_data_int_count + s->func1_stat.func1_msg_int_count;

			oal_sdio_rx_transfer_lock(s);
			sdio_claim_host(s->func);
			ret = oal_sdio_do_isr(s);
			sdio_release_host(s->func);
			oal_sdio_rx_transfer_unlock(s);
			atomic_inc(&kk_m3b_isr_tur);
			if (ret) {
				atomic_inc(&kk_m3b_isr_hata);
				kk_m3b_dur("oal_sdio_do_isr hata (SDIO okuma/RERROR/biçim)");
				break;
			}
			if (dr != once) {
				once = dr;
				son = jiffies;
			}
		}
		if (time_after(jiffies, son + HZ))
			msleep(KK_M3B_BOS_MS);
		else
			msleep(KK_M3B_YOKLAMA_MS);
	}
	pr_info("kk: m3b: yoklama bitti (tur %d, hata %d)\n", atomic_read(&kk_m3b_isr_tur), atomic_read(&kk_m3b_isr_hata));
	return 0;
}

#define KK_ADIM(ad, ifade) do { int _r; kk_asama_ad_adim = ad; _r = (ifade); \
	pr_info("kk: m3b: %s -> %d\n", ad, _r); if (_r) { kk_m3b_dur(ad " basarisiz"); return 0; } } while (0)
static const char *kk_asama_ad_adim = "-";

static int kk_bas(void *v)
{
	struct pm_drv_data *pm;

	kk_asama = 1;
	pr_info("kk: m3b: baslatma: fn%u (%s); K1 tek guc acilisi, K3 tek surucu + yoklama, HCC tx beyaz listesi\n",
		kk_fn->num, sdio_func_id(kk_fn));
	kk_m3b_fn_ver(kk_fn);
	KK_ADIM("ini_cfg_init (gomulu INI)", ini_cfg_init());
	/* gomulu derlemede bellek havuzu plat_sysfs_init'te kurulur (plat_main.c:367); o cagrilmadigi icin burada */
	KK_ADIM("oal_mem_init_pool", (int)oal_mem_init_pool());
	KK_ADIM("oal_main_init (bellek + hcc + sdio devral)", oal_main_init());
	KK_ADIM("oam_main_init", oam_main_init());
	KK_ADIM("frw_main_init", frw_main_init());
	KK_ADIM("low_power_init (pm verisi; uyku yok)", low_power_init());
	pm = pm_get_drvdata();
	if (!pm || !pm->pst_wlan_pm_info) {
		kk_m3b_dur("pm verisi yok");
		return 0;
	}
	/* uyandırma (0xF0 <- 0) motorda bir kez yapıldı (Y7/B3); port'un wakeup_dev'i erken dönsün */
	pm->pst_wlan_pm_info->ul_wlan_dev_state = HOST_DISALLOW_TO_SLEEP;
	hcc_enable(hcc_get_default_handler(), OAL_TRUE);
	pr_info("kk: m3b: hcc_enable (yalniz host durumu); dev_state DISALLOW_TO_SLEEP; g_wlan_pm_switch 0\n");
	kk_baslatildi = true;
	kk_yokla_ts = kthread_run(kk_yokla, NULL, "kk-m3b-yokla");
	if (IS_ERR(kk_yokla_ts)) {
		kk_yokla_ts = NULL;
		kk_m3b_dur("yoklama is parcacigi");
		return 0;
	}
	kk_asama = 2;
	/* G56 M3b-2: 4.9'da firmware_cfg_init (plat_firmware.c:2037) ayırır; M3b o yolu çağırmaz. Yalnız kzalloc (plat_cali.c:176-190). */
	BUILD_BUG_ON(sizeof(oal_cali_param_stru) != 1944);	/* yukleme 1940 = G55 Oops x21 0x794 */
	pr_info("kk: m3b: cali tamponu: oal_cali_param_stru %u B (yukleme %u B, indirme %u B)\n", (unsigned int)sizeof(oal_cali_param_stru),
		(unsigned int)OAL_WIFI_CALI_DATA_UPLOAD_LEN, (unsigned int)OAL_WIFI_CALI_DATA_DOWNLOAD_LEN);
	KK_ADIM("cali_data_buf_malloc (yalniz kzalloc)", (int)cali_data_buf_malloc());
	KK_ADIM("hi1102_host_main_init (hwifi_config + hmac + wal + wlan_pm_open K1)", hi1102_host_main_init());
	if (!kk_m3b_durdu()) {
		kk_asama = 3;
		pr_info("kk: m3b: HAZIR: wiphy + wlan0 kayitli (yalniz STA, pasif tarama); tx izin %d red %d\n",
			atomic_read(&kk_m3b_tx_izin), atomic_read(&kk_m3b_tx_red));
	}
	return 0;
}

#if IS_ENABLED(CONFIG_KK_HI1102_YUKLE_SAHTE)
#include <linux/scatterlist.h>
#include <linux/slab.h>
/* YALNIZ QEMU: gerçek SDIO kartı yok; port'un func->card->host alanları için sahte nesneler, G/Ç kk_hs_* modeline. */
static struct mmc_host kk_q_host = { .max_seg_size = 65536, .max_segs = 64, .max_blk_size = 512, .max_blk_count = 511,
				     .max_req_size = 512 * 511 };
static struct mmc_card kk_q_card = { .host = &kk_q_host, .cccr = { .multi_block = 1 } };
static struct sdio_func kk_q_func = { .card = &kk_q_card, .num = 1, .vendor = 0x0296, .device = 0x5347,
				      .cur_blksize = 512, .max_blksize = 512 };
int kk_q_yok(const char *ad)
{
	pr_err("kk: m3b-q: %s cagrildi (port'ta beklenmiyor)\n", ad);
	kk_m3b_dur(ad);
	return -EPERM;
}
void kk_q_writel(u32 v, unsigned int adr, int *err)
{
	*err = kk_q_yok("sdio_writel");
}
int kk_q_toio(unsigned int adr, const void *src, int n)
{
	return kk_q_yok("sdio_memcpy_toio");
}
void kk_q_mmc_wait_for_req(struct mmc_host *h, struct mmc_request *mrq)
{
	struct mmc_command *c = mrq->cmd;
	struct mmc_data *d = mrq->data;
	u32 arg = c->arg, adr = (arg >> 9) & 0x1FFFF, fn = (arg >> 28) & 7;
	unsigned int top = d ? d->blksz * d->blocks : 0, o = 0, i;
	struct scatterlist *sg;
	u8 *t;
	int e;

	c->error = 0;
	if (d)
		d->error = 0;
	if (c->opcode != 53 || !d || fn != 1 || !top || (arg & BIT(26))) {
		c->error = kk_q_yok("mmc_wait_for_req (CMD53 fn1 sabit adres disi)");
		return;
	}
	t = kzalloc(top, GFP_KERNEL);
	if (!t) {
		c->error = -ENOMEM;
		return;
	}
	if (arg & BIT(31)) {
		for_each_sg(d->sg, sg, d->sg_len, i) {
			unsigned int n = min(sg->length, top - o);
			memcpy(t + o, sg_virt(sg), n);
			o += n;
		}
		e = kk_hs_writesb(adr, t, top);
	} else {
		e = kk_hs_readsb(t, adr, top);
		for_each_sg(d->sg, sg, d->sg_len, i) {
			unsigned int n = min(sg->length, top - o);
			memcpy(sg_virt(sg), t + o, n);
			o += n;
		}
	}
	kfree(t);
	d->error = e;
	if (!e)
		d->bytes_xfered = top;
}
#endif

/* kk-hi1102-yukle'den (M3A TAMAM sonrası) bir kez; erişim yok, iş parçacığına verilir. */
int kk_m3b_baslat(struct sdio_func *f)
{
#if IS_ENABLED(CONFIG_KK_HI1102_YUKLE_SAHTE)
	if (!f) {
		f = &kk_q_func;
		pr_info("kk: m3b-q: QEMU sahte fn1 (kk_hs_* modeli)\n");
	} else
#endif
	if (f)
		get_device(&f->dev);
	if (kk_fn || !f)
		return -EBUSY;
	kk_fn = f;
	kk_bas_ts = kthread_run(kk_bas, NULL, "kk-m3b");
	if (IS_ERR(kk_bas_ts)) {
		kk_bas_ts = NULL;
		kk_m3b_dur("baslatma is parcacigi");
		return -ENOMEM;
	}
	return 0;
}

static ssize_t durum_show(struct kobject *k, struct kobj_attribute *a, char *b)
{
	return sysfs_emit(b, "asama=%s adim='%s' dur=%d neden='%s' tx_izin=%d tx_red=%d h2d_msg_red=%d xmit_dusen=%d tarama_istek=%d tarama_ssid_atilan=%d tarama_red=%d isr_tur=%d isr_hata=%d cali_paket=%d cali_bayt=%d cali_tamam=%d cali_dusen=%d koruma=%d\n",
		kk_asama_ad[kk_asama], kk_asama_ad_adim, kk_m3b_durdu(), kk_dur_neden, atomic_read(&kk_m3b_tx_izin),
		atomic_read(&kk_m3b_tx_red), atomic_read(&kk_m3b_msg_red_n), atomic_read(&kk_m3b_xmit_dusen),
		atomic_read(&kk_m3b_tarama_istek), atomic_read(&kk_m3b_tarama_ssid_atilan), atomic_read(&kk_m3b_tarama_red),
		atomic_read(&kk_m3b_isr_tur), atomic_read(&kk_m3b_isr_hata), atomic_read(&kk_cali_paket), atomic_read(&kk_cali_bayt),
		atomic_read(&kk_cali_tamam), atomic_read(&kk_cali_dusen), atomic_read(&kk_koruma_n));
}
#ifdef CONFIG_KK_HI1102_M3C
extern atomic_t kk_m3c_baglan_istek, kk_m3c_baglan_red, kk_m3c_ayril_istek;
static ssize_t m3c_durum_show(struct kobject *k, struct kobj_attribute *a, char *b)
{
	int n = scnprintf(b, PAGE_SIZE, "m3c_izin=%d m3c_liste_disi=%d kayit=%d baglan_istek=%d baglan_red=%d ayril_istek=%d ",
		atomic_read(&kk_m3c_izin), atomic_read(&kk_m3c_disi), kk_m3c_kayit, atomic_read(&kk_m3c_baglan_istek),
		atomic_read(&kk_m3c_baglan_red), atomic_read(&kk_m3c_ayril_istek));

	n += kk_m3c_sayac_yaz(b + n, PAGE_SIZE - n - 2);
	b[n++] = '\n';
	b[n] = 0;
	return n;
}
static struct kobj_attribute kk_m3c_durum_attr = __ATTR_RO(m3c_durum);
#endif
static struct kobj_attribute kk_durum_attr = __ATTR_RO(durum);

static int __init kk_m3b_sysfs(void)
{
	struct kobject *k = kobject_create_and_add("kk_m3b", kernel_kobj);

	if (!k || sysfs_create_file(k, &kk_durum_attr.attr))
		pr_warn("kk: m3b: sysfs yok\n");
#ifdef CONFIG_KK_HI1102_M3C
	if (k && sysfs_create_file(k, &kk_m3c_durum_attr.attr))
		pr_warn("kk: m3c: sysfs yok\n");
	pr_info("kk: m3c: G57: connect/disconnect/anahtar op'lari kayitli; veri L3 suzgeci; liste disi RED+DUR (kayit kipi %d); durum /sys/kernel/kk_m3b/m3c_durum\n", kk_m3c_kayit);
#endif
	pr_info("kk: m3b: hazir (acilista SDIO erisimi 0; port init'leri acilista CAGRILMAZ); durum /sys/kernel/kk_m3b/durum\n");
	return 0;
}
late_initcall(kk_m3b_sysfs);
