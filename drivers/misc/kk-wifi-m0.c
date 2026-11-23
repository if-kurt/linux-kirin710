// SPDX-License-Identifier: GPL-2.0
/*
 * Kara Kutu G42 (Wi-Fi dilim 2): Hi1102 SDIO M0 - YALNIZ OKUMA durum dökümü.
 *
 * Hi1102'yi açmak için gereken saat kapısı / reset / pinmux / GPIO / PMIC 32 kHz durumunu bir kez
 * (probe'da) okur, "kk: wifi-m0: ..." satırlarıyla döker ve sysfs "durum"da (salt okunur önbellek;
 * okuma donanıma gitmez) sunar. HİÇBİR YAZMA YOK: writel/regmap_write/regmap_update_bits yok.
 *
 * Kural: saati kapalı ya da resette olan bloğun yazmacı OKUNMAZ. Önce CRG/SCTRL durum yazmaçları
 * (her zaman erişilebilir) okunur; bir blok yalnız kapısı açık (PERSTAT = 1) VE reset'i bırakılmış
 * (PERRSTSTAT = 0) ise okunur. PERCLKEN (yazılımın istediği) yalnız loglanır; karar PERSTAT'tan
 * (donanımın gerçek durumu) verilir -> çekingen: CLKEN=1, STAT=0 ise okunmaz.
 *
 * Kaynaklar (4.9): drivers/hisi/ap/platform/kirin710/soc_crgperiph_interface.h (ofset ve bitler),
 * soc_sctrl_interface.h, soc_acpu_baseaddr_interface.h; drivers/clk/hisi/hisi-clkgate.c
 * (hi3xxx kapı durumu = açma ofseti + 0x8 = PERCLKEN; PERSTAT = +0xc); canlı DT (G17).
 *
 *  Blok          Adres        Okunma koşulu
 *  iomg-mmc1     fc182000     PERSTAT0 b15 clk_mmc1_peribus, PERRSTSTAT0 b11 prst_mmc1pericfg=0,
 *  iocg-mmc1     fc182800       PERSTAT7 b21 pclk_mmc1_ioc, PERRSTSTAT4 b10 prst_mmc1_ioc=0
 *  iomg (GPIO3_6) e896c000    PERSTAT2 b25 pclk_ioc, PERRSTSTAT2 b25 prst_ioc=0
 *  gpio3         e8a0e000     PERSTAT1 b3 pclk_gpio3, PERRSTSTAT1 b3 prst_gpio3=0
 *  dwmmc2        fc183000     mmc1 veri yolu (yukarıdaki ilk iki koşul) + PERSTAT0 b21 hclk_sdio
 *                             + PERSTAT4 b19 clk_sdio + PERRSTSTAT4 b20 hrst_sdio=0
 *                             (b26 ip_rst_sdio yalnız loglanır; 4.9 libra yalnız b20'yi kullanır)
 *  PMIC 0x3e     SPMI sid 0   kk-spmi-syscon regmap'i (karakutu,pmic-okuma) varsa regmap_read
 */
#include <linux/devm-helpers.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/spmi.h>
#include <linux/workqueue.h>

/* CRG (fff35000) */
#define CRG_PERCLKEN0	0x008
#define CRG_PERSTAT0	0x00c
#define CRG_PERCLKEN1	0x018
#define CRG_PERSTAT1	0x01c
#define CRG_PERCLKEN2	0x028
#define CRG_PERSTAT2	0x02c
#define CRG_PERCLKEN4	0x048
#define CRG_PERSTAT4	0x04c
#define CRG_PERRSTSTAT0	0x068
#define CRG_PERRSTSTAT1	0x074
#define CRG_PERRSTSTAT2	0x080
#define CRG_PERRSTSTAT4	0x098
#define CRG_CLKDIV6	0x0c0	/* [3:0] div_sdio1, [5:4] PLL seçimi (muxpl), [6] sys<->PLL (muxsy) */
#define CRG_CLKDIV19	0x0f4	/* b6 sc_gt_clk_sdio1_sys, b8 sc_gt_clk_sdio1 (b3 = SD, SDIO değil) */
#define CRG_PERCLKEN7	0x428
#define CRG_PERSTAT7	0x42c

