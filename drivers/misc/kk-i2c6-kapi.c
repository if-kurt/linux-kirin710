// SPDX-License-Identifier: GPL-2.0
/*
 * Kara Kutu G39/G40 (Tur 5): i2c6 kapısı.
 *
 * i2c6 (designware @ffd6a000; hi6523 şarj çipi 0x6B bu hatta) MMIO'suna dokunulmadan ÖNCE IOMCU CRG'deki
 * i2c6 saat kapısı ve reset durumu, pinmux ve pinconf (gpio212/213) okunur.
 *
 * G39: bootloader i2c6'yı KAPALI bırakıyor (CRG kapısı bit 27 = 0, pinmux 0 = GPIO).
 * G40 (Furkan onayı, G39.A4): durum uygun değilse, 4.9'un açılışta yaptığı üç adım SIRAYLA ve her yazmadan
 * sonra GERİ OKUYARAK yapılır:
 *   1. CRG: yalnız kapı+0x0 (PEREN, 0xffd7e000+0x10) <- BIT(27) (tek yazma; 1 yazılan bit açılır). PERDIS
 *      (+0x14) ve reset yazmaçlarına ASLA yazılmaz. Sonra durum (+0x18) bit 27 = 1 olmalı ve başka bit
 *      değişmemeli. Reset aktifse hiçbir şey yazılmaz.
 *   2. Pinmux +0x710/+0x714: zaten fonksiyon 1 ise yazılmaz; 0 değilse DUR; 0 ise 1 yazılır, geri okuma 1.
 *   3. Pinconf (0xfff11800) +0x710/+0x714: (değer & 0x73) == 0x10 ise (4.9 gpio212/213_cfg_func: pull yok,
 *      sürücü gücü 0x10/maske 0x70) yazılmaz; değilse (değer & ~0x73) | 0x10 yazılır ve geri okunur.
 * Bir geri okuma beklenen değilse: değerler loglanır, GERİ ALMA DENENMEZ, i2c6 aygıtı oluşturulmaz, sürücü çıkar.
 * Hepsi tutarsa i2c6 için "i2c_designware" platform aygıtı oluşturulur (DT düğümü status="disabled").
 *
 * 4.9 kaynakları: drivers/clk/hisi/hisi-clkgate.c (enable: ebits'i kapı ofsetine yaz; durum = ofset + 0x8),
 * drivers/i2c/busses/hisi_i2c_designware.c (reset durum +0x28, bit = 1 iken reset'te),
 * canlı DT pinmux@fff11000 (function-mask 0x7, gpio212/213_pmx_func <0x710 1>/<0x714 1>) ve
 * pinmux@fff11800 (gpio212/213_cfg_func: bias-pulldown <0 2 0 2>, bias-pullup <0 1 0 1>, drive-strength <0x10 0x70>).
 */
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>

#define KK_CONF_MASKE	0x73	/* pull-up b0, pull-down b1, sürücü gücü [6:4] */
#define KK_CONF_49	0x10

struct kk_kapi {
	struct platform_device *i2c_pdev;
};

struct kk_harita {
	void __iomem *crg, *pmx, *cfg;
	u32 kapi_ac, kapi_durum, kapi_bit, rst_durum, rst_bit, pin[2], fonk;
};

static int kk_harita_al(struct device *dev, struct kk_harita *h)
{
	struct device_node *np = dev->of_node;
	u32 ko[3], ro[2];

	h->fonk = 1;
	of_property_read_u32(np, "karakutu,pinmux-fonk", &h->fonk);
	/* karakutu,crg-kapi = <açma(PEREN) ofseti, durum ofseti, bit> */
	if (of_property_read_u32_array(np, "karakutu,crg-kapi", ko, 3) ||
	    of_property_read_u32_array(np, "karakutu,crg-reset", ro, 2) ||
	    of_property_read_u32_array(np, "karakutu,pinmux", h->pin, 2) ||
	    ko[0] > 0xffc || ko[1] > 0xffc || ro[0] > 0xffc || h->pin[0] > 0xffc || h->pin[1] > 0xffc ||
	    ko[2] > 31 || ro[1] > 31 || h->fonk > 7)
		return -EINVAL;
	h->kapi_ac = ko[0];
	h->kapi_durum = ko[1];
	h->kapi_bit = ko[2];
	h->rst_durum = ro[0];
	h->rst_bit = ro[1];
	/* of_iomap: bölge talep edilmez */
	h->crg = of_iomap(np, 0);
	h->pmx = of_iomap(np, 1);
	h->cfg = of_iomap(np, 2);
	if (!h->crg || !h->pmx || !h->cfg)
		return -ENOMEM;
	return 0;
}

