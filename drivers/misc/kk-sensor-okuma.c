// SPDX-License-Identifier: GPL-2.0
/*
 * Kara Kutu: Kirin 710 / hi6555v200 — sensör günlüğü + sıcaklık sigortası.
 *
 * G34 (6a, A sınıfı, saf okuma):
 *   - pil gerilimi: PMIC coul V_OUT 0x2D4/0x2D5; bit15 temizlenir; uV = kod*2400000/10661.
 *     G35: 4.9'un varsayılan ×1,014 kazancı KALDIRILDI (bu cihazın NV kalibrasyonu ≈1,00; G34-B: ham değer 4.9'a 3 mV).
 *   - pil akımı: coul CURRENT 0x2D2/0x2D3 (işaretli); uA = kod*5000000/10661*10/r_coul_mohm; + = deşarj.
 *   - CPU frekans oyu PMCTRL 0x2DC, SCTRL 0x41C (ham).
 *
 * G35 (Tur 2, Furkan onaylı):
 *   - Pil sıcaklığı (C sınıfı): LPM3'e HKADC isteği, 4.9 hisi_adc.c + hisi_mailbox_dev.c ile BİREBİR:
 *       TX mailbox-16 (AP src bit0 -> LPM3 des bit3), AUTO_ACK, DATA0 = 0x000B020C, DATA1 = kanal (10);
 *       RX mailbox-0 (GIC SPI 197): üst bilgi 0x080B020C, DATA1 [7:0] kanal, [15:8] hata, [31:16] ham değer.
 *       Yazmaç sırası 4.9'daki gibi (ensure_channel/release -> unlock -> occupy -> hw_send; alımda
 *       veri oku -> veri sıfırla -> ICLR -> gerekirse SEND). Başka LPM3 komutu ya da PMIC yazması YOK.
 *       Fark (bilinçli): 30 sn'de en fazla bir istek; zaman aşımında (1500 ms) tekrar deneme YOK (4.9 coul 3 kez dener).
 *       Dönüşüm: 4.9 hisi_coul_core.c T_V_TABLE, aynı doğrusal ara değer (m°C çözünürlükte).
 *   - SoC sıcaklıkları (S sınıfı): 4.9 hisi_tsens.c ile aynı SMC: x0 = 0xC5009900, x1 = sensör no (0..3:
 *       cluster0/A53, cluster1/A73, gpu, modem; ddr reg_no 0xff, 4.9 de okumuyor), x2 = x3 = 0. 5 sn'de bir.
 *       Dönüşüm: T = (kod - 116) * 165 / (922 - 116) - 40 (DT tsensor_adc_start/end); aralık dışı = geçersiz.
 *   - Sigorta (kernel içinde; kullanıcı alanından bağımsız): pil > pil_esik_mc ya da en sıcak SoC sensörü >
 *       soc_esik_mc, aynı kaynaktan iki ardışık GEÇERLİ okumada -> KERN_EMERG log + kernel_restart("bootloader").
 *       G57 G-2/G-3 hazırlığı: hedef DT'den "karakutu,sigorta-hedef" = "bootloader" (varsayılan; özellik yoksa
 *       davranış ve log metni değişmez) | "erecovery" (D3; reboot-mode'da mode-erecovery = <0x08> gerekir).
 *       Başka değer -> uyarı + "bootloader". Hedef açılışta sabitlenir; sysfs'ten değiştirilemez.
 *       Geçersiz/anlamsız değerler (< 0 ya da > 150 °C, tablo uçları, aralık dışı kod, zaman aşımı) sigortayı
 *       tetiklemez ve ardışık sayacı değiştirmez; log'a düşer. Eşikler sysfs'ten YALNIZ DÜŞÜRÜLEBİLİR.
 *   - Sahte kaynak (yalnız QEMU DT'sinde "karakutu,sahte-kaynak"): IPC/SMC'ye hiç dokunulmaz; değerler DT/sysfs'ten.
 */
#include <linux/arm-smccc.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/regmap.h>
#include <linux/spmi.h>
#include <linux/thermal.h>
#include <linux/workqueue.h>
#include "kk-sarj.h"	/* G39: sigorta kancası (yalnız CONFIG_KK_SARJ_POLITIKA; cihazda boş satır içi) */

#define PMIC_CURRENT_0		0x2D2
#define PMIC_V_OUT_0		0x2D4
#define PMCTRL_HV_SONUC		0x2DC
#define SCTRL_SCBAKDATA4	0x41C
#define KK_PERIYOT		(30 * HZ)	/* günlük satırı + pil (LPM3) isteği */
#define KK_SOC_PERIYOT		(5 * HZ)	/* SoC SMC */

/* 4.9 hisi_mailbox_dev.c yazmaç düzeni */
#define MB_SRC(c)		(((c) << 6) + 0x00)
#define MB_DSET(c)		(((c) << 6) + 0x04)
#define MB_MODE(c)		(((c) << 6) + 0x10)
#define MB_IMASK(c)		(((c) << 6) + 0x14)
#define MB_ICLR(c)		(((c) << 6) + 0x18)
#define MB_SEND(c)		(((c) << 6) + 0x1C)
#define MB_DATA(c, i)		(((c) << 6) + 0x20 + ((i) << 2))
#define MB_LOCK			0xA00
#define MB_IDLE			BIT(4)
#define MB_DEST			BIT(6)
#define MB_ACK			BIT(7)
#define MB_AUTO_ACK		BIT(0)
#define MB_TX			16	/* mailbox-16: tx, src_bit 0 (AP), des_bit 3 (LPM3) */
#define MB_RX			0	/* mailbox-0: rx, src_bit 3, des_bit 0 */
#define MB_AP			0
#define MB_LPM3			3
#define MB_KAPASITE		8	/* DT ipc capability */
#define GIC_1			0
#define GIC_2			1

