// SPDX-License-Identifier: GPL-2.0
/*
 * Kara Kutu (Görev 23) — SPMI üzerindeki PMIC'i (Kirin 710: hi6555v200, sid 0) yalnızca
 * bir regmap olarak sunar ve bunu syscon olarak kaydeder. Amaç: standart
 * "syscon-reboot-mode" sürücüsünün, 4.9 vendor kernel'in yaptığı gibi reboot
 * nedenini PMIC HRST_REG13'e (0x1E1) yazabilmesi ("reboot bootloader" -> 0x01).
 *
 * Probe sırasında SPMI veri yolunda HİÇBİR işlem yapılmaz (yalnız regmap kurulur);
 * ilk ve tek erişim, reboot-mode bildiriminde regmap_update_bits() ile olur.
 * Regülatör, IRQ ya da başka bir PMIC işlevi YOK.
 *
 * G48 (W11, yalnız DARALTIR): regmap'e .writeable_reg eklendi. Bu regmap üzerinden yazılabilen PMIC yazmaçları
 * YALNIZ 0x1E1 (HRST_REG13, reboot-mode; "reboot bootloader" dönüş yolu) ve 0x3E (W1 en_32k_bt, kk-sdio-kapi).
 * Başka her adrese regmap_write/update_bits -EIO döner (regmap.c _regmap_write -> regmap_writeable). Okumalar
 * değişmez. Tutarlılık: probe DT'deki reboot-mode "offset" değerinin listede olduğunu denetler; değilse (dönüş yolu
 * bozulmasın diye) kısıtsız regmap kurulur ve bu açıkça loglanır (= G47 davranışı).
 */
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/regmap.h>
#include <linux/spmi.h>

#define KK_PMIC_HRST_REG13	0x1e1	/* reboot-mode (DT offset) */
#define KK_PMIC_32K_BT		0x3e	/* W1 (kk-sdio-kapi) */

static bool kk_spmi_yazilabilir(struct device *dev, unsigned int reg)
{
	return reg == KK_PMIC_HRST_REG13 || reg == KK_PMIC_32K_BT;
}

static const struct regmap_config kk_spmi_syscon_regmap = {
	.reg_bits	= 16,
	.val_bits	= 8,
	.max_register	= 0xffff,
	.writeable_reg	= kk_spmi_yazilabilir,	/* G48 W11 */
};

/* G48 W11: yalnız DT tutarsızsa (reboot-mode offset listede değil) kullanılan, G47 ile aynı kısıtsız yapılandırma */
static const struct regmap_config kk_spmi_syscon_regmap_kisitsiz = {
	.reg_bits	= 16,
	.val_bits	= 8,
	.max_register	= 0xffff,
};

static int kk_spmi_syscon_probe(struct spmi_device *sdev)
{
	struct device *dev = &sdev->dev;
	struct device_node *rm;
	struct regmap *map;
	u32 off = 0;
	bool tutarli;
	int ret;

	rm = of_get_child_by_name(dev->of_node, "reboot-mode");
	tutarli = rm && !of_property_read_u32(rm, "offset", &off) && kk_spmi_yazilabilir(dev, off);
	of_node_put(rm);
	map = devm_regmap_init_spmi_ext(sdev, tutarli ? &kk_spmi_syscon_regmap : &kk_spmi_syscon_regmap_kisitsiz);
	if (IS_ERR(map))
		return dev_err_probe(dev, PTR_ERR(map), "regmap\n");

	ret = of_syscon_register_regmap(dev->of_node, map);
	if (ret)
		return dev_err_probe(dev, ret, "syscon kaydi\n");

	dev_info(dev, "kk: PMIC regmap (sid %d) syscon olarak kaydedildi\n", sdev->usid);
	if (tutarli)
		dev_info(dev, "kk: PMIC regmap W11: yazilabilir YALNIZ 0x%03x (reboot-mode, DT offset 0x%x TUTTU) ve 0x%02x (W1); okumalar degismedi\n",
			 KK_PMIC_HRST_REG13, off, KK_PMIC_32K_BT);
	else
		dev_warn(dev, "kk: PMIC regmap W11 TUTARSIZ: DT reboot-mode offset 0x%x listede degil ya da yok -> KISITSIZ (G47 davranisi; donus yolu korunur)\n",
			 off);
	return devm_of_platform_populate(dev);
}

static const struct of_device_id kk_spmi_syscon_of_match[] = {
	{ .compatible = "karakutu,spmi-syscon" },
	{ }
};
MODULE_DEVICE_TABLE(of, kk_spmi_syscon_of_match);

static struct spmi_driver kk_spmi_syscon_driver = {
	.probe = kk_spmi_syscon_probe,
	.driver = {
		.name = "kk-spmi-syscon",
		.of_match_table = kk_spmi_syscon_of_match,
	},
};
module_spmi_driver(kk_spmi_syscon_driver);

MODULE_DESCRIPTION("Kara Kutu: SPMI PMIC regmap'ini syscon olarak sun");
MODULE_LICENSE("GPL");
