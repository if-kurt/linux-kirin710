// SPDX-License-Identifier: GPL-2.0
/*
 * Kara Kutu G44 (A.2): güç teşhisi - YALNIZ OKUMA durum dökümü.
 *
 * Bootloader'ın ana hatta açık bıraktığı saat kapılarını, güç alanlarını, DDR/GPU/CPU frekans indeksini ve
 * çevre gerilim oylarını iki kez döker: probe'da (açılış) ve KK_IKINCI_SN sonra (Debian açık, boşta).
 * HİÇBİR YAZMA YOK: writel/regmap_write yok; bölge talep edilmez (CRG/SCTRL/PMCTRL başka sürücülerin de).
 * Sonuç "kk: guc: ..." satırları ve salt okunur sysfs "durum" (önbellek; okuma donanıma gitmez).
 *
 * Okunan yazmaçlar (hepsi her zaman erişilebilir AO/PERI durum yazmaçları; NS sınırı CRG < 0xC00,
 * SCTRL < 0x800; lpmcu bölgesi, DDRC/DMSS, alt sistemlerin iç blokları OKUNMAZ):
 *  CRG fff35000  PERCLKEN/PERSTAT 0,1,2,4,7 + PERRSTSTAT 0,1,2,4   G43 M0'da cihazda okundu (aynı yazmaçlar)
 *                PERPWRSTAT 0x158  stok 4.9 drivers/hisi/isp/nonsec.c:97,115 (ISP açma yolu)
 *                PERPWRACK  0x15C  4.9 drivers/hisi/pm/pm.c:219 (askıya alma yolu; v5.1'de HISI_SR=y)
 *  SCTRL fff0a000 SCPERCLKEN0/SCPERSTAT0 0x168/0x16C (G43 M0), SCPERRSTSTAT0 0x208 (4.9 sdhci_mshc_libra.c:300),
 *                SCBAKDATA4 0x41C: 4.9 clk-kirin-common.c:1155 (ddrfreq cur_freq yolu; [3:0] küçük küme,
 *                [7:4] büyük küme, [11:8] DDR, [15:12] GPU frekans indeksi)
 *  PMCTRL fff31000 0x2DC VOTE_RESULT0 (4.9 hw_vote.c:93; ana hat cpufreq sürücüsü de okuyor),
 *                0x350 PERI_CTRL4 [29:28] çevre VDD (hisi_peri_dvfs_volt.c:323), 0x354 PERI_CTRL5 IP başına 2 bit
 *                gerilim oyu (hisi_peri_dvfs_volt.c:344; alanlar canlı DT pvp_*).
 * OKUNMAYANLAR (4.9 yalnız hata dökümünde okuyor ya da hiç okumuyor; karar bekliyor): ISOSTAT 0x14C,
 *  PERSTAT3/5/6, PERRSTSTAT3/5, PMCTRL NOC_POWER_IDLE* 0x380-0x388, SCPERSTATUS6, media1 CRG ve DSS iç yazmaçları
 *  (media1 saat kapısı PERSTAT6'da; o okunmadığı için kapı doğrulanamaz -> kural gereği okunmaz).
 */
#include <linux/devm-helpers.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>

/* ikinci döküm: Debian açılmış ve boşta (sn; en az 5). Yalnız QEMU senaryosu cmdline'da kısaltır. */
static unsigned int ikinci_sn = 300;
module_param(ikinci_sn, uint, 0444);
#define KK_IKINCI_SN	max(ikinci_sn, 5U)

/* CRG */
static const u16 kk_crg_en[] = { 0x008, 0x018, 0x028, 0x048, 0x428 };	/* PERCLKEN0,1,2,4,7 */
static const u8 kk_crg_no[] = { 0, 1, 2, 4, 7 };
static const u16 kk_crg_rst[] = { 0x068, 0x074, 0x080, 0x098 };	/* PERRSTSTAT0,1,2,4 */
static const u8 kk_crg_rst_no[] = { 0, 1, 2, 4 };
#define CRG_PERPWRSTAT	0x158
#define CRG_PERPWRACK	0x15c
/* SCTRL */
#define SC_PERCLKEN0	0x168
#define SC_PERSTAT0	0x16c
#define SC_PERRSTSTAT0	0x208
#define SC_BAKDATA4	0x41c
/* PMCTRL */
#define PM_VOTE_RESULT0	0x2dc
#define PM_PERI_CTRL4	0x350
#define PM_PERI_CTRL5	0x354