#define ADC_IPC_DATA		0x000B020C	/* (AP 0 << 24) | (ADC 0x0b << 16) | (0x02 << 8) | 0x0c */
#define ADC_RECV_HEADER		0x080B020C
#define ADC_RECV_CURRENT_HEADER	0x080B0214
#define ADC_IPC_TIMEOUT_MS	1500

#define TSENS_SMC		0xC5009900UL
#define TSENS_SAYI		4
#define TSENS_KOD_BAS		116
#define TSENS_KOD_SON		922

#define PIL_ESIK_VARSAYILAN	45000
#define SOC_ESIK_VARSAYILAN	70000
#define ANLAMLI_MIN		0
#define ANLAMLI_MAX		150000

static const char * const tsens_ad[TSENS_SAYI] = { "cluster0", "cluster1", "gpu", "modem" };

/* 4.9 drivers/power/hisi/coul/hisi_coul_core.c T_V_TABLE (T_V_ARRAY_LENGTH = 31) */
static const int T_V_TABLE[][2] = {
	{-273, 4095}, {-40, 3764}, {-36, 3689}, {-32, 3602}, {-28, 3500},
	{-24, 3387}, {-20, 3261}, {-16, 3122}, {-12, 2973}, {-8, 2814},
	{-4, 2650}, {0, 2480}, {4, 2308}, {8, 2136}, {12, 1967},
	{16, 1803}, {20, 1646}, {24, 1497}, {28, 1360}, {32, 1230},
	{36, 1111}, {40, 1001}, {44, 903}, {48, 812}, {52, 729},
	{56, 655}, {60, 590}, {64, 531}, {74, 406}, {84, 313},
	{125, 110},
};
#define T_V_LEN ARRAY_SIZE(T_V_TABLE)

struct kk_sensor {
	struct device *dev;
	struct device_node *pmic_np;
	struct regmap *pmic, *pmctrl, *sctrl;
	u32 r_mohm;
	struct delayed_work iz, soc_iz;
	struct mutex kilit;		/* durum alanları */

	bool sahte;
	int sahte_pil_adc;
	u32 sahte_soc_kod;

	/* pil (LPM3/HKADC) */
	void __iomem *ipc;
	u32 unlock;
	u32 pil_kanal;
	struct mutex adc_kilit;
	struct completion adc_bitti;
	int adc_bekleyen;		/* beklenen kanal, -1 yok */
	int adc_deger;			/* ham değer ya da -EINVAL */
	u32 son_rx[MB_KAPASITE];
	unsigned long son_istek;
	bool hic_istek;
	u32 n_istek, n_yanit, n_zaman_asimi, n_gonderim_hatasi, n_adc_hatasi, n_bilinmeyen, n_kanal_uyusmaz;
	u32 n_kilit_tekrar, n_isgal_tekrar, n_kanal_bekleme;
	int pil_adc;			/* son ham ADC, <0 yok */
	int pil_mc;
	bool pil_gecerli;
	unsigned long pil_zaman;	/* G42: son geçerli pil sıcaklığı (jiffies) */
	u32 pil_rx0, pil_rx1;

	/* SoC (SMC) */
	u32 soc_kod[TSENS_SAYI];
	int soc_mc[TSENS_SAYI];
	bool soc_gecerli[TSENS_SAYI];
	int soc_max_mc;
	bool soc_max_gecerli;
	u32 n_soc_okuma;
	unsigned long soc_zaman;	/* son SMC okuması (jiffies) */
	bool soc_hic;

	/* G36: thermal zone'lar (0..3 sensörler, 4 = en sıcak) */
	struct kk_tz { struct kk_sensor *k; int id; } tz[TSENS_SAYI + 1];

	/* sigorta */
	int pil_esik_mc, soc_esik_mc;
	int pil_ardisik, soc_ardisik;
	u32 n_anlamsiz;
	bool sigorta_atti;
	const char *sigorta_hedef;	/* G57 G-2/G-3: "bootloader" (varsayılan) | "erecovery" */
};

/* ---------------- G34: coul V/I, PMCTRL oyu (saf okuma) ---------------- */

static int kk_pmic_bul(struct kk_sensor *k)
{
	struct spmi_device *sd;

	if (k->pmic)
		return 0;
	if (!k->pmic_np)
		return -ENODEV;
	sd = spmi_find_device_by_of_node(k->pmic_np);
	if (!sd)
		return -ENODEV;
	k->pmic = dev_get_regmap(&sd->dev, NULL);
	put_device(&sd->dev);
	return k->pmic ? 0 : -ENODEV;
}

static int kk_oku16(struct kk_sensor *k, u32 reg, u16 *v)
{
	u32 lo, hi;
	int r = regmap_read(k->pmic, reg, &lo);

	if (!r)
		r = regmap_read(k->pmic, reg + 1, &hi);
	if (!r)
		*v = (u16)((hi & 0xff) << 8 | (lo & 0xff));
	return r;
}

