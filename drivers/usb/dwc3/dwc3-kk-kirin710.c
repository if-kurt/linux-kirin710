// SPDX-License-Identifier: GPL-2.0
/*
 * Kara Kutu (Görev 32): HiSilicon Kirin 710 USB2 (DWC3 3.20a, yalnız HS) sargı sürücüsü.
 *
 * 4.9 vendor "hisilicon,nov-dwc3" (drivers/usb/dwc3/hisi/dwc3-nov.c + dwc3-hisi.c) yapısının aynısı:
 * önce saat/reset/PHY/sahte-VBUS dizisi (4.9 dwc3_release() + set_usb2_eye_diagram_param()), sonra
 * "snps,dwc3" çocuğu oluşturulur. Böylece dwc3 çekirdeği (GSNPSID okuması dahil) denetleyici resetten
 * çıkmadan hiçbir yazmaca dokunmaz.
 *
 * 4.9'da saat çatısıyla yapılan açmalar burada doğrudan yazmaç olarak (4.9 DT'sindeki kapı tanımları):
 *   hclk_usb2otg   -> autodiv_sysbus CRG 0x404 bit5 (hiword) + clk_usb2otg CRG PEREN0 bit6
 *   clk_usb2phy_ref-> clk_usb2otg_ref CRG PEREN0 bit25 (32 kHz)
 *   clk_usb2_tcxo_en -> clk_usb_tcxo_en PCTRL PERI_CTRL3 bit1 (hiword); ebeveyni clk_abb_192 PMIC'te
 *                       (0x37 bit0): BU SÜRÜCÜ PMIC'E YAZMAZ, yalnız okuyup log'a döker.
 * Şarj IC'sine, PMIC'e ve Type-C denetleyicisine hiçbir yazma yok. BC (şarj tipi algılama) koşulmaz;
 * yalnız BC modu açık kalmışsa 4.9'un algılama sonundaki "BC kapat" (bc_ctrl3 = 0) yazılır.
 * Her yazım önceki/yeni (ve set/clear yazmaçlarında durum yazmacı) değeriyle log'a düşer.
 */
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/spmi.h>

/* PERI CRG (fff35000) */
#define CRG_PEREN0		0x000
#define CRG_PERSTAT0		0x00c
#define CRG_PERRSTDIS0		0x064
#define CRG_PERRSTSTAT0		0x068
#define CRG_ISODIS		0x148
#define CRG_ISOSTAT		0x14c
#define CRG_AUTODIV		0x404	/* autodiv_sysbus kapısı, hiword */
#define GT_HCLK_USB2OTG		BIT(6)
#define GT_CLK_USB2OTG_REF	BIT(25)
#define AUTODIV_SYSBUS		BIT(5)
#define IP_HRST_USB2OTG		BIT(4)
#define IP_HRST_USB2OTG_AHBIF	BIT(5)
#define IP_HRST_USB2OTG_MUX	BIT(6)
#define IP_RST_USB2OTG_ADP	BIT(25)
#define IP_RST_USB2OTG_PHY	BIT(27)
#define IP_RST_USB2PHY_POR	BIT(28)
#define USB_REFCLK_ISO_EN	BIT(25)

/* PCTRL (e8a09000) */
#define PCTRL_PERI_CTRL3	0x010	/* hiword */
#define USB_TCXO_EN		BIT(1)
#define PCTRL_PERI_CTRL24	0x064
#define SC_CLK_USB3PHY_3MUX1_SEL BIT(25)

/* SCTRL (fff0a000) */
#define SCTRL_SCDEEPSLEEPED	0x008
#define ABB_NOT_SELECTED	BIT(20)	/* 0 = ABB saati seçili (4.9 is_abb_clk_selected) */

/* otg_bc / usb ahbif (ff200000), 4.9 hisi_usb2_bc.h struct usb_ahbif_registers */
#define OTG_CTRL0		0x00
#define OTG_CTRL2		0x08
#define OTG_CTRL3		0x0c	/* eye diagram */
#define OTG_BC_CTRL3		0x24
#define OTG_DUMP_END		0x58
#define CTRL0_SESSVLD		(0x3 << 10)	/* sessvld_sel | sessvld */
#define CTRL2_VBUSVLD		(BIT(3) | BIT(2))	/* vbusvldext | vbusvldsel */

