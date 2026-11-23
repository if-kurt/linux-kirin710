// SPDX-License-Identifier: GPL-2.0
/*
 * Kara Kutu (G36, Tur 3): Kirin 710 CPU frekansı — 4.9'daki hw_vote oy mekanizmasının birebir taşıması.
 *
 * 4.9 kaynağı: drivers/hisi/hw_vote/hw_vote.c (hv_reg_write/hv_reg_read, hisi_hv_set_freq, hisi_hv_get_result),
 * drivers/cpufreq/hisi/hisi_cpufreq_dt.c (hisi_cpufreq_set, hisi_cpufreq_policy_cur_init, hisi_cpufreq_get),
 * canlı DT "hw_vote" (PMCTRL fff31000): big-freq vote-src-1 = <0x270 0xff 0xff0000>, little-freq vote-src-1 =
 * <0x280 0xff 0xff0000>, sonuç result_reg = <0x2dc 0xff 0xff> (big) / <0x2dc 0xff00 0xff00> (little), ratio 16;
 * cpu düğümleri freq-vote-channel = "big-freq|little-freq", "vote-src-1".
 *
 *   oy yazma (hv_reg_write):  writel(wr_mask | ((kHz / 1000 / ratio) & 0xff) << shift, PMCTRL + oy)
 *                              = PMCTRL+0x270 (big) / 0x280 (little) <- 0x00ff0000 | (MHz / 16)
 *   ilk durum (policy_cur_init): sonuç alanı oku -> en yakın tablo frekansı (RELATION_C) -> policy->cur ve o oyu yaz
 *   .get (hisi_cpufreq_get):     policy->cur (donanım okunmaz)
 *
 * Yalnız CPU kümeleri (GPU oyu 0x290'a DOKUNULMAZ); vote-src-2 alanlarına yazılmaz; başka PMCTRL/LPM3 yazması YOK.
 * G36 eki (yalnız okuma): her oydan sonra sonuç yazmacı 0x2dc'den geri okunur (en çok 20 ms) ve log'a yazılır.
 * Frekans tabloları canlı 4.9 DT'sinin opp_table0/1'inden (G22) — DT'den verilir.
 * "karakutu,sahte-lpm3" (yalnız QEMU DT'si): LPM3 yokken sonuç alanını sürücü kendisi kopyalar (benzetim).
 */
#include <linux/cpufreq.h>
#include <linux/cpu.h>
#include <linux/delay.h>
#include <linux/ktime.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#define KK_SONUC_REG	0x2dc
#define KK_WR_MASK	0x00ff0000
#define KK_RATIO	16
#define KK_KUME_SAYI	2
#define KK_FREK_MAX	16

struct kk_kume {
	const char *ad;
	u32 oy_reg;			/* 0x280 little / 0x270 big */
	u32 sonuc_shift;		/* 8 little / 0 big */
	u32 ilk_cpu, cpu_sayi;
	struct cpufreq_frequency_table tablo[KK_FREK_MAX + 1];
	u32 n_oy, n_uyusmaz;
};

static struct regmap *kk_pmctrl;
static bool kk_sahte;
static struct kk_kume kk_kumeler[KK_KUME_SAYI];
static struct device *kk_dev;
static DEFINE_MUTEX(kk_kilit);

/* G37: yoklama 200 ms -> oy sayısı artar; geri okuma HER oyda yapılır, log satırı ise yalnız kümenin ilk 64 oyunda,
 * her FARKLI'da ve her 50. oyda (özet) yazılır (pstore konsolu açılış kanıtını taşırmasın). Toplamlar sysfs'te. */
static unsigned int kk_oy_toplam, kk_uyusmaz_toplam, kk_en_uzun_us;
module_param_named(oy_toplam, kk_oy_toplam, uint, 0444);
module_param_named(uyusmaz_toplam, kk_uyusmaz_toplam, uint, 0444);
module_param_named(en_uzun_us, kk_en_uzun_us, uint, 0444);

static struct kk_kume *kk_kume_bul(unsigned int cpu)
{
	int i;

	for (i = 0; i < KK_KUME_SAYI; i++)
		if (cpu >= kk_kumeler[i].ilk_cpu && cpu < kk_kumeler[i].ilk_cpu + kk_kumeler[i].cpu_sayi)
			return &kk_kumeler[i];
	return NULL;
}