/* ---------------- G35: 4.9 mailbox yolu (birebir) ---------------- */

static inline u32 mb_r(struct kk_sensor *k, u32 off) { return readl(k->ipc + off); }
static inline void mb_w(struct kk_sensor *k, u32 v, u32 off) { writel(v, k->ipc + off); }

/* hisi_mdev_release: imask_all + set_src (sev kullanılmıyor; tek AP istemcisi) */
static void kk_mb_birak(struct kk_sensor *k)
{
	mb_w(k, ~0U, MB_IMASK(MB_TX));
	mb_w(k, BIT(MB_AP), MB_SRC(MB_TX));
}

/* hisi_mdev_ensure_channel: IDLE ise dön; ACK ise bırak; değilse ACK'i bekle (1000×5 us + 60×3-5 ms), sonra bırak */
static void kk_mb_kanal_hazirla(struct kk_sensor *k)
{
	int t, dongu = 300 / 5 + 1000;

	if (mb_r(k, MB_MODE(MB_TX)) & MB_IDLE)
		return;
	if (!(mb_r(k, MB_MODE(MB_TX)) & MB_ACK)) {
		k->n_kanal_bekleme++;
		for (t = 0; t < dongu; t++) {
			if (t < 1000)
				udelay(5);
			else
				usleep_range(3000, 5000);
			if (mb_r(k, MB_MODE(MB_TX)) & MB_ACK)
				break;
		}
		if (t == dongu)
			dev_err(k->dev, "kk: pil: mailbox-16 ACK beklenirken zaman asimi (mode=0x%08x); 4.9 gibi birakiliyor\n",
				mb_r(k, MB_MODE(MB_TX)));
	}
	kk_mb_birak(k);
}

/* hisi_mdev_unlock: en fazla 3 deneme */
static int kk_mb_kilit_ac(struct kk_sensor *k)
{
	int r = 3;

	do {
		mb_w(k, k->unlock, MB_LOCK);
		if (mb_r(k, MB_LOCK) == 0)
			return 0;
		k->n_kilit_tekrar++;
		udelay(10);
	} while (--r);
	return -ENODEV;
}

/* hisi_mdev_occupy: en fazla 10 deneme (4.9 'wfe'; burada kısa bekleme) */
static int kk_mb_isgal(struct kk_sensor *k)
{
	int r = 10;

	do {
		if (!(mb_r(k, MB_MODE(MB_TX)) & MB_IDLE)) {
			udelay(1);
		} else {
			mb_w(k, BIT(MB_AP), MB_SRC(MB_TX));
			if (mb_r(k, MB_SRC(MB_TX)) & BIT(MB_AP))
				return 0;
		}
		k->n_isgal_tekrar++;
	} while (--r);
	return -ENODEV;
}

/* hisi_mdev_hw_send, AUTO_ACK */
static void kk_mb_gonder(struct kk_sensor *k, u32 m0, u32 m1)
{
	mb_w(k, ~0U, MB_IMASK(MB_TX));
	mb_w(k, mb_r(k, MB_IMASK(MB_TX)) & ~BIT(MB_LPM3), MB_IMASK(MB_TX));
	mb_w(k, BIT(MB_LPM3), MB_DSET(MB_TX));
	mb_w(k, MB_AUTO_ACK, MB_MODE(MB_TX));
	mb_w(k, m0, MB_DATA(MB_TX, 0));
	mb_w(k, m1, MB_DATA(MB_TX, 1));
	mb_w(k, BIT(MB_AP), MB_SEND(MB_TX));
}

/* mailbox-0 alma: hisi_mbox_interrupt (DEST) + hisi_mdev_receive_msg + hisi_mdev_clr_irq_and_ack + adc_dev_notifier */
static irqreturn_t kk_mb_rx(int irq, void *p)
{
	struct kk_sensor *k = p;
	u32 m[MB_KAPASITE], imask, todo, st;
	int i;

	if (!(mb_r(k, MB_MODE(MB_RX)) & MB_DEST))
		return IRQ_NONE;
	for (i = 0; i < MB_KAPASITE; i++)
		m[i] = mb_r(k, MB_DATA(MB_RX, i));
	for (i = 0; i < MB_KAPASITE; i++)
		mb_w(k, 0, MB_DATA(MB_RX, i));
	imask = mb_r(k, MB_IMASK(MB_RX));
	todo = (BIT(GIC_1) | BIT(GIC_2)) & ~imask;
	mb_w(k, todo, MB_ICLR(MB_RX));
	st = mb_r(k, MB_MODE(MB_RX));
	if ((st & MB_DEST) && !(st & MB_AUTO_ACK))
		mb_w(k, todo, MB_SEND(MB_RX));

	memcpy(k->son_rx, m, sizeof(m));
	if (m[0] == ADC_RECV_HEADER || m[0] == ADC_RECV_CURRENT_HEADER) {
		if (!((m[1] >> 8) & 0xff)) {
			if ((int)(m[1] & 0xff) == READ_ONCE(k->adc_bekleyen)) {
				k->adc_deger = m[1] >> 16;
				k->n_yanit++;
				complete(&k->adc_bitti);
				return IRQ_HANDLED;
			}
			k->n_kanal_uyusmaz++;
		} else {
			k->adc_deger = -EINVAL;
			k->n_adc_hatasi++;
			complete(&k->adc_bitti);
		}
		dev_err_ratelimited(k->dev, "kk: pil: LPM3 yanit hata/uyusmaz: msg[0]=0x%08x msg[1]=0x%08x\n", m[0], m[1]);
		return IRQ_HANDLED;
	}
	k->n_bilinmeyen++;
	dev_info_ratelimited(k->dev, "kk: pil: mailbox-0 bilinmeyen LPM3 mesaji (4.9 gibi alindi ve onaylandi): %08x %08x %08x %08x %08x %08x %08x %08x\n",
			     m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7]);
	return IRQ_HANDLED;
}