enum { B_CRG, B_SCTRL, B_PMCTRL, B_SAYI };
static const char * const kk_blok_ad[B_SAYI] = { "crg", "sctrl", "pmctrl" };
static const u32 kk_blok_min[B_SAYI] = { 0x430, 0x420, 0x358 };
/* NS okuma sınırları (ARASTIRMA.md §2): bunların üstü güvenli bölge */
static const u32 kk_blok_max[B_SAYI] = { 0xc00, 0x800, 0x1000 };

/* PERPWRSTAT (soc_crgperiph_interface.h:5835-5871) */
static const struct { u8 bit; const char *ad; } kk_pwr[] = {
	{ 0, "isp" }, { 1, "venc" }, { 2, "vdec" }, { 5, "media1" }, { 8, "cnn" }, { 10, "cci" },
	{ 19, "modemperi" }, { 20, "modemirm" }, { 21, "ivp32dsp" }, { 22, "isp_srt" }, { 23, "isp_bpe" },
};
/* PERI_CTRL5 2 bitlik oy alanları (canlı DT pvp_*, kaydırma = alanın ilk biti) */
static const struct { u8 sh; const char *ad; } kk_pvp[] = {
	{ 0, "edc0(dss)" }, { 2, "vdec" }, { 4, "venc" }, { 6, "ispcpu" }, { 8, "ispfunc" }, { 10, "ivpcore" },
	{ 12, "usb2otg" }, { 14, "coresight" }, { 16, "secs" }, { 28, "hold" },
};
/* 4.9 ddrfreq tablosu (ARASTIRMA.md §5; yalnız yorum için, ham indeks de loglanır) */
static const u16 kk_ddr_mhz[] = { 203, 451, 902, 1245, 1622 };

struct kk_guc {
	struct device *dev;
	void __iomem *b[B_SAYI];
	struct delayed_work ikinci;
	unsigned int n_dokum;
	struct mutex kilit;
	char onbellek[PAGE_SIZE];
	size_t uzunluk;
};

static __printf(2, 3) void kk_log(struct kk_guc *g, const char *fmt, ...)
{
	char satir[320];
	va_list ap;

	va_start(ap, fmt);
	vscnprintf(satir, sizeof(satir), fmt, ap);
	va_end(ap);
	pr_info("kk: guc: %s\n", satir);
	mutex_lock(&g->kilit);
	g->uzunluk += scnprintf(g->onbellek + g->uzunluk, sizeof(g->onbellek) - g->uzunluk, "%s\n", satir);
	mutex_unlock(&g->kilit);
}

static u32 kk_rd(struct kk_guc *g, int blok, u32 off)
{
	if (WARN_ON_ONCE(off + 4 > kk_blok_max[blok] || off + 4 > kk_blok_min[blok]))
		return 0xdeadbeef;	/* listede olmayan ofset: okunmaz */
	return readl(g->b[blok] + off);
}

#define B(v, n)	(((v) >> (n)) & 1)