static u32 kk_sonuc_alan(struct kk_kume *c, u32 *ham)
{
	u32 v = 0;

	regmap_read(kk_pmctrl, KK_SONUC_REG, &v);
	if (ham)
		*ham = v;
	return (v >> c->sonuc_shift) & 0xff;
}

/* hv_reg_write birebir: değer alanı 8 bit, taşarsa 0xff'e kırpılır */
static void kk_oy_yaz(struct kk_kume *c, unsigned int khz, unsigned int eski_khz)
{
	u32 oy = khz / 1000 / KK_RATIO, yaz, alan = 0, ham = 0, oyreg = 0;
	ktime_t t0;
	s64 us;
	int i;

	if (oy > 0xff)
		oy = 0xff;
	yaz = KK_WR_MASK | (oy & 0xff);
	t0 = ktime_get();
	regmap_write(kk_pmctrl, c->oy_reg, yaz);
	if (kk_sahte)	/* QEMU: LPM3 benzetimi */
		regmap_update_bits(kk_pmctrl, KK_SONUC_REG, 0xff << c->sonuc_shift, oy << c->sonuc_shift);
	for (i = 0; i < 40; i++) {
		alan = kk_sonuc_alan(c, &ham);
		if (alan == oy)
			break;
		usleep_range(400, 600);
	}
	us = ktime_us_delta(ktime_get(), t0);
	regmap_read(kk_pmctrl, c->oy_reg, &oyreg);
	c->n_oy++;
	kk_oy_toplam++;
	if (us > kk_en_uzun_us)
		kk_en_uzun_us = us;
	if (alan != oy) {
		c->n_uyusmaz++;
		kk_uyusmaz_toplam++;
	}
	if (c->n_oy <= 64 || alan != oy || !(c->n_oy % 50))
		dev_info(kk_dev, "kk: cpufreq: %s %u -> %u kHz: oy %u (0x%03x <- 0x%08x, geri 0x%08x); 0x2dc=0x%08x alan=%u beklenen=%u %s (%lld us) [oy %u, uyusmaz %u]\n",
			 c->ad, eski_khz, khz, oy, c->oy_reg, yaz, oyreg, ham, alan, oy,
			 alan == oy ? "AYNI" : "FARKLI", us, c->n_oy, c->n_uyusmaz);
}

static int kk_target_index(struct cpufreq_policy *policy, unsigned int index)
{
	struct kk_kume *c = policy->driver_data;

	mutex_lock(&kk_kilit);
	kk_oy_yaz(c, policy->freq_table[index].frequency, policy->cur);
	mutex_unlock(&kk_kilit);
	return 0;
}

static unsigned int kk_get(unsigned int cpu)
{
	struct cpufreq_policy *policy = cpufreq_cpu_get_raw(cpu);

	return policy ? policy->cur : 0;	/* 4.9 hisi_cpufreq_get */
}

static int kk_init(struct cpufreq_policy *policy)
{
	struct kk_kume *c = kk_kume_bul(policy->cpu);
	unsigned int i, khz, idx;
	u32 ham;

	if (!c)
		return -ENODEV;
	for (i = 0; i < c->cpu_sayi; i++)
		cpumask_set_cpu(c->ilk_cpu + i, policy->cpus);
	policy->driver_data = c;
	policy->freq_table = c->tablo;
	policy->cpuinfo.transition_latency = 2000000;	/* 4.9: 2 ms */

	/* hisi_cpufreq_policy_cur_init: sonuç -> en yakın tablo frekansı -> o oyu yaz */
	khz = kk_sonuc_alan(c, &ham) * 1000 * KK_RATIO;
	/* RELATION_C: en yakın tablo frekansı (çekirdeğin sıralanmamış tablo yolu init'te policy->min/max'a dayanıyor) */
	for (idx = 0, i = 1; c->tablo[i].frequency != CPUFREQ_TABLE_END; i++)
		if (abs((int)c->tablo[i].frequency - (int)khz) < abs((int)c->tablo[idx].frequency - (int)khz))
			idx = i;
	policy->cur = c->tablo[idx].frequency;
	dev_info(kk_dev, "kk: cpufreq: %s (cpu %u-%u) ilk sonuc 0x2dc=0x%08x -> %u kHz, en yakin tablo %u kHz\n",
		 c->ad, c->ilk_cpu, c->ilk_cpu + c->cpu_sayi - 1, ham, khz, policy->cur);
	mutex_lock(&kk_kilit);
	kk_oy_yaz(c, policy->cur, khz);
	mutex_unlock(&kk_kilit);
	return 0;
}