/* 4.9 adc_to_temp, m°C çözünürlükte (aynı tablo, aynı doğrusal ara değer) */
static int kk_adc_mc(int adc)
{
	int i;

	if (adc >= T_V_TABLE[0][1])
		return T_V_TABLE[0][0] * 1000;
	if (adc <= T_V_TABLE[T_V_LEN - 1][1])
		return T_V_TABLE[T_V_LEN - 1][0] * 1000;
	for (i = 0; i < T_V_LEN; i++) {
		if (adc == T_V_TABLE[i][1])
			return T_V_TABLE[i][0] * 1000;
		if (adc > T_V_TABLE[i][1])
			break;
	}
	return T_V_TABLE[i - 1][0] * 1000 +
		(adc - T_V_TABLE[i - 1][1]) * (T_V_TABLE[i][0] - T_V_TABLE[i - 1][0]) * 1000 /
		(T_V_TABLE[i][1] - T_V_TABLE[i - 1][1]);
}

/* Bir pil isteği: en fazla 30 sn'de bir; zaman aşımında tekrar YOK. */
static void kk_pil_oku(struct kk_sensor *k)
{
	int adc = -ENODATA, r;
	long kalan;

	if (!k->hic_istek && time_before(jiffies, k->son_istek + KK_PERIYOT)) {
		dev_warn(k->dev, "kk: pil: istek atlandi (30 sn dolmadi)\n");
		return;
	}
	k->hic_istek = false;
	k->son_istek = jiffies;
	k->n_istek++;

	if (k->sahte) {
		adc = READ_ONCE(k->sahte_pil_adc);
		k->n_yanit++;
		k->pil_rx0 = ADC_RECV_HEADER;
		k->pil_rx1 = (adc << 16) | k->pil_kanal;
		goto cevir;
	}

	mutex_lock(&k->adc_kilit);
	reinit_completion(&k->adc_bitti);
	k->adc_deger = -EINVAL;
	WRITE_ONCE(k->adc_bekleyen, k->pil_kanal);
	kk_mb_kanal_hazirla(k);
	r = kk_mb_kilit_ac(k);
	if (!r)
		r = kk_mb_isgal(k);
	if (r) {
		k->n_gonderim_hatasi++;
		WRITE_ONCE(k->adc_bekleyen, -1);
		mutex_unlock(&k->adc_kilit);
		dev_err(k->dev, "kk: pil: mailbox-16 kilit/isgal basarisiz (lock=0x%08x mode=0x%08x); istek gonderilmedi\n",
			mb_r(k, MB_LOCK), mb_r(k, MB_MODE(MB_TX)));
		goto cevir;
	}
	kk_mb_gonder(k, ADC_IPC_DATA, k->pil_kanal);
	kalan = wait_for_completion_timeout(&k->adc_bitti, msecs_to_jiffies(ADC_IPC_TIMEOUT_MS));
	WRITE_ONCE(k->adc_bekleyen, -1);
	if (!kalan) {
		k->n_zaman_asimi++;
		dev_err(k->dev, "kk: pil: LPM3 yaniti %d ms icinde gelmedi (zaman asimi #%u; tekrar denenmeyecek)\n",
			ADC_IPC_TIMEOUT_MS, k->n_zaman_asimi);
		adc = -ETIMEDOUT;
	} else {
		adc = k->adc_deger;
	}
	k->pil_rx0 = k->son_rx[0];
	k->pil_rx1 = k->son_rx[1];
	mutex_unlock(&k->adc_kilit);

cevir:
	mutex_lock(&k->kilit);
	k->pil_adc = adc;
	if (adc < 0) {
		k->pil_gecerli = false;
	} else {
		k->pil_mc = kk_adc_mc(adc);
		/* tablo uçları (açık/kısa NTC) ve aralık dışı = anlamsız */
		k->pil_gecerli = adc < T_V_TABLE[0][1] && adc > T_V_TABLE[T_V_LEN - 1][1] &&
				 k->pil_mc >= ANLAMLI_MIN && k->pil_mc <= ANLAMLI_MAX;
		if (k->pil_gecerli)
			k->pil_zaman = jiffies;
	}
	mutex_unlock(&k->kilit);
}