#define PMIC_ABB192		0x37	/* clk_abb_192 kapısı (yalnız okunur) */

struct kk_usb {
	struct device *dev;
	void __iomem *otg;
	struct regmap *crg, *pctrl, *sctrl, *pmic;
	u32 eye;
};

static u32 rd(struct regmap *m, u32 off)
{
	u32 v = 0xdeadbeef;

	regmap_read(m, off, &v);
	return v;
}

/* set/clear (yaz-1) yazmacı: yalnız istenen bitler yazılır; durum yazmacı önce/sonra log'a. */
static void kk_setclr(struct kk_usb *k, struct regmap *m, const char *ad, u32 off, u32 bits, u32 stat)
{
	u32 once = rd(m, stat);

	regmap_write(m, off, bits);
	dev_info(k->dev, "kk: usb yaz %s [0x%03x] <- 0x%08x; durum[0x%03x] 0x%08x -> 0x%08x\n",
		 ad, off, bits, stat, once, rd(m, stat));
}

/* hiword maskeli yazmaç: (bit | bit<<16); yalnız o bit etkilenir. */
static void kk_hiword(struct kk_usb *k, struct regmap *m, const char *ad, u32 off, u32 bit)
{
	u32 once = rd(m, off);

	regmap_write(m, off, bit | (bit << 16));
	dev_info(k->dev, "kk: usb yaz %s [0x%03x] <- 0x%08x (hiword); okunan 0x%08x -> 0x%08x\n",
		 ad, off, bit | (bit << 16), once, rd(m, off));
}

/* oku-değiştir-yaz: yalnız mask bitleri. */
static void kk_rmw_map(struct kk_usb *k, struct regmap *m, const char *ad, u32 off, u32 mask, u32 val)
{
	u32 once = rd(m, off), yeni = (once & ~mask) | (val & mask);

	regmap_write(m, off, yeni);
	dev_info(k->dev, "kk: usb yaz %s [0x%03x] maske 0x%08x: 0x%08x -> 0x%08x, geri okunan 0x%08x\n",
		 ad, off, mask, once, yeni, rd(m, off));
}

static void kk_rmw_otg(struct kk_usb *k, const char *ad, u32 off, u32 mask, u32 val)
{
	u32 once = readl(k->otg + off), yeni = (once & ~mask) | (val & mask);

	writel(yeni, k->otg + off);
	dev_info(k->dev, "kk: usb yaz %s [otg+0x%02x] maske 0x%08x: 0x%08x -> 0x%08x, geri okunan 0x%08x\n",
		 ad, off, mask, once, yeni, readl(k->otg + off));
}

static bool kk_otg_okunur(struct kk_usb *k)
{
	u32 st = rd(k->crg, CRG_PERSTAT0), rs = rd(k->crg, CRG_PERRSTSTAT0);

	return (st & GT_HCLK_USB2OTG) &&
	       !(rs & (IP_HRST_USB2OTG_AHBIF | IP_HRST_USB2OTG_MUX | IP_RST_USB2OTG_ADP));
}

/* YALNIZ OKUMA. otg_bc yalnız hclk açık ve AHBIF/MUX/ADP resette değilse okunur (kilitlenme riski). */
static void kk_dump(struct kk_usb *k, const char *ne)
{
	u32 off;

	dev_info(k->dev, "kk: usb dokum[%s] sctrl SCDEEPSLEEPED=0x%08x (bit20=%u: %s)\n", ne,
		 rd(k->sctrl, SCTRL_SCDEEPSLEEPED),
		 !!(rd(k->sctrl, SCTRL_SCDEEPSLEEPED) & ABB_NOT_SELECTED),
		 (rd(k->sctrl, SCTRL_SCDEEPSLEEPED) & ABB_NOT_SELECTED) ? "ABB degil" : "ABB secili");
	dev_info(k->dev, "kk: usb dokum[%s] crg PERCLKEN0=0x%08x PERSTAT0=0x%08x PERRSTSTAT0=0x%08x ISOSTAT=0x%08x AUTODIV=0x%08x\n",
		 ne, rd(k->crg, 0x008), rd(k->crg, CRG_PERSTAT0), rd(k->crg, CRG_PERRSTSTAT0),
		 rd(k->crg, CRG_ISOSTAT), rd(k->crg, CRG_AUTODIV));
	dev_info(k->dev, "kk: usb dokum[%s] pctrl PERI_CTRL3=0x%08x PERI_CTRL24=0x%08x\n", ne,
		 rd(k->pctrl, PCTRL_PERI_CTRL3), rd(k->pctrl, PCTRL_PERI_CTRL24));
	if (k->pmic) {
		u32 v = 0;
		int r = regmap_read(k->pmic, PMIC_ABB192, &v);

		dev_info(k->dev, "kk: usb dokum[%s] pmic[0x%02x] (clk_abb_192, yalniz okuma) = 0x%02x (r=%d)\n",
			 ne, PMIC_ABB192, v, r);
	} else {
		dev_info(k->dev, "kk: usb dokum[%s] pmic okunamadi (regmap yok)\n", ne);
	}
	if (!kk_otg_okunur(k)) {
		dev_info(k->dev, "kk: usb dokum[%s] otg_bc okunmadi (hclk kapali ya da AHBIF/MUX/ADP resette)\n", ne);
		return;
	}
	for (off = 0; off < OTG_DUMP_END; off += 0x10)
		dev_info(k->dev, "kk: usb dokum[%s] otg+0x%02x: %08x %08x %08x %08x\n", ne, off,
			 readl(k->otg + off), readl(k->otg + off + 4),
			 readl(k->otg + off + 8), readl(k->otg + off + 12));
}