/* SCTRL (fff0a000) */
#define SC_PERCLKEN0	0x168
#define SC_PERSTAT0	0x16c	/* b21 pclk_ao_gpio4 (Wi-Fi wake pini, M2) */

/* dw_mmc */
#define DW_CTRL		0x00
#define DW_PWREN	0x04
#define DW_CLKDIV	0x08
#define DW_CLKENA	0x10
#define DW_CTYPE	0x18
#define DW_VERID	0x6c

/* PL061 */
#define PL061_DATA_ALL	0x3fc	/* adres maskesi = 0xff: 8 bitin hepsi */
#define PL061_DIR	0x400

#define PMIC_CLK32K	0x3e	/* clk_pmu32kb kapısı, bit0 */
#define KK_PMIC_DENEME	10	/* 10 x 500 ms: kk-spmi-syscon bağlanmasını bekle */

enum { B_CRG, B_SCTRL, B_IOMG_MMC1, B_IOCG_MMC1, B_IOMG, B_GPIO3, B_DWMMC2, B_SAYI };
static const char * const kk_blok_ad[B_SAYI] = {
	"crg", "sctrl", "iomg-mmc1", "iocg-mmc1", "iomg", "gpio3", "dwmmc2",
};
/* Haritalanan boyut; okunan en büyük ofset bunun içinde olmalı. */
static const u32 kk_blok_min[B_SAYI] = { 0x430, 0x170, 0x18, 0x18, 0x60, 0x400 + 4, 0x70 };

struct kk_m0 {
	struct device *dev;
	void __iomem *b[B_SAYI];
	struct device_node *pmic_np;
	struct delayed_work pmic_is;
	int pmic_deneme;
	struct mutex kilit;		/* önbellek */
	char onbellek[PAGE_SIZE];
	size_t uzunluk;
};

static __printf(2, 3) void kk_log(struct kk_m0 *m, const char *fmt, ...)
{
	char satir[256];
	va_list ap;

	va_start(ap, fmt);
	vscnprintf(satir, sizeof(satir), fmt, ap);
	va_end(ap);
	pr_info("kk: wifi-m0: %s\n", satir);
	mutex_lock(&m->kilit);
	m->uzunluk += scnprintf(m->onbellek + m->uzunluk, sizeof(m->onbellek) - m->uzunluk,
				"%s\n", satir);
	mutex_unlock(&m->kilit);
}

static inline u32 kk_rd(struct kk_m0 *m, int blok, u32 off)
{
	return readl(m->b[blok] + off);
}

#define B(v, n)	(((v) >> (n)) & 1)

static void kk_pmic_oku(struct work_struct *w)
{
	struct kk_m0 *m = container_of(to_delayed_work(w), struct kk_m0, pmic_is);
#if IS_ENABLED(CONFIG_SPMI) && IS_ENABLED(CONFIG_REGMAP)
	struct spmi_device *sd;
	struct regmap *rm = NULL;
	unsigned int v = 0;
	int r;

	sd = spmi_find_device_by_of_node(m->pmic_np);
	if (!sd) {
		kk_log(m, "pmic[0x%02x] OKUNMADI: %pOF bir SPMI aygiti degil ya da yok", PMIC_CLK32K, m->pmic_np);
		return;
	}
	rm = dev_get_regmap(&sd->dev, NULL);
	if (!rm) {
		put_device(&sd->dev);
		if (++m->pmic_deneme < KK_PMIC_DENEME) {
			schedule_delayed_work(&m->pmic_is, msecs_to_jiffies(500));
			return;
		}
		kk_log(m, "pmic[0x%02x] OKUNMADI: kk-spmi-syscon regmap'i %d sn icinde hazir olmadi",
		       PMIC_CLK32K, KK_PMIC_DENEME / 2);
		return;
	}
	r = regmap_read(rm, PMIC_CLK32K, &v);	/* yalnız okuma; oku-değiştir-yaz YOK */
	put_device(&sd->dev);
	if (r)
		kk_log(m, "pmic[0x%02x] OKUNMADI: regmap_read hata %d", PMIC_CLK32K, r);
	else
		kk_log(m, "pmic[0x%02x]=0x%02x clk_pmu32kb(b0)=%u (32 kHz %s)", PMIC_CLK32K, v,
		       B(v, 0), B(v, 0) ? "acik" : "KAPALI");
#else
	kk_log(m, "pmic[0x%02x] OKUNMADI: CONFIG_SPMI/REGMAP yok", PMIC_CLK32K);
#endif
}