static void kk_soc_oku(struct kk_sensor *k)
{
	struct arm_smccc_res res;
	int i, mx = INT_MIN;
	bool herhangi = false;

	mutex_lock(&k->kilit);
	for (i = 0; i < TSENS_SAYI; i++) {
		u32 kod;

		if (k->sahte) {
			kod = READ_ONCE(k->sahte_soc_kod);
		} else {
			arm_smccc_smc(TSENS_SMC, i, 0, 0, 0, 0, 0, 0, &res);
			kod = (u32)(int)res.a0;
		}
		k->soc_kod[i] = kod;
		k->soc_gecerli[i] = kod >= TSENS_KOD_BAS && kod <= TSENS_KOD_SON;
		if (k->soc_gecerli[i]) {
			k->soc_mc[i] = ((int)kod - TSENS_KOD_BAS) * 165 * 1000 / (TSENS_KOD_SON - TSENS_KOD_BAS) - 40000;
			if (k->soc_mc[i] < ANLAMLI_MIN || k->soc_mc[i] > ANLAMLI_MAX)
				k->soc_gecerli[i] = false;
		}
		if (k->soc_gecerli[i]) {
			herhangi = true;
			mx = max(mx, k->soc_mc[i]);
		}
	}
	k->soc_max_gecerli = herhangi;
	if (herhangi)
		k->soc_max_mc = mx;
	k->n_soc_okuma++;
	k->soc_zaman = jiffies;
	k->soc_hic = false;
	mutex_unlock(&k->kilit);
}

/* G36: thermal çekirdeği sık sorar; aynı SMC okumasını kısa süre yeniden kullan (5 zone aynı turda tek okuma).
 * G37: yoklama 200 ms -> önbellek 100 ms (soc-max her turda taze okur; en çok ~10 SMC turu/sn, tur = 4 SMC). */
static void kk_soc_tazele(struct kk_sensor *k)
{
	if (k->soc_hic || time_after(jiffies, k->soc_zaman + msecs_to_jiffies(100)))
		kk_soc_oku(k);
}

static int kk_tz_get_temp(struct thermal_zone_device *tzd, int *temp)
{
	struct kk_tz *z = thermal_zone_device_priv(tzd);
	struct kk_sensor *k = z->k;
	int r = 0;

	kk_soc_tazele(k);
	mutex_lock(&k->kilit);
	if (z->id < TSENS_SAYI) {
		if (k->soc_gecerli[z->id])
			*temp = k->soc_mc[z->id];
		else
			r = -EAGAIN;
	} else {
		if (k->soc_max_gecerli)
			*temp = k->soc_max_mc;
		else
			r = -EAGAIN;
	}
	mutex_unlock(&k->kilit);
	return r;
}

static const struct thermal_zone_device_ops kk_tz_ops = {
	.get_temp = kk_tz_get_temp,
};

/* ---------------- G42: şarj politikası için pil girdileri ---------------- */

static struct kk_sensor *kk_sensor_tek;

/* coul V/I: kk_satir ile aynı saf SPMI okuması; sıcaklık: önbellek (yeni LPM3 isteği YOK), en çok 75 sn eski */
int kk_sensor_pil(int *vbat_mv, int *ibat_ma, int *pil_mc, bool *sic_gecerli)
{
	struct kk_sensor *k = READ_ONCE(kk_sensor_tek);
	u16 vk, ik;
	int r;

	if (!k)
		return -ENODEV;
	r = kk_pmic_bul(k);
	if (!r)
		r = kk_oku16(k, PMIC_V_OUT_0, &vk);
	if (!r)
		r = kk_oku16(k, PMIC_CURRENT_0, &ik);
	if (r)
		return r;
	*vbat_mv = (int)div_s64((s64)(vk & 0x7fff) * 2400000 / 10661, 1000);
	*ibat_ma = (int)div_s64((s64)(s16)ik * 5000000 / 10661 * 10 / k->r_mohm, 1000);
	mutex_lock(&k->kilit);
	*pil_mc = k->pil_mc;
	*sic_gecerli = k->pil_gecerli && time_before(jiffies, k->pil_zaman + 75 * HZ);
	mutex_unlock(&k->kilit);
	return 0;
}
EXPORT_SYMBOL_GPL(kk_sensor_pil);

/* ---------------- sigorta ---------------- */

static void kk_sigorta_at(struct kk_sensor *k, const char *neden)
{
	if (k->sigorta_atti)
		return;
	k->sigorta_atti = true;
	pr_emerg("kk: sigorta: %s -> reboot %s (pil %d m°C esik %d, SoC max %d m°C esik %d)\n",
		 neden, k->sigorta_hedef, k->pil_mc, k->pil_esik_mc, k->soc_max_mc, k->soc_esik_mc);
	/* G39: şarj politikası varsa önce CHG_EN=0 (en çok 300 ms; takılırsa reboot gecikmez). Cihazda boş. */
	kk_sarj_sigorta(300);
	kk_deney_sigorta(300);	/* G40: Faz 2 deneyi sürüyorsa CHG_EN=0 (yalnız CONFIG_KK_HI6523_DENEY) */
	kernel_restart((char *)k->sigorta_hedef);
}

static void kk_sigorta_pil(struct kk_sensor *k)
{
	if (!k->pil_gecerli) {
		k->n_anlamsiz++;
		dev_warn(k->dev, "kk: sigorta: pil degeri anlamsiz/yok (adc=%d, %d m°C); sigortayi tetiklemez\n",
			 k->pil_adc, k->pil_mc);
		return;
	}
	if (k->pil_mc > READ_ONCE(k->pil_esik_mc)) {
		k->pil_ardisik++;
		pr_emerg("kk: sigorta: pil %d m°C > esik %d (ardisik %d/2)\n", k->pil_mc, k->pil_esik_mc, k->pil_ardisik);
		if (k->pil_ardisik >= 2)
			kk_sigorta_at(k, "pil sicakligi iki ardisik okumada esigin ustunde");
	} else {
		k->pil_ardisik = 0;
	}
}

