// SPDX-License-Identifier: GPL-2.0
/*
 * Kara Kutu G47 S1: YALNIZ QEMU. Sahte i2c3 hattı + sahte LM36274 (0x11). Cihaz imajında ASLA derlenmez
 * (build.sh test/cihaz varyantlarında CONFIG_KK_S1_SAHTE=y ise derleme durur).
 *  - SMBus bayt okuma/yazma; yazmaç dosyası bootloader değerleriyle başlar (TASARIM-ekran-kapatma §1.2).
 *  - Her işlem loglanır ("kk: s1-sahte: ..."); 0x11 dışı adres -> -ENXIO ve "IZINSIZ ADRES" sayacı.
 *  - Senaryo (kk_s1_sahte.senaryo=N): 0 olağan; 1 0x01=0x06 (kimlik/0x01 tutmaz); 2 0x08=0x10 (kimlik tutmaz);
 *    3 0x05 yazması yutulur (geri okuma FARKLI); 4 0x0F=0x01 (arıza bayrağı); 5 0x0F okuma hatası;
 *    6 0x01 okuma hatası (NACK); 7 0x04 yazması I2C hatası.
 *  - sysfs "sahte": yazmaç dosyası + yazmaç başına okuma/yazma sayıları.
 */
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include "kk-s1-karar.h"

static int senaryo;
module_param(senaryo, int, 0444);

struct kk_sahte {
	struct i2c_adapter ad;
	u8 r[0x20];
	unsigned int okuma[0x20], yazma[0x20], izinsiz;
};

static const u8 kk_bl[0x12] = {
	[0x01] = 0x05, [0x02] = 0x68, [0x03] = 0x85, [0x04] = 0x04, [0x05] = 0x9c, [0x08] = 0x17, [0x09] = 0x99,
	[0x0a] = 0x11, [0x0c] = 0x24, [0x0d] = 0x20, [0x0e] = 0x20, [0x10] = 0x46, [0x11] = 0x35,
};

static int kk_sahte_xfer(struct i2c_adapter *ad, u16 addr, unsigned short flags, char rw, u8 cmd, int size,
			 union i2c_smbus_data *data)
{
	struct kk_sahte *s = i2c_get_adapdata(ad);

	if (addr != KK_LM_ADRES) {
		s->izinsiz++;
		dev_warn(&ad->dev, "kk: s1-sahte: IZINSIZ ADRES 0x%02x (toplam %u)\n", addr, s->izinsiz);
		return -ENXIO;
	}
	if (size != I2C_SMBUS_BYTE_DATA || cmd >= ARRAY_SIZE(s->r))
		return -EOPNOTSUPP;
	if (rw == I2C_SMBUS_READ) {
		s->okuma[cmd]++;
		if ((senaryo == 5 && cmd == KK_LM_FLAGS) || (senaryo == 6 && cmd == KK_LM_REV)) {
			dev_info(&ad->dev, "kk: s1-sahte: oku 0x%02x -> NACK (senaryo %d)\n", cmd, senaryo);
			return -ENXIO;
		}
		data->byte = s->r[cmd];
		dev_info(&ad->dev, "kk: s1-sahte: oku 0x%02x = 0x%02x\n", cmd, data->byte);
		return 0;
	}
	s->yazma[cmd]++;
	if (senaryo == 7 && cmd == KK_LM_LSB) {
		dev_info(&ad->dev, "kk: s1-sahte: yaz 0x%02x <- 0x%02x -> NACK (senaryo 7)\n", cmd, data->byte);
		return -ENXIO;
	}
	if (senaryo == 3 && cmd == KK_LM_MSB) {
		dev_info(&ad->dev, "kk: s1-sahte: yaz 0x%02x <- 0x%02x YUTULDU (senaryo 3)\n", cmd, data->byte);
		return 0;
	}
	s->r[cmd] = data->byte;
	dev_info(&ad->dev, "kk: s1-sahte: yaz 0x%02x <- 0x%02x\n", cmd, data->byte);
	return 0;
}

static u32 kk_sahte_func(struct i2c_adapter *ad)
{
	return I2C_FUNC_SMBUS_BYTE_DATA;
}

static const struct i2c_algorithm kk_sahte_algo = {
	.smbus_xfer = kk_sahte_xfer,
	.functionality = kk_sahte_func,
};

static ssize_t sahte_show(struct device *dev, struct device_attribute *a, char *buf)
{
	struct kk_sahte *s = dev_get_drvdata(dev);
	int i, n = 0;

	n += sysfs_emit_at(buf, n, "senaryo=%d izinsiz=%u\n", senaryo, s->izinsiz);
	for (i = 0; i <= KK_LM_SON; i++)
		n += sysfs_emit_at(buf, n, "r%02x=%02x ok=%u yz=%u\n", i, s->r[i], s->okuma[i], s->yazma[i]);
	return n;
}
static DEVICE_ATTR_RO(sahte);
static struct attribute *kk_sahte_attrs[] = { &dev_attr_sahte.attr, NULL };
ATTRIBUTE_GROUPS(kk_sahte);

static int kk_sahte_probe(struct platform_device *pdev)
{
	struct kk_sahte *s;

	s = devm_kzalloc(&pdev->dev, sizeof(*s), GFP_KERNEL);
	if (!s)
		return -ENOMEM;
	memcpy(s->r, kk_bl, sizeof(kk_bl));
	if (senaryo == 1)
		s->r[KK_LM_REV] = 0x06;
	if (senaryo == 2)
		s->r[KK_LM_EN] = 0x10;
	if (senaryo == 4)
		s->r[KK_LM_FLAGS] = 0x01;
	platform_set_drvdata(pdev, s);
	s->ad.owner = THIS_MODULE;
	s->ad.algo = &kk_sahte_algo;
	s->ad.dev.parent = &pdev->dev;
	s->ad.dev.of_node = pdev->dev.of_node;
	s->ad.nr = -1;
	strscpy(s->ad.name, "kk-s1-sahte-i2c3", sizeof(s->ad.name));
	i2c_set_adapdata(&s->ad, s);
	dev_warn(&pdev->dev, "kk: s1-sahte: SAHTE i2c3 + LM36274 (YALNIZ QEMU), senaryo %d\n", senaryo);
	return devm_i2c_add_adapter(&pdev->dev, &s->ad);
}

static const struct of_device_id kk_sahte_of[] = {
	{ .compatible = "karakutu,s1-sahte-i2c3" },
	{ }
};

static struct platform_driver kk_sahte_drv = {
	.probe = kk_sahte_probe,
	.driver = { .name = "kk-s1-sahte", .of_match_table = kk_sahte_of, .dev_groups = kk_sahte_groups },
};
module_platform_driver(kk_sahte_drv);
MODULE_DESCRIPTION("Kara Kutu: S1 sahte i2c3/LM36274 (yalniz QEMU)");
MODULE_LICENSE("GPL");
