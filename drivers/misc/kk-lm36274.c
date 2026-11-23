// SPDX-License-Identifier: GPL-2.0
/*
 * Kara Kutu G48 P1: LM36274 arka ışık + bias IC'si (i2c3, 0x11) — ÜRETİM kipi (G47 S1 sürücüsünden).
 *
 * Ana hattın ti-lmu / leds-lm36274 / lm363x-regulator yığını KULLANILMAZ. Compatible "karakutu,lm36274-kk".
 *  - Probe (yazma YOK): önce yalnız 0x01; hata ya da 0x05 değilse DUR (başka okuma yok). Sonra 0x02-0x0E, 0x10, 0x11.
 *  - 0x0F (arıza bayrakları; okununca silinen bayrak yazmacı) HİÇBİR YOLDA okunmaz (kk_lm_i2c_oku da reddeder).
 *  - Kimlik: 0x01=0x05, 0x08=0x17, 0x04=0x04, 0x05=0x9C, 0x09=0x99. Tutmazsa yazma KİLİTLİ.
 *  - sysfs kk_arka_isik: YALNIZ "kapat" = 0x04 <- 0, 0x05 <- 0 (LSB -> MSB), her yazmadan sonra geri okuma; hata ->
 *    KİLİT, başka yazma yok. Açılış değerine geri yazma ("geri") ÜRETİMDE YOK: komut tanınmaz (-EINVAL), kodu yok.
 *    Okuma = durum + açılış önbelleği + canlı 0x04/0x05/0x08.
 *  - Bütün I2C yazmaları tek işlevde (kk_lm_i2c_yaz): adres yalnız 0x11, beyaz liste {0x04 <- 0, 0x05 <- 0}.
 * Tetikleyen: kk-idle-bekci A0 (kullanıcı alanı). Geri dönüş: reboot (bootloader IC'yi her açılışta kurar, 1252/2047).
 */
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/types.h>
#include "kk-p1-lm-karar.h"

struct kk_lm {
	struct i2c_client *c;
	struct mutex kilit;
	struct kk_lm_durum d;
};

/* TEK I2C okuma yeri: 0x0F burada da reddedilir (iki kat) */
static int kk_lm_i2c_oku(void *ctx, int reg)
{
	struct kk_lm *lm = ctx;

	if (WARN_ON_ONCE(lm->c->addr != KK_LM_ADRES || !kk_lm_okunur(reg)))
		return -EPERM;
	return i2c_smbus_read_byte_data(lm->c, reg);
}

/* TEK I2C yazma yeri: adres ve beyaz liste burada da denetlenir (iki kat) */
static noinline int kk_lm_i2c_yaz(void *ctx, int reg, int val)
{
	struct kk_lm *lm = ctx;

	if (WARN_ON(lm->c->addr != KK_LM_ADRES || !kk_lm_yazma_izinli(reg, val)))
		return -EPERM;
	return i2c_smbus_write_byte_data(lm->c, reg, 0);
}

static void kk_lm_log(void *ctx, const char *satir)
{
	struct kk_lm *lm = ctx;

	dev_info(&lm->c->dev, "kk: arka-isik: %s\n", satir);
}

static ssize_t kk_arka_isik_show(struct device *dev, struct device_attribute *a, char *buf)
{
	struct kk_lm *lm = dev_get_drvdata(dev);
	struct kk_lm_durum *d = &lm->d;
	int l = -1, m = -1, e = -1;
	ssize_t n;

	mutex_lock(&lm->kilit);
	if (d->bus_ok) {	/* canlı okuma yalnız hat ve IC doğrulanmışsa; yalnız 0x04/0x05/0x08 */
		l = kk_lm_i2c_oku(lm, KK_LM_LSB);
		m = kk_lm_i2c_oku(lm, KK_LM_MSB);
		e = kk_lm_i2c_oku(lm, KK_LM_EN);
	}
	n = sysfs_emit(buf, "durum=%s kimlik=%d kilit=%d yazma=%u hata=%u acilis 04=%02x 05=%02x 08=%02x 09=%02x 01=%02x canli 04=%d 05=%d 08=%d kip=uretim\n",
		       d->kapali ? "kapali" : "acik", d->kimlik, d->kilit, d->yazma, d->hata, d->reg[KK_LM_LSB] & 0xff,
		       d->reg[KK_LM_MSB] & 0xff, d->reg[KK_LM_EN] & 0xff, d->reg[KK_LM_BIAS1] & 0xff, d->reg[KK_LM_REV] & 0xff,
		       l, m, e);
	mutex_unlock(&lm->kilit);
	return n;
}

static ssize_t kk_arka_isik_store(struct device *dev, struct device_attribute *a, const char *buf, size_t len)
{
	struct kk_lm *lm = dev_get_drvdata(dev);
	struct kk_lm_io io = { .ctx = lm, .rd = kk_lm_i2c_oku, .wr = kk_lm_i2c_yaz, .log = kk_lm_log };
	int r;

	if (!sysfs_streq(buf, "kapat"))
		return -EINVAL;	/* üretimde tek komut; "geri" yok */
	mutex_lock(&lm->kilit);
	r = kk_lm_kapat(&lm->d, &io);
	mutex_unlock(&lm->kilit);
	return r ? -EIO : len;
}
static DEVICE_ATTR_RW(kk_arka_isik);

static struct attribute *kk_lm_attrs[] = { &dev_attr_kk_arka_isik.attr, NULL };
ATTRIBUTE_GROUPS(kk_lm);

static int kk_lm_probe(struct i2c_client *c)
{
	struct kk_lm *lm;
	struct kk_lm_io io;

	if (c->addr != KK_LM_ADRES)
		return dev_err_probe(&c->dev, -ENODEV, "kk: arka-isik: adres 0x%02x (yalniz 0x11); dokunulmadi\n", c->addr);
	lm = devm_kzalloc(&c->dev, sizeof(*lm), GFP_KERNEL);
	if (!lm)
		return -ENOMEM;
	lm->c = c;
	mutex_init(&lm->kilit);
	i2c_set_clientdata(c, lm);
	io = (struct kk_lm_io){ .ctx = lm, .rd = kk_lm_i2c_oku, .wr = kk_lm_i2c_yaz, .log = kk_lm_log };
	dev_info(&c->dev, "kk: arka-isik: basladi (i2c%d 0x%02x; uretim kipi: probe'da yazma YOK, 0x0F okunmaz; beyaz liste 0x04/0x05 <- 0; geri yazma yok)\n",
		 c->adapter->nr, c->addr);
	mutex_lock(&lm->kilit);
	kk_lm_probe_oku(&lm->d, &io);
	mutex_unlock(&lm->kilit);
	return 0;	/* kimlik tutmasa da bağlı kalır (sysfs: durum + kilit); yazma kilitli */
}

static const struct of_device_id kk_lm_of[] = {
	{ .compatible = "karakutu,lm36274-kk" },
	{ }
};
MODULE_DEVICE_TABLE(of, kk_lm_of);

static struct i2c_driver kk_lm_drv = {
	.driver = { .name = "kk-lm36274", .of_match_table = kk_lm_of, .dev_groups = kk_lm_groups },
	.probe = kk_lm_probe,
};
module_i2c_driver(kk_lm_drv);
MODULE_DESCRIPTION("Kara Kutu: LM36274 okuyucu + parlaklik 0 tetigi (uretim; beyaz liste 0x04/0x05 <- 0)");
MODULE_LICENSE("GPL");