static void kk_sigorta_soc(struct kk_sensor *k)
{
	if (!k->soc_max_gecerli) {
		k->n_anlamsiz++;
		dev_warn_ratelimited(k->dev, "kk: sigorta: SoC degerleri anlamsiz (kod %u/%u/%u/%u); sigortayi tetiklemez\n",
				     k->soc_kod[0], k->soc_kod[1], k->soc_kod[2], k->soc_kod[3]);
		return;
	}
	if (k->soc_max_mc > READ_ONCE(k->soc_esik_mc)) {
		k->soc_ardisik++;
		pr_emerg("kk: sigorta: SoC %d m°C > esik %d (ardisik %d/2)\n", k->soc_max_mc, k->soc_esik_mc, k->soc_ardisik);
		if (k->soc_ardisik >= 2)
			kk_sigorta_at(k, "en sicak SoC sensoru iki ardisik okumada esigin ustunde");
	} else {
		k->soc_ardisik = 0;
	}
}

/* ---------------- günlük satırı ---------------- */

static int kk_satir(struct kk_sensor *k, char *b, size_t n)
{
	u16 vk = 0, ik = 0;
	s64 uv = 0, ua = 0;
	u32 hv = 0, sc = 0;
	int rv = -ENODEV, ri = -ENODEV, i;
	size_t o;

	if (!kk_pmic_bul(k)) {
		rv = kk_oku16(k, PMIC_V_OUT_0, &vk);
		ri = kk_oku16(k, PMIC_CURRENT_0, &ik);
	}
	if (!rv)
		uv = (s64)(vk & 0x7fff) * 2400000 / 10661;
	if (!ri)
		ua = (s64)(s16)ik * 5000000 / 10661 * 10 / k->r_mohm;
	if (k->pmctrl)
		regmap_read(k->pmctrl, PMCTRL_HV_SONUC, &hv);
	if (k->sctrl)
		regmap_read(k->sctrl, SCTRL_SCBAKDATA4, &sc);

	mutex_lock(&k->kilit);
	o = scnprintf(b, n, "vbat=%lld mV (0x%04x r=%d) ibat=%lld mA (0x%04x r=%d) | pil: adc=%d %d.%03d C%s rx=%08x/%08x istek=%u yanit=%u zaman_asimi=%u gonderim_hatasi=%u adc_hatasi=%u bilinmeyen=%u tekrar(kilit/isgal)=%u/%u | soc:",
		      uv / 1000, vk, rv, ua / 1000, ik, ri,
		      k->pil_adc, k->pil_mc / 1000, abs(k->pil_mc % 1000), k->pil_gecerli ? "" : " (gecersiz)",
		      k->pil_rx0, k->pil_rx1, k->n_istek, k->n_yanit, k->n_zaman_asimi, k->n_gonderim_hatasi,
		      k->n_adc_hatasi, k->n_bilinmeyen, k->n_kilit_tekrar, k->n_isgal_tekrar);
	for (i = 0; i < TSENS_SAYI; i++)
		o += scnprintf(b + o, n - o, " %s=%u/%d.%03d%s", tsens_ad[i], k->soc_kod[i], k->soc_mc[i] / 1000,
			       abs(k->soc_mc[i] % 1000), k->soc_gecerli[i] ? "" : "(gecersiz)");
	o += scnprintf(b + o, n - o, " max=%d.%03d (okuma %u) | esik pil/soc=%d/%d ardisik=%d/%d anlamsiz=%u%s | oy big/little/gpu=%u/%u/%u MHz sc=0x%x\n",
		       k->soc_max_mc / 1000, abs(k->soc_max_mc % 1000), k->n_soc_okuma,
		       k->pil_esik_mc, k->soc_esik_mc, k->pil_ardisik, k->soc_ardisik, k->n_anlamsiz,
		       k->sahte ? " SAHTE" : "",
		       (hv & 0xff) * 16, ((hv >> 8) & 0xff) * 16, ((hv >> 16) & 0xff) * 16, sc);
	mutex_unlock(&k->kilit);
	return o;
}

static void kk_iz(struct work_struct *w)
{
	struct kk_sensor *k = container_of(to_delayed_work(w), struct kk_sensor, iz);
	char *b = kmalloc(1024, GFP_KERNEL);

	kk_pil_oku(k);
	kk_sigorta_pil(k);
	if (b) {
		kk_satir(k, b, 1024);
		dev_info(k->dev, "kk: sensor: %s", b);
		kfree(b);
	}
	schedule_delayed_work(&k->iz, KK_PERIYOT);
}

static void kk_soc_iz(struct work_struct *w)
{
	struct kk_sensor *k = container_of(to_delayed_work(w), struct kk_sensor, soc_iz);

	kk_soc_oku(k);
	kk_sigorta_soc(k);
	schedule_delayed_work(&k->soc_iz, KK_SOC_PERIYOT);
}

/* ---------------- sysfs ---------------- */

static ssize_t durum_show(struct device *dev, struct device_attribute *a, char *buf)
{
	return kk_satir(dev_get_drvdata(dev), buf, PAGE_SIZE);
}
static DEVICE_ATTR_RO(durum);