static void kk_dokum(struct kk_m0 *m)
{
	u32 ce0, st0, ce1, st1, ce2, st2, ce4, st4, ce7, st7, r0, r1, r2, r4, d6, d19, scce, scst;
	bool mmc1_yol, ioc_mmc1, ioc, gpio3, sdio;
	int i;

	/* 1) Durum yazmaçları: CRG ve SCTRL her zaman erişilebilir. */
	ce0 = kk_rd(m, B_CRG, CRG_PERCLKEN0);	st0 = kk_rd(m, B_CRG, CRG_PERSTAT0);
	ce1 = kk_rd(m, B_CRG, CRG_PERCLKEN1);	st1 = kk_rd(m, B_CRG, CRG_PERSTAT1);
	ce2 = kk_rd(m, B_CRG, CRG_PERCLKEN2);	st2 = kk_rd(m, B_CRG, CRG_PERSTAT2);
	ce4 = kk_rd(m, B_CRG, CRG_PERCLKEN4);	st4 = kk_rd(m, B_CRG, CRG_PERSTAT4);
	ce7 = kk_rd(m, B_CRG, CRG_PERCLKEN7);	st7 = kk_rd(m, B_CRG, CRG_PERSTAT7);
	r0 = kk_rd(m, B_CRG, CRG_PERRSTSTAT0);	r1 = kk_rd(m, B_CRG, CRG_PERRSTSTAT1);
	r2 = kk_rd(m, B_CRG, CRG_PERRSTSTAT2);	r4 = kk_rd(m, B_CRG, CRG_PERRSTSTAT4);
	d6 = kk_rd(m, B_CRG, CRG_CLKDIV6);	d19 = kk_rd(m, B_CRG, CRG_CLKDIV19);
	scce = kk_rd(m, B_SCTRL, SC_PERCLKEN0);	scst = kk_rd(m, B_SCTRL, SC_PERSTAT0);

	kk_log(m, "crg PERCLKEN0=0x%08x PERSTAT0=0x%08x PERCLKEN1=0x%08x PERSTAT1=0x%08x PERCLKEN2=0x%08x PERSTAT2=0x%08x",
	       ce0, st0, ce1, st1, ce2, st2);
	kk_log(m, "crg PERCLKEN4=0x%08x PERSTAT4=0x%08x PERCLKEN7=0x%08x PERSTAT7=0x%08x",
	       ce4, st4, ce7, st7);
	kk_log(m, "crg PERRSTSTAT0=0x%08x PERRSTSTAT1=0x%08x PERRSTSTAT2=0x%08x PERRSTSTAT4=0x%08x",
	       r0, r1, r2, r4);
	kk_log(m, "crg CLKDIV6(0xc0)=0x%08x div_sdio1=%u (/%u) pll_sec=%u muxsy(b6)=%u [%s]; CLKDIV19(0xf4)=0x%08x gt_sdio1_sys(b6)=%u gt_sdio1(b8)=%u",
	       d6, d6 & 0xf, (d6 & 0xf) + 1, (d6 >> 4) & 3, B(d6, 6), B(d6, 6) ? "PLL/div" : "sys 19.2/6",
	       d19, B(d19, 6), B(d19, 8));
	kk_log(m, "sctrl SCPERCLKEN0=0x%08x SCPERSTAT0=0x%08x pclk_ao_gpio4(b21) en=%u st=%u",
	       scce, scst, B(scce, 21), B(scst, 21));
	kk_log(m, "cozum hclk_sdio en=%u st=%u; clk_sdio en=%u st=%u; sdio_hrst(b20)=%u sdio_rst(b26)=%u; mmc1_peribus en=%u st=%u; prst_mmc1pericfg=%u",
	       B(ce0, 21), B(st0, 21), B(ce4, 19), B(st4, 19), B(r4, 20), B(r4, 26),
	       B(ce0, 15), B(st0, 15), B(r0, 11));
	kk_log(m, "cozum pclk_mmc1_ioc en=%u st=%u prst_mmc1_ioc=%u; pclk_ioc en=%u st=%u prst_ioc=%u; pclk_gpio3 en=%u st=%u prst_gpio3=%u",
	       B(ce7, 21), B(st7, 21), B(r4, 10), B(ce2, 25), B(st2, 25), B(r2, 25),
	       B(ce1, 3), B(st1, 3), B(r1, 3));

	/* 2) Kararlar (yalnız PERSTAT/PERRSTSTAT). */
	mmc1_yol = B(st0, 15) && !B(r0, 11);
	ioc_mmc1 = mmc1_yol && B(st7, 21) && !B(r4, 10);
	ioc = B(st2, 25) && !B(r2, 25);
	gpio3 = B(st1, 3) && !B(r1, 3);
	sdio = mmc1_yol && B(st0, 21) && B(st4, 19) && !B(r4, 20);

	/* 3) Bloklar. */
	if (ioc_mmc1) {
		u32 mx[6], cf[6];

		for (i = 0; i < 6; i++) {
			mx[i] = kk_rd(m, B_IOMG_MMC1, i * 4);
			cf[i] = kk_rd(m, B_IOCG_MMC1, i * 4);
		}
		kk_log(m, "iomg-mmc1 fc182000+0x00..0x14 = %x %x %x %x %x %x (1=SDIO, 0=GPIO; fonk maske 0x7)",
		       mx[0], mx[1], mx[2], mx[3], mx[4], mx[5]);
		kk_log(m, "iocg-mmc1 fc182800+0x00..0x14 = 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x (b0 pu, b1 pd, [6:4] surus)",
		       cf[0], cf[1], cf[2], cf[3], cf[4], cf[5]);
	} else {
		kk_log(m, "iomg-mmc1/iocg-mmc1 OKUNMADI: %s%s%s%s",
		       B(st0, 15) ? "" : "clk_mmc1_peribus kapali; ", B(r0, 11) ? "prst_mmc1pericfg resette; " : "",
		       B(st7, 21) ? "" : "pclk_mmc1_ioc kapali; ", B(r4, 10) ? "prst_mmc1_ioc resette; " : "");
	}

	if (ioc) {
		u32 v = kk_rd(m, B_IOMG, 0x5c);

		kk_log(m, "iomg e896c000+0x5c (GPIO3_6, pin 23) = 0x%x (fonk %u; 0 = GPIO)", v, v & 7);
	} else {
		kk_log(m, "iomg e896c000 OKUNMADI: %s%s", B(st2, 25) ? "" : "pclk_ioc kapali; ",
		       B(r2, 25) ? "prst_ioc resette; " : "");
	}

	if (gpio3) {
		u32 dir = kk_rd(m, B_GPIO3, PL061_DIR), dat = kk_rd(m, B_GPIO3, PL061_DATA_ALL);

		kk_log(m, "gpio3 GPIODIR=0x%02x GPIODATA=0x%02x; GPIO3_6: %s, seviye %u (wlan_en; 1 = cip acik)",
		       dir & 0xff, dat & 0xff, B(dir, 6) ? "cikis" : "giris", B(dat, 6));
	} else {
		kk_log(m, "gpio3 OKUNMADI: %s%s", B(st1, 3) ? "" : "pclk_gpio3 kapali; ",
		       B(r1, 3) ? "prst_gpio3 resette; " : "");
	}

	if (sdio) {
		kk_log(m, "dwmmc2 VERID=0x%08x CTRL=0x%08x PWREN=0x%08x CLKDIV=0x%08x CLKENA=0x%08x CTYPE=0x%08x",
		       kk_rd(m, B_DWMMC2, DW_VERID), kk_rd(m, B_DWMMC2, DW_CTRL), kk_rd(m, B_DWMMC2, DW_PWREN),
		       kk_rd(m, B_DWMMC2, DW_CLKDIV), kk_rd(m, B_DWMMC2, DW_CLKENA), kk_rd(m, B_DWMMC2, DW_CTYPE));
	} else {
		kk_log(m, "dwmmc2 OKUNMADI: %s%s%s%s%s",
		       mmc1_yol ? "" : "mmc1 veri yolu kapali/resette; ", B(st0, 21) ? "" : "hclk_sdio kapali; ",
		       B(st4, 19) ? "" : "clk_sdio kapali; ", B(r4, 20) ? "sdio_hrst resette; " : "",
		       (B(ce0, 21) && !B(st0, 21)) ? "(hclk_sdio CLKEN=1 ama STAT=0: cekingen) " : "");
	}
}