static void kk_harita_birak(struct kk_harita *h)
{
	if (h->cfg)
		iounmap(h->cfg);
	if (h->pmx)
		iounmap(h->pmx);
	if (h->crg)
		iounmap(h->crg);
}

static void kk_durum_log(struct device *dev, struct kk_harita *h, const char *an)
{
	u32 k = readl(h->crg + h->kapi_durum), r = readl(h->crg + h->rst_durum);
	u32 p0 = readl(h->pmx + h->pin[0]), p1 = readl(h->pmx + h->pin[1]);
	u32 c0 = readl(h->cfg + h->pin[0]), c1 = readl(h->cfg + h->pin[1]);

	dev_info(dev, "kk: i2c6 kapi [%s]: CRG kapi durum(+0x%x)=0x%08x bit%u=%u, reset durum(+0x%x)=0x%08x bit%u=%u, pinmux 0x%08x/0x%08x, pinconf 0x%08x/0x%08x\n",
		 an, h->kapi_durum, k, h->kapi_bit, !!(k & BIT(h->kapi_bit)), h->rst_durum, r, h->rst_bit,
		 !!(r & BIT(h->rst_bit)), p0, p1, c0, c1);
}

/* 0 = i2c6 kullanılabilir; <0 = dur (i2c6'ya dokunulmaz) */
static int kk_kapi_hazirla(struct device *dev, struct kk_harita *h)
{
	u32 k0, k1, r, v, g;
	int i, yazma = 0;

	kk_durum_log(dev, h, "once");
	r = readl(h->crg + h->rst_durum);
	if (r & BIT(h->rst_bit)) {
		dev_warn(dev, "kk: i2c6 kapi: RESET AKTIF; reset yazmaclarina yazilmaz -> DUR (hicbir sey yazilmadi)\n");
		return -ENODEV;
	}

	/* 1. CRG kapısı */
	k0 = readl(h->crg + h->kapi_durum);
	if (!(k0 & BIT(h->kapi_bit))) {
		writel(BIT(h->kapi_bit), h->crg + h->kapi_ac);
		yazma++;
		k1 = readl(h->crg + h->kapi_durum);
		dev_info(dev, "kk: i2c6 kapi: CRG +0x%x <- 0x%08x (PEREN, tek yazma); durum +0x%x 0x%08x -> 0x%08x (beklenen 0x%08x) %s\n",
			 h->kapi_ac, (u32)BIT(h->kapi_bit), h->kapi_durum, k0, k1, k0 | (u32)BIT(h->kapi_bit),
			 k1 == (k0 | BIT(h->kapi_bit)) ? "AYNI" : "FARKLI");
		if (k1 != (k0 | BIT(h->kapi_bit))) {
			dev_warn(dev, "kk: i2c6 kapi: CRG geri okuma beklenen degil (bit%u=%u, degisen bitler 0x%08x); geri alma denenmez -> DUR\n",
				 h->kapi_bit, !!(k1 & BIT(h->kapi_bit)), (u32)((k1 ^ k0) & ~BIT(h->kapi_bit)));
			return -EIO;
		}
	}

	/* 2. pinmux */
	for (i = 0; i < 2; i++) {
		v = readl(h->pmx + h->pin[i]);
		if (v == h->fonk)
			continue;
		if (v != 0) {
			dev_warn(dev, "kk: i2c6 kapi: pinmux +0x%x = 0x%08x (0 ya da %u degil) -> DUR\n", h->pin[i], v, h->fonk);
			return -EIO;
		}
		writel(h->fonk, h->pmx + h->pin[i]);
		yazma++;
		g = readl(h->pmx + h->pin[i]);
		dev_info(dev, "kk: i2c6 kapi: pinmux +0x%x 0x%08x <- %u; geri 0x%08x %s\n", h->pin[i], v, h->fonk, g,
			 g == h->fonk ? "AYNI" : "FARKLI");
		if (g != h->fonk) {
			dev_warn(dev, "kk: i2c6 kapi: pinmux geri okuma beklenen degil; geri alma denenmez -> DUR\n");
			return -EIO;
		}
	}

	/* 3. pinconf */
	for (i = 0; i < 2; i++) {
		u32 y;

		v = readl(h->cfg + h->pin[i]);
		if ((v & KK_CONF_MASKE) == KK_CONF_49) {
			dev_info(dev, "kk: i2c6 kapi: pinconf +0x%x = 0x%08x 4.9 degeriyle ayni (maske 0x%x = 0x%x); yazilmadi\n",
				 h->pin[i], v, KK_CONF_MASKE, KK_CONF_49);
			continue;
		}
		y = (v & ~KK_CONF_MASKE) | KK_CONF_49;
		writel(y, h->cfg + h->pin[i]);
		yazma++;
		g = readl(h->cfg + h->pin[i]);
		dev_info(dev, "kk: i2c6 kapi: pinconf +0x%x 0x%08x <- 0x%08x (4.9: pull yok, surucu gucu 0x10); geri 0x%08x %s\n",
			 h->pin[i], v, y, g, g == y ? "AYNI" : "FARKLI");
		if (g != y) {
			dev_warn(dev, "kk: i2c6 kapi: pinconf geri okuma beklenen degil; geri alma denenmez -> DUR\n");
			return -EIO;
		}
	}
	kk_durum_log(dev, h, "sonra");
	dev_info(dev, "kk: i2c6 kapi: hazir (%d yazma; hepsinin geri okumasi beklenen)\n", yazma);
	return 0;
}