static ssize_t kk_esik_yaz(struct kk_sensor *k, int *esik, const char *ad, const char *buf, size_t n)
{
	int v, eski;

	if (kstrtoint(buf, 0, &v))
		return -EINVAL;
	mutex_lock(&k->kilit);
	eski = *esik;
	if (v >= eski) {
		mutex_unlock(&k->kilit);
		dev_warn(k->dev, "kk: sigorta: %s esigi %d -> %d REDDEDILDI (yalniz dusurulebilir)\n", ad, eski, v);
		return -EPERM;
	}
	WRITE_ONCE(*esik, v);
	mutex_unlock(&k->kilit);
	pr_emerg("kk: sigorta: %s esigi %d -> %d m°C (test; yalniz dusurulebilir)\n", ad, eski, v);
	return n;
}

static ssize_t pil_esik_mc_show(struct device *dev, struct device_attribute *a, char *buf)
{
	return sysfs_emit(buf, "%d\n", ((struct kk_sensor *)dev_get_drvdata(dev))->pil_esik_mc);
}
static ssize_t pil_esik_mc_store(struct device *dev, struct device_attribute *a, const char *buf, size_t n)
{
	struct kk_sensor *k = dev_get_drvdata(dev);

	return kk_esik_yaz(k, &k->pil_esik_mc, "pil", buf, n);
}
static DEVICE_ATTR_RW(pil_esik_mc);

static ssize_t soc_esik_mc_show(struct device *dev, struct device_attribute *a, char *buf)
{
	return sysfs_emit(buf, "%d\n", ((struct kk_sensor *)dev_get_drvdata(dev))->soc_esik_mc);
}
static ssize_t soc_esik_mc_store(struct device *dev, struct device_attribute *a, const char *buf, size_t n)
{
	struct kk_sensor *k = dev_get_drvdata(dev);

	return kk_esik_yaz(k, &k->soc_esik_mc, "soc", buf, n);
}
static DEVICE_ATTR_RW(soc_esik_mc);

/* yalnız sahte kaynakta (QEMU) oluşturulur */
static ssize_t sahte_pil_adc_show(struct device *dev, struct device_attribute *a, char *buf)
{
	return sysfs_emit(buf, "%d\n", ((struct kk_sensor *)dev_get_drvdata(dev))->sahte_pil_adc);
}
static ssize_t sahte_pil_adc_store(struct device *dev, struct device_attribute *a, const char *buf, size_t n)
{
	int v;

	if (kstrtoint(buf, 0, &v))
		return -EINVAL;
	WRITE_ONCE(((struct kk_sensor *)dev_get_drvdata(dev))->sahte_pil_adc, v);
	return n;
}
static DEVICE_ATTR_RW(sahte_pil_adc);

static ssize_t sahte_soc_kod_show(struct device *dev, struct device_attribute *a, char *buf)
{
	return sysfs_emit(buf, "%u\n", ((struct kk_sensor *)dev_get_drvdata(dev))->sahte_soc_kod);
}
static ssize_t sahte_soc_kod_store(struct device *dev, struct device_attribute *a, const char *buf, size_t n)
{
	u32 v;

	if (kstrtou32(buf, 0, &v))
		return -EINVAL;
	WRITE_ONCE(((struct kk_sensor *)dev_get_drvdata(dev))->sahte_soc_kod, v);
	return n;
}
static DEVICE_ATTR_RW(sahte_soc_kod);

/* ---------------- probe ---------------- */