/* 4.9 dwc3_release() + set_usb2_eye_diagram_param() (cihaz modu), aynı sıra ve beklemeler. */
static int kk_usb_release(struct kk_usb *k)
{
	u32 bc3;

	if (rd(k->sctrl, SCTRL_SCDEEPSLEEPED) & ABB_NOT_SELECTED) {
		dev_err(k->dev, "kk: usb: ABB saati secili degil; 4.9'un yedek PLL yolu tasinmadi, USB acilmiyor\n");
		return -ENODEV;
	}

	/* clk_prepare_enable(hclk_usb2otg): ebeveyn autodiv_sysbus, sonra clk_usb2otg */
	kk_hiword(k, k->crg, "autodiv_sysbus", CRG_AUTODIV, AUTODIV_SYSBUS);
	kk_setclr(k, k->crg, "PEREN0 hclk_usb2otg", CRG_PEREN0, GT_HCLK_USB2OTG, CRG_PERSTAT0);
	/* clk_prepare_enable(clk_usb2phy_ref) = clk_usb2otg_ref */
	kk_setclr(k, k->crg, "PEREN0 clk_usb2otg_ref", CRG_PEREN0, GT_CLK_USB2OTG_REF, CRG_PERSTAT0);
	/* unreset controller */
	kk_setclr(k, k->crg, "PERRSTDIS0 adp|mux|ahbif", CRG_PERRSTDIS0,
		  IP_RST_USB2OTG_ADP | IP_HRST_USB2OTG_MUX | IP_HRST_USB2OTG_AHBIF, CRG_PERRSTSTAT0);
	/* ABB yolu: open ABBISO, tcxo (PMIC ebeveyni yazılmaz), 1 ms, 3MUX1_SEL temizle */
	kk_setclr(k, k->crg, "ISODIS usb_refclk", CRG_ISODIS, USB_REFCLK_ISO_EN, CRG_ISOSTAT);
	kk_hiword(k, k->pctrl, "PERI_CTRL3 usb_tcxo_en", PCTRL_PERI_CTRL3, USB_TCXO_EN);
	msleep(1);
	kk_rmw_map(k, k->pctrl, "PERI_CTRL24 3mux1_sel=0", PCTRL_PERI_CTRL24, SC_CLK_USB3PHY_3MUX1_SEL, 0);
	/* unreset phy, phy clk domain, hclk domain; her biri 100 us */
	kk_setclr(k, k->crg, "PERRSTDIS0 usb2phy_por", CRG_PERRSTDIS0, IP_RST_USB2PHY_POR, CRG_PERRSTSTAT0);
	udelay(100);
	kk_setclr(k, k->crg, "PERRSTDIS0 usb2otg_phy", CRG_PERRSTDIS0, IP_RST_USB2OTG_PHY, CRG_PERRSTSTAT0);
	udelay(100);
	kk_setclr(k, k->crg, "PERRSTDIS0 hrst_usb2otg", CRG_PERRSTDIS0, IP_HRST_USB2OTG, CRG_PERRSTSTAT0);
	udelay(100);
	if (!kk_otg_okunur(k)) {
		dev_err(k->dev, "kk: usb: release sonrasi otg_bc hala erisilemez; durduruldu\n");
		return -EIO;
	}
	/* fake vbus valid signal */
	kk_rmw_otg(k, "CTRL2 vbusvldext|sel", OTG_CTRL2, CTRL2_VBUSVLD, CTRL2_VBUSVLD);
	kk_rmw_otg(k, "CTRL0 sessvld_sel|sessvld", OTG_CTRL0, CTRL0_SESSVLD, CTRL0_SESSVLD);
	msleep(1);
	/* set_usb2_eye_diagram_param (cihaz): 4.9 yazmacın tamamını yazar (alanların hepsi PHY ayarı) */
	kk_rmw_otg(k, "CTRL3 eye_diagram", OTG_CTRL3, 0xffffffff, k->eye);
	/* BC: 4.9 algılamayı her başlatmada koşup BC'yi kapatır. Burada algılama yok; açık kalmışsa kapat. */
	bc3 = readl(k->otg + OTG_BC_CTRL3);
	if (bc3)
		kk_rmw_otg(k, "BC_CTRL3 bc kapat", OTG_BC_CTRL3, 0xffffffff, 0);
	else
		dev_info(k->dev, "kk: usb: BC_CTRL3=0 (BC modu kapali), yazilmadi\n");
	return 0;
}