static void kk_dokum(struct kk_guc *g, const char *an)
{
	u32 en[ARRAY_SIZE(kk_crg_en)], st[ARRAY_SIZE(kk_crg_en)], rs[ARRAY_SIZE(kk_crg_rst)];
	u32 pwr, ack, scce, scst, scrs, bak, vote, p4, p5;
	char t[200];
	int i, o;
	unsigned int di;

	g->n_dokum++;
	for (i = 0; i < ARRAY_SIZE(kk_crg_en); i++) {
		en[i] = kk_rd(g, B_CRG, kk_crg_en[i]);
		st[i] = kk_rd(g, B_CRG, kk_crg_en[i] + 4);	/* PERSTATn = PERCLKENn + 4 */
	}
	for (i = 0; i < ARRAY_SIZE(kk_crg_rst); i++)
		rs[i] = kk_rd(g, B_CRG, kk_crg_rst[i]);
	pwr = kk_rd(g, B_CRG, CRG_PERPWRSTAT);
	ack = kk_rd(g, B_CRG, CRG_PERPWRACK);
	scce = kk_rd(g, B_SCTRL, SC_PERCLKEN0);
	scst = kk_rd(g, B_SCTRL, SC_PERSTAT0);
	scrs = kk_rd(g, B_SCTRL, SC_PERRSTSTAT0);
	bak = kk_rd(g, B_SCTRL, SC_BAKDATA4);
	vote = kk_rd(g, B_PMCTRL, PM_VOTE_RESULT0);
	p4 = kk_rd(g, B_PMCTRL, PM_PERI_CTRL4);
	p5 = kk_rd(g, B_PMCTRL, PM_PERI_CTRL5);

	kk_log(g, "dokum %u (%s)", g->n_dokum, an);
	for (i = 0; i < ARRAY_SIZE(kk_crg_en); i++)
		kk_log(g, "crg PERCLKEN%u=0x%08x PERSTAT%u=0x%08x (acik bit %u; CLKEN!=STAT 0x%08x)", kk_crg_no[i],
		       en[i], kk_crg_no[i], st[i], hweight32(st[i]), en[i] ^ st[i]);
	kk_log(g, "crg PERRSTSTAT%u=0x%08x PERRSTSTAT%u=0x%08x PERRSTSTAT%u=0x%08x PERRSTSTAT%u=0x%08x (1 = resette)",
	       kk_crg_rst_no[0], rs[0], kk_crg_rst_no[1], rs[1], kk_crg_rst_no[2], rs[2], kk_crg_rst_no[3], rs[3]);
	o = 0;
	for (i = 0; i < ARRAY_SIZE(kk_pwr); i++)
		o += scnprintf(t + o, sizeof(t) - o, " %s=%u", kk_pwr[i].ad, B(pwr, kk_pwr[i].bit));
	kk_log(g, "crg PERPWRSTAT=0x%08x (1 = guc acik):%s; cekirdekler[18:11]=0x%02x", pwr, t, (pwr >> 11) & 0xff);
	kk_log(g, "crg PERPWRACK=0x%08x media(b5)=%u g3d(b6,b7,b20-25)=0x%x g3dcg0en(b26)=%u modem(b27,b28)=%u%u ivp(b29)=%u cekirdekler[18:11]=0x%02x",
	       ack, B(ack, 5), ((ack >> 6) & 3) | (((ack >> 20) & 0x3f) << 2), B(ack, 26), B(ack, 27), B(ack, 28),
	       B(ack, 29), (ack >> 11) & 0xff);
	kk_log(g, "sctrl SCPERCLKEN0=0x%08x SCPERSTAT0=0x%08x (acik bit %u) SCPERRSTSTAT0=0x%08x asp_rst[10:7]=0x%x",
	       scce, scst, hweight32(scst), scrs, (scrs >> 7) & 0xf);
	di = (bak >> 8) & 0xf;
	if (di < ARRAY_SIZE(kk_ddr_mhz))
		scnprintf(t, sizeof(t), " (%u MHz, 4.9 tablosu)", kk_ddr_mhz[di]);
	else
		scnprintf(t, sizeof(t), " (4.9 tablosu disi; 4.9 bunu 0'a dusurur)");
	kk_log(g, "sctrl SCBAKDATA4=0x%08x frekans indeksi: kucuk=%u buyuk=%u DDR=%u%s GPU=%u",
	       bak, bak & 0xf, (bak >> 4) & 0xf, di, t, (bak >> 12) & 0xf);
	kk_log(g, "pmctrl VOTE_RESULT0=0x%08x buyuk=%u kucuk=%u gpu=%u (x16 MHz)", vote, vote & 0xff,
	       (vote >> 8) & 0xff, (vote >> 16) & 0xff);
	o = 0;
	for (i = 0; i < ARRAY_SIZE(kk_pvp); i++)
		o += scnprintf(t + o, sizeof(t) - o, " %s=%u", kk_pvp[i].ad, (p5 >> kk_pvp[i].sh) & 3);
	kk_log(g, "pmctrl PERI_CTRL4=0x%08x cevre_vdd[29:28]=%u onoff[31:30]=%u; PERI_CTRL5=0x%08x oylar:%s",
	       p4, (p4 >> 28) & 3, (p4 >> 30) & 3, p5, t);
	kk_log(g, "dokum %u bitti (yazma yok; ISOSTAT/PERSTAT3,5,6/NoC/media1/DSS OKUNMADI: 4.9 yalniz hata dokumunde okuyor, karar bekliyor)",
	       g->n_dokum);
}