static ssize_t durum_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct kk_m0 *m = dev_get_drvdata(dev);
	ssize_t n;

	mutex_lock(&m->kilit);	/* yalnız önbellek; donanıma gidilmez */
	n = sysfs_emit(buf, "%s", m->onbellek);
	mutex_unlock(&m->kilit);
	return n;
}
static DEVICE_ATTR_RO(durum);

static struct attribute *kk_m0_attrs[] = { &dev_attr_durum.attr, NULL };
ATTRIBUTE_GROUPS(kk_m0);

static int kk_m0_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct kk_m0 *m;
	int i, ret;

	m = devm_kzalloc(dev, sizeof(*m), GFP_KERNEL);
	if (!m)
		return -ENOMEM;
	m->dev = dev;
	mutex_init(&m->kilit);
	platform_set_drvdata(pdev, m);

	/* Her blok ayrı reg girdisi; bölge TALEP EDİLMEZ (CRG/SCTRL/pinctrl başka sürücülerin de). */
	for (i = 0; i < B_SAYI; i++) {
		struct resource r;
		int idx = of_property_match_string(np, "reg-names", kk_blok_ad[i]);

		if (idx < 0 || of_address_to_resource(np, idx, &r))
			return dev_err_probe(dev, -EINVAL, "kk: wifi-m0: reg '%s' yok\n", kk_blok_ad[i]);
		if (resource_size(&r) < kk_blok_min[i])
			return dev_err_probe(dev, -EINVAL, "kk: wifi-m0: reg '%s' boyutu 0x%llx < 0x%x\n",
					     kk_blok_ad[i], (u64)resource_size(&r), kk_blok_min[i]);
		m->b[i] = devm_ioremap(dev, r.start, resource_size(&r));
		if (!m->b[i])
			return -ENOMEM;
	}

	kk_log(m, "basladi (yalniz okuma; yazma yolu yok)");
	kk_dokum(m);

	ret = devm_delayed_work_autocancel(dev, &m->pmic_is, kk_pmic_oku);
	if (ret)
		return ret;
	m->pmic_np = of_parse_phandle(np, "karakutu,pmic-okuma", 0);
	if (!m->pmic_np)
		kk_log(m, "pmic[0x%02x] OKUNMADI: karakutu,pmic-okuma yok", PMIC_CLK32K);
	else
		schedule_delayed_work(&m->pmic_is, 0);
	return 0;
}

static void kk_m0_remove(struct platform_device *pdev)
{
	struct kk_m0 *m = platform_get_drvdata(pdev);

	cancel_delayed_work_sync(&m->pmic_is);
	of_node_put(m->pmic_np);
}

static const struct of_device_id kk_m0_of_match[] = {
	{ .compatible = "karakutu,wifi-m0" },
	{ }
};
MODULE_DEVICE_TABLE(of, kk_m0_of_match);

static struct platform_driver kk_m0_driver = {
	.probe = kk_m0_probe,
	.remove = kk_m0_remove,
	.driver = {
		.name = "kk-wifi-m0",
		.of_match_table = kk_m0_of_match,
		.dev_groups = kk_m0_groups,
	},
};
module_platform_driver(kk_m0_driver);

MODULE_DESCRIPTION("Kara Kutu: Hi1102 SDIO M0 salt okuma durum dokumu");
MODULE_LICENSE("GPL");