static int kk_usb_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node, *pn;
	struct kk_usb *k;
	int ret;

	k = devm_kzalloc(dev, sizeof(*k), GFP_KERNEL);
	if (!k)
		return -ENOMEM;
	k->dev = dev;
	k->otg = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(k->otg))
		return PTR_ERR(k->otg);
	k->crg = syscon_regmap_lookup_by_phandle(np, "karakutu,crg");
	k->pctrl = syscon_regmap_lookup_by_phandle(np, "karakutu,pctrl");
	k->sctrl = syscon_regmap_lookup_by_phandle(np, "karakutu,sctrl");
	if (IS_ERR(k->crg) || IS_ERR(k->pctrl) || IS_ERR(k->sctrl))
		return dev_err_probe(dev, -ENODEV, "kk: usb: crg/pctrl/sctrl syscon yok\n");
	if (of_property_read_u32(np, "karakutu,eye-diagram", &k->eye))
		return dev_err_probe(dev, -EINVAL, "kk: usb: karakutu,eye-diagram yok\n");

	/* PMIC: yalnız okuma. kk-spmi-syscon'un SPMI aygıtından (henüz yoksa okunmaz; erteleme yok). */
	pn = of_parse_phandle(np, "karakutu,pmic-okuma", 0);
	if (pn) {
		struct spmi_device *sd = spmi_find_device_by_of_node(pn);

		if (sd) {
			k->pmic = dev_get_regmap(&sd->dev, NULL);
			put_device(&sd->dev);
		}
		of_node_put(pn);
	}

	kk_dump(k, "probe, degisiklik oncesi");
	ret = kk_usb_release(k);
	kk_dump(k, "release sonrasi");
	if (ret)
		return ret;

	ret = of_platform_populate(np, NULL, NULL, dev);
	if (ret)
		return dev_err_probe(dev, ret, "kk: usb: dwc3 cocugu olusturulamadi\n");
	dev_info(dev, "kk: usb: kirin710 USB2 (DWC3) hazir; cocuk dwc3 olusturuldu, yalniz cihaz modu\n");
	return 0;
}

static void kk_usb_remove(struct platform_device *pdev)
{
	of_platform_depopulate(&pdev->dev);
}

static const struct of_device_id kk_usb_of_match[] = {
	{ .compatible = "karakutu,kirin710-dwc3" },
	{ }
};
MODULE_DEVICE_TABLE(of, kk_usb_of_match);

static struct platform_driver kk_usb_driver = {
	.probe = kk_usb_probe,
	.remove = kk_usb_remove,
	.driver = {
		.name = "kk-kirin710-dwc3",
		.of_match_table = kk_usb_of_match,
	},
};
module_platform_driver(kk_usb_driver);

MODULE_DESCRIPTION("Kara Kutu: Kirin 710 DWC3 USB2 sargi surucusu (4.9 nov sirasinin tasimasi)");
MODULE_LICENSE("GPL");