static void kk_ikinci(struct work_struct *w)
{
	struct kk_guc *g = container_of(to_delayed_work(w), struct kk_guc, ikinci);

	kk_dokum(g, "Debian acik, bosta");
}

static ssize_t durum_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct kk_guc *g = dev_get_drvdata(dev);
	ssize_t n;

	mutex_lock(&g->kilit);	/* yalnız önbellek; donanıma gidilmez */
	n = sysfs_emit(buf, "%s", g->onbellek);
	mutex_unlock(&g->kilit);
	return n;
}
static DEVICE_ATTR_RO(durum);

static struct attribute *kk_guc_attrs[] = { &dev_attr_durum.attr, NULL };
ATTRIBUTE_GROUPS(kk_guc);

static int kk_guc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct kk_guc *g;
	int i, ret;

	g = devm_kzalloc(dev, sizeof(*g), GFP_KERNEL);
	if (!g)
		return -ENOMEM;
	g->dev = dev;
	mutex_init(&g->kilit);
	platform_set_drvdata(pdev, g);

	for (i = 0; i < B_SAYI; i++) {
		struct resource r;
		int idx = of_property_match_string(np, "reg-names", kk_blok_ad[i]);

		if (idx < 0 || of_address_to_resource(np, idx, &r))
			return dev_err_probe(dev, -EINVAL, "kk: guc: reg '%s' yok\n", kk_blok_ad[i]);
		if (resource_size(&r) < kk_blok_min[i] || resource_size(&r) > kk_blok_max[i])
			return dev_err_probe(dev, -EINVAL, "kk: guc: reg '%s' boyutu 0x%llx (0x%x..0x%x olmali)\n",
					     kk_blok_ad[i], (u64)resource_size(&r), kk_blok_min[i], kk_blok_max[i]);
		g->b[i] = devm_ioremap(dev, r.start, resource_size(&r));
		if (!g->b[i])
			return -ENOMEM;
	}

	kk_log(g, "basladi (yalniz okuma; yazma yolu yok); ikinci dokum %u sn sonra", KK_IKINCI_SN);
	kk_dokum(g, "acilis");
	ret = devm_delayed_work_autocancel(dev, &g->ikinci, kk_ikinci);
	if (ret)
		return ret;
	schedule_delayed_work(&g->ikinci, KK_IKINCI_SN * HZ);
	return 0;
}

static const struct of_device_id kk_guc_of_match[] = {
	{ .compatible = "karakutu,guc-teshis" },
	{ }
};
MODULE_DEVICE_TABLE(of, kk_guc_of_match);

static struct platform_driver kk_guc_driver = {
	.probe = kk_guc_probe,
	.driver = {
		.name = "kk-guc-teshis",
		.of_match_table = kk_guc_of_match,
		.dev_groups = kk_guc_groups,
	},
};
module_platform_driver(kk_guc_driver);

MODULE_DESCRIPTION("Kara Kutu: guc teshisi salt okuma durum dokumu");
MODULE_LICENSE("GPL");