static int kk_kapi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct kk_harita h = {};
	struct device_node *i2c;
	struct kk_kapi *kk;
	struct resource res[2] = {};
	struct platform_device_info pi = {};
	int r;

	kk = devm_kzalloc(dev, sizeof(*kk), GFP_KERNEL);
	if (!kk)
		return -ENOMEM;
	r = kk_harita_al(dev, &h);
	if (r) {
		kk_harita_birak(&h);
		return dev_err_probe(dev, r, "kk: i2c6 kapi: DT/esleme hatasi; i2c6'ya dokunulmadi\n");
	}
	r = kk_kapi_hazirla(dev, &h);
	kk_harita_birak(&h);
	if (r) {
		dev_warn(dev, "kk: i2c6 kapi: i2c6'ya HIC dokunulmuyor, surucu cikiyor\n");
		return -ENODEV;
	}

	i2c = of_parse_phandle(dev->of_node, "karakutu,i2c", 0);
	if (!i2c)
		return dev_err_probe(dev, -EINVAL, "kk: i2c6 kapi: karakutu,i2c yok\n");
	if (of_address_to_resource(i2c, 0, &res[0]) || of_irq_to_resource(i2c, 0, &res[1]) <= 0) {
		of_node_put(i2c);
		return dev_err_probe(dev, -EINVAL, "kk: i2c6 kapi: i2c6 reg/irq okunamadi\n");
	}
	pi.parent = dev;
	pi.fwnode = of_fwnode_handle(i2c);
	pi.name = "i2c_designware";
	pi.id = PLATFORM_DEVID_NONE;
	pi.res = res;
	pi.num_res = 2;
	kk->i2c_pdev = platform_device_register_full(&pi);
	of_node_put(i2c);
	if (IS_ERR(kk->i2c_pdev))
		return dev_err_probe(dev, PTR_ERR(kk->i2c_pdev), "kk: i2c6 kapi: i2c6 aygiti olusturulamadi\n");
	platform_set_drvdata(pdev, kk);
	dev_info(dev, "kk: i2c6 kapi: i2c6 (%pR, irq %llu) designware aygiti olusturuldu\n",
		 &res[0], (unsigned long long)res[1].start);
	return 0;
}

static void kk_kapi_remove(struct platform_device *pdev)
{
	struct kk_kapi *kk = platform_get_drvdata(pdev);

	if (kk && !IS_ERR_OR_NULL(kk->i2c_pdev))
		platform_device_unregister(kk->i2c_pdev);
}

static const struct of_device_id kk_kapi_of[] = {
	{ .compatible = "karakutu,kirin710-i2c6-kapi" },
	{ }
};
MODULE_DEVICE_TABLE(of, kk_kapi_of);

static struct platform_driver kk_kapi_drv = {
	.probe = kk_kapi_probe,
	.remove = kk_kapi_remove,
	.driver = { .name = "kk-i2c6-kapi", .of_match_table = kk_kapi_of },
};
module_platform_driver(kk_kapi_drv);
MODULE_DESCRIPTION("Kara Kutu: i2c6 kapisi (4.9 ile ayni CRG/pinmux/pinconf acma, geri okumali)");
MODULE_LICENSE("GPL");