static struct cpufreq_driver kk_cpufreq_driver = {
	.name		= "kk-kirin710",
	.flags		= CPUFREQ_IS_COOLING_DEV | CPUFREQ_HAVE_GOVERNOR_PER_POLICY,
	.verify		= cpufreq_generic_frequency_table_verify,
	.target_index	= kk_target_index,
	.get		= kk_get,
	.init		= kk_init,
	.attr		= cpufreq_generic_attr,
};

static int kk_kume_oku(struct device_node *np, int i, const char *ad)
{
	struct kk_kume *c = &kk_kumeler[i];
	char p[40];
	u32 v[KK_FREK_MAX];
	int n, j;

	c->ad = ad;
	snprintf(p, sizeof(p), "karakutu,%s-khz", ad);
	n = of_property_count_u32_elems(np, p);
	if (n <= 0 || n > KK_FREK_MAX || of_property_read_u32_array(np, p, v, n))
		return -EINVAL;
	for (j = 0; j < n; j++) {
		c->tablo[j].driver_data = j;
		c->tablo[j].frequency = v[j];
	}
	c->tablo[n].frequency = CPUFREQ_TABLE_END;
	snprintf(p, sizeof(p), "karakutu,%s-oy", ad);
	if (of_property_read_u32(np, p, &c->oy_reg))
		return -EINVAL;
	if (c->oy_reg != 0x270 && c->oy_reg != 0x280)	/* yalnız CPU kümeleri; GPU 0x290'a asla */
		return -EPERM;
	snprintf(p, sizeof(p), "karakutu,%s-sonuc-shift", ad);
	if (of_property_read_u32(np, p, &c->sonuc_shift))
		return -EINVAL;
	snprintf(p, sizeof(p), "karakutu,%s-cpular", ad);
	if (of_property_read_u32_index(np, p, 0, &c->ilk_cpu) || of_property_read_u32_index(np, p, 1, &c->cpu_sayi))
		return -EINVAL;
	return 0;
}

static int kk_cpufreq_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	int r;

	kk_dev = &pdev->dev;
	kk_pmctrl = syscon_regmap_lookup_by_phandle(np, "karakutu,pmctrl");
	if (IS_ERR(kk_pmctrl))
		return dev_err_probe(&pdev->dev, PTR_ERR(kk_pmctrl), "kk: cpufreq: pmctrl yok\n");
	kk_sahte = of_property_read_bool(np, "karakutu,sahte-lpm3");
	r = kk_kume_oku(np, 0, "little");
	if (!r)
		r = kk_kume_oku(np, 1, "big");
	if (r)
		return dev_err_probe(&pdev->dev, r, "kk: cpufreq: DT tablolari hatali\n");
	dev_info(&pdev->dev, "kk: cpufreq: 4.9 hw_vote (vote-src-1) oy yazmaclari little 0x%03x / big 0x%03x, sonuc 0x2dc; GPU oyuna dokunulmaz%s\n",
		 kk_kumeler[0].oy_reg, kk_kumeler[1].oy_reg, kk_sahte ? "; SAHTE LPM3 (QEMU)" : "");
	return cpufreq_register_driver(&kk_cpufreq_driver);
}

static void kk_cpufreq_remove(struct platform_device *pdev)
{
	cpufreq_unregister_driver(&kk_cpufreq_driver);
}

static const struct of_device_id kk_cpufreq_of_match[] = {
	{ .compatible = "karakutu,kirin710-cpufreq" },
	{ }
};
MODULE_DEVICE_TABLE(of, kk_cpufreq_of_match);

static struct platform_driver kk_cpufreq_platdrv = {
	.probe = kk_cpufreq_probe,
	.remove = kk_cpufreq_remove,
	.driver = { .name = "kk-kirin710-cpufreq", .of_match_table = kk_cpufreq_of_match },
};
module_platform_driver(kk_cpufreq_platdrv);
MODULE_DESCRIPTION("Kara Kutu: Kirin 710 hw_vote cpufreq (4.9 birebir)");
MODULE_LICENSE("GPL");