static int kk_sensor_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct kk_sensor *k;
	int irq, r;

	k = devm_kzalloc(dev, sizeof(*k), GFP_KERNEL);
	if (!k)
		return -ENOMEM;
	k->dev = dev;
	mutex_init(&k->kilit);
	mutex_init(&k->adc_kilit);
	init_completion(&k->adc_bitti);
	k->adc_bekleyen = -1;
	k->pil_adc = -ENODATA;
	k->hic_istek = true;
	k->pil_esik_mc = PIL_ESIK_VARSAYILAN;
	k->soc_esik_mc = SOC_ESIK_VARSAYILAN;
	k->sahte = of_property_read_bool(np, "karakutu,sahte-kaynak");
	k->sigorta_hedef = "bootloader";	/* G57 G-2/G-3: yalnız bootloader|erecovery */
	of_property_read_string(np, "karakutu,sigorta-hedef", &k->sigorta_hedef);
	if (strcmp(k->sigorta_hedef, "bootloader") && strcmp(k->sigorta_hedef, "erecovery")) {
		dev_warn(dev, "kk: sigorta: hedef '%s' izinli degil -> bootloader\n", k->sigorta_hedef);
		k->sigorta_hedef = "bootloader";
	}

	k->pmic_np = of_parse_phandle(np, "karakutu,pmic-okuma", 0);	/* yoksa V/I okunmaz */
	k->pmctrl = syscon_regmap_lookup_by_phandle(np, "karakutu,pmctrl");
	if (IS_ERR(k->pmctrl))
		k->pmctrl = NULL;
	k->sctrl = syscon_regmap_lookup_by_phandle(np, "karakutu,sctrl");
	if (IS_ERR(k->sctrl))
		k->sctrl = NULL;
	if (of_property_read_u32(np, "karakutu,r-coul-mohm", &k->r_mohm) || !k->r_mohm)
		k->r_mohm = 10;
	if (of_property_read_u32(np, "karakutu,adc-pil-kanal", &k->pil_kanal))
		k->pil_kanal = 10;
	platform_set_drvdata(pdev, k);

	if (k->sahte) {
		if (of_property_read_u32(np, "karakutu,sahte-pil-adc", (u32 *)&k->sahte_pil_adc))
			k->sahte_pil_adc = 1497;
		if (of_property_read_u32(np, "karakutu,sahte-soc-kod", &k->sahte_soc_kod))
			k->sahte_soc_kod = 500;
		device_create_file(dev, &dev_attr_sahte_pil_adc);
		device_create_file(dev, &dev_attr_sahte_soc_kod);
	} else {
		k->ipc = devm_platform_ioremap_resource(pdev, 0);
		if (IS_ERR(k->ipc))
			return dev_err_probe(dev, PTR_ERR(k->ipc), "kk: sensor: ipc yok\n");
		if (of_property_read_u32(np, "karakutu,ipc-unlock", &k->unlock))
			return dev_err_probe(dev, -EINVAL, "kk: sensor: ipc-unlock yok\n");
		irq = platform_get_irq(pdev, 0);
		if (irq < 0)
			return irq;
		/* 4.9: mailbox-0 hızlı alıcı, kendi IRQ'su (SPI 197); dağıtım tasklet'te, burada iş parçacığında */
		r = devm_request_threaded_irq(dev, irq, NULL, kk_mb_rx, IRQF_ONESHOT, "kk-lpm3-mbx0", k);
		if (r)
			return dev_err_probe(dev, r, "kk: sensor: mailbox-0 irq\n");
	}

	device_create_file(dev, &dev_attr_durum);
	device_create_file(dev, &dev_attr_pil_esik_mc);
	device_create_file(dev, &dev_attr_soc_esik_mc);

	/* G36: SoC sensörleri thermal zone olarak (DT thermal-zones; #thermal-sensor-cells = <1>) */
	k->soc_hic = true;
	for (r = 0; r <= TSENS_SAYI; r++) {
		struct thermal_zone_device *tzd;

		k->tz[r].k = k;
		k->tz[r].id = r;
		tzd = devm_thermal_of_zone_register(dev, r, &k->tz[r], &kk_tz_ops);
		if (IS_ERR(tzd))
			dev_info(dev, "kk: sensor: thermal zone %d (%s) kayit yok (%ld)\n", r,
				 r < TSENS_SAYI ? tsens_ad[r] : "soc-max", PTR_ERR(tzd));
		else
			dev_info(dev, "kk: sensor: thermal zone %d (%s) kayitli: %s\n", r,
				 r < TSENS_SAYI ? tsens_ad[r] : "soc-max", thermal_zone_device_type(tzd));
	}
	INIT_DELAYED_WORK(&k->iz, kk_iz);
	INIT_DELAYED_WORK(&k->soc_iz, kk_soc_iz);
	schedule_delayed_work(&k->soc_iz, HZ);
	schedule_delayed_work(&k->iz, 2 * HZ);
	WRITE_ONCE(kk_sensor_tek, k);
	dev_info(dev, "kk: sensor: pil V/I (SPMI okuma), pil sicakligi (LPM3 HKADC kanal %u, mailbox-16/0, 30 sn'de en fazla 1 istek, tekrar yok), SoC (SMC 0x%lx, %d sensor, 5 sn); sigorta pil>%d / SoC>%d m°C, 2 ardisik -> reboot %s%s\n",
		 k->pil_kanal, TSENS_SMC, TSENS_SAYI, k->pil_esik_mc, k->soc_esik_mc, k->sigorta_hedef,
		 k->sahte ? "; SAHTE KAYNAK (IPC/SMC kullanilmaz)" : "");
	return 0;
}

static void kk_sensor_remove(struct platform_device *pdev)
{
	struct kk_sensor *k = platform_get_drvdata(pdev);

	WRITE_ONCE(kk_sensor_tek, NULL);
	cancel_delayed_work_sync(&k->iz);
	cancel_delayed_work_sync(&k->soc_iz);
	device_remove_file(&pdev->dev, &dev_attr_durum);
	device_remove_file(&pdev->dev, &dev_attr_pil_esik_mc);
	device_remove_file(&pdev->dev, &dev_attr_soc_esik_mc);
	if (k->sahte) {
		device_remove_file(&pdev->dev, &dev_attr_sahte_pil_adc);
		device_remove_file(&pdev->dev, &dev_attr_sahte_soc_kod);
	}
	of_node_put(k->pmic_np);
}

static const struct of_device_id kk_sensor_of_match[] = {
	{ .compatible = "karakutu,kirin710-sensor-okuma" },
	{ }
};
MODULE_DEVICE_TABLE(of, kk_sensor_of_match);

static struct platform_driver kk_sensor_driver = {
	.probe = kk_sensor_probe,
	.remove = kk_sensor_remove,
	.driver = { .name = "kk-sensor-okuma", .of_match_table = kk_sensor_of_match },
};
module_platform_driver(kk_sensor_driver);
MODULE_DESCRIPTION("Kara Kutu: Kirin 710 sensor gunlugu ve sicaklik sigortasi");
MODULE_LICENSE("GPL");
