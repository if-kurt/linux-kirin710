// SPDX-License-Identifier: GPL-2.0
/*
 * Kara Kutu G47 S1 (E0/E1): i2c3 kapısı (G40 kk-i2c6-kapi deseni). YALNIZ test imajı.
 *
 * i2c3 (designware @fdf0c000; LM36274 arka ışık/bias IC'si 0x11 bu hatta) MMIO'suna dokunulmadan ÖNCE PERI CRG'deki
 * i2c3 saat kapısı (PERSTAT2 b7) ve reset durumu (PERRSTSTAT2 b7), pinmux (iomg 0x700/0x704) ve pinconf
 * (iocg 0x700/0x704) okunur (G1-G4).
 *  - Hepsi beklenen değerdeyse (saat açık, reset bırakılmış, pinmux 1, pinconf 4.9 değeri): G5 (IC_ENABLE_STATUS)
 *    okunur ve i2c3 "i2c_designware" aygıtı açılışta oluşturulur (YAZMA YOK).
 *  - Pinmux 0/1 değilse ya da pinconf 4.9 değeri değilse: DUR (hiçbir şey yazılmaz; pinconf yazması onaylı değil).
 *  - Yalnız saat/reset/pinmux kapalıysa: açılışta YAZMA YAPILMAZ (G47 kuralı "açılışta yeni yazma yok"); plan
 *    loglanır ve sysfs "kapi" dosyasına "ac" yazılınca (oturumda, bir kez) G40 deseniyle SIRAYLA açılır:
 *    PEREN2 <- BIT(7), PERRSTDIS2 <- BIT(7), iomg <- 1. Her yazmadan sonra geri okuma; tutmazsa DUR (geri alma
 *    denenmez; reboot sıfırlar). Sonra G5 okunur ve i2c3 aygıtı oluşturulur.
 * Bütün MMIO yazmaları tek işlevde (kk_i3_mmio_yaz) ve derlenmiş 4 girdilik beyaz listeyle (kk-s1-karar.h) sınırlı.
 * Fiziksel tabanlar derlenmiş sabitlerle karşılaştırılır; DT farklıysa hiçbir şey okunmaz.
 */
#include <linux/device.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include "kk-s1-karar.h"

enum { KK_KAPI_DUR, KK_KAPI_BEKLIYOR, KK_KAPI_HAZIR };
static const char * const kk_kapi_durum_ad[] = { "DUR", "BEKLIYOR (sysfs 'ac')", "HAZIR (i2c3 olusturuldu)" };

struct kk_i3 {
	struct device *dev;
	void __iomem *b[3];		/* crg, pmx, cfg */
	struct resource i2c_res[2];
	struct device_node *i2c_np;
	struct platform_device *i2c_pdev;
	struct mutex kilit;
	int durum, plan;
	unsigned int yazma;
	u32 g5;
	int g5_okundu;
};

static u32 kk_i3_rd(void *ctx, int blok, u32 off)
{
	struct kk_i3 *k = ctx;

	if (WARN_ON_ONCE(blok < 0 || blok > 2 || off + 4 > (blok == KK_I3_B_CRG ? KK_I3_CRG_BOY : KK_I3_PIN_BOY)))
		return 0xdeadbeef;
	return readl(k->b[blok] + off);
}

/* TEK MMIO yazma yeri: beyaz liste burada da denetlenir (iki kat) */
static noinline void kk_i3_mmio_yaz(void *ctx, int blok, u32 off, u32 val)
{
	struct kk_i3 *k = ctx;

	if (WARN_ON(!kk_i3_yazma_izinli(blok, off, val)))
		return;
	writel(val, k->b[blok] + off);
}

static void kk_i3_log(void *ctx, const char *satir)
{
	struct kk_i3 *k = ctx;

	dev_info(k->dev, "kk: i2c3 kapi: %s\n", satir);
}

static void kk_i3_oku(struct kk_i3 *k, struct kk_i3_oku *o)
{
	o->clken2 = kk_i3_rd(k, KK_I3_B_CRG, KK_I3_PERCLKEN2);
	o->stat2 = kk_i3_rd(k, KK_I3_B_CRG, KK_I3_PERSTAT2);
	o->rst2 = kk_i3_rd(k, KK_I3_B_CRG, KK_I3_PERRSTSTAT2);
	o->pmx[0] = kk_i3_rd(k, KK_I3_B_PMX, 0);
	o->pmx[1] = kk_i3_rd(k, KK_I3_B_PMX, 4);
	o->cfg[0] = kk_i3_rd(k, KK_I3_B_CFG, 0);
	o->cfg[1] = kk_i3_rd(k, KK_I3_B_CFG, 4);
}

static void kk_i3_durum_log(struct kk_i3 *k, const struct kk_i3_oku *o, const char *an)
{
	dev_info(k->dev, "kk: i2c3 kapi [%s]: G1 PERCLKEN2=0x%08x PERSTAT2=0x%08x b7=%u; G2 PERRSTSTAT2=0x%08x b7=%u (1 = resette); G3 iomg 0x%08x/0x%08x; G4 iocg 0x%08x/0x%08x (maske 0x73 -> 0x%02x/0x%02x, 4.9 0x10)\n",
		 an, o->clken2, o->stat2, !!(o->stat2 & KK_I3_BIT), o->rst2, !!(o->rst2 & KK_I3_BIT), o->pmx[0], o->pmx[1],
		 o->cfg[0], o->cfg[1], o->cfg[0] & KK_I3_CFG_MASKE, o->cfg[1] & KK_I3_CFG_MASKE);
}

/* G5 + i2c3 aygıtı. Yalnız son okuma planı 0 (G1=1, G2=0, G3=1/1, G4 tamam) iken çağrılır. */
static int kk_i3_olustur(struct kk_i3 *k)
{
	struct platform_device_info pi = {};
	struct kk_i3_oku o;
	void __iomem *s;

	kk_i3_oku(k, &o);
	if (kk_i3_plan(&o) != 0 || !kk_i3_g5_okunur(o.stat2, o.rst2)) {
		kk_i3_durum_log(k, &o, "son");
		dev_warn(k->dev, "kk: i2c3 kapi: DUR: son durum beklenen degil; i2c3'e dokunulmadi\n");
		return -EIO;
	}
	s = ioremap(k->i2c_res[0].start + KK_I3_IC_ENABLE_STATUS, 4);
	if (!s)
		return -ENOMEM;
	k->g5 = readl(s);
	k->g5_okundu = 1;
	iounmap(s);
	dev_info(k->dev, "kk: i2c3 kapi: G5 IC_ENABLE_STATUS(+0x9c)=0x%08x (b0 ic_en=%u)\n", k->g5, k->g5 & 1);

	pi.parent = k->dev;
	pi.fwnode = of_fwnode_handle(k->i2c_np);
	pi.name = "i2c_designware";
	pi.id = 3;	/* "i2c_designware.3": i2c6 kapısının "i2c_designware" adıyla çakışmasın */
	pi.res = k->i2c_res;
	pi.num_res = 2;
	k->i2c_pdev = platform_device_register_full(&pi);
	if (IS_ERR(k->i2c_pdev)) {
		int r = PTR_ERR(k->i2c_pdev);

		k->i2c_pdev = NULL;
		dev_warn(k->dev, "kk: i2c3 kapi: i2c3 aygiti olusturulamadi (%d)\n", r);
		return r;
	}
	dev_info(k->dev, "kk: i2c3 kapi: i2c3 (%pR, irq %llu) designware aygiti olusturuldu (%u yazma)\n",
		 &k->i2c_res[0], (unsigned long long)k->i2c_res[1].start, k->yazma);
	return 0;
}

static ssize_t kapi_show(struct device *dev, struct device_attribute *a, char *buf)
{
	struct kk_i3 *k = dev_get_drvdata(dev);
	struct kk_i3_oku o;
	ssize_t n;

	mutex_lock(&k->kilit);
	kk_i3_oku(k, &o);	/* G1-G4 canlı (yan etkisiz durum yazmaçları) */
	n = sysfs_emit(buf, "durum=%s plan=%d yazma=%u perstat2=0x%08x perrststat2=0x%08x iomg=0x%x/0x%x iocg=0x%x/0x%x g5=%s0x%08x\n",
		       kk_kapi_durum_ad[k->durum], k->plan, k->yazma, o.stat2, o.rst2, o.pmx[0], o.pmx[1], o.cfg[0], o.cfg[1],
		       k->g5_okundu ? "" : "(okunmadi)", k->g5);
	mutex_unlock(&k->kilit);
	return n;
}

static ssize_t kapi_store(struct device *dev, struct device_attribute *a, const char *buf, size_t len)
{
	struct kk_i3 *k = dev_get_drvdata(dev);
	struct kk_i3_io io = { .ctx = k, .rd = kk_i3_rd, .wr = kk_i3_mmio_yaz, .log = kk_i3_log };
	struct kk_i3_oku o;
	int r, plan;

	if (!sysfs_streq(buf, "ac"))
		return -EINVAL;
	mutex_lock(&k->kilit);
	if (k->durum != KK_KAPI_BEKLIYOR) {
		dev_info(dev, "kk: i2c3 kapi: 'ac' reddedildi (durum %s; tek seferlik)\n", kk_kapi_durum_ad[k->durum]);
		mutex_unlock(&k->kilit);
		return -EPERM;
	}
	k->durum = KK_KAPI_DUR;		/* tek seferlik: başarısızlıkta yeniden denenmez */
	kk_i3_oku(k, &o);		/* taze okuma: plan açılıştan beri değişmiş olabilir */
	kk_i3_durum_log(k, &o, "ac once");
	plan = kk_i3_plan(&o);
	dev_info(dev, "kk: i2c3 kapi: 'ac' (oturum tetigi): taze plan %d (acilis plani %d)\n", plan, k->plan);
	r = kk_i3_hazirla(&io, plan, &k->yazma);
	kk_i3_oku(k, &o);
	kk_i3_durum_log(k, &o, "ac sonra");
	if (r) {
		dev_warn(dev, "kk: i2c3 kapi: DUR (%d): %u yazma yapildi; i2c3 olusturulmadi\n", r, k->yazma);
	} else {
		r = kk_i3_olustur(k);
		if (!r)
			k->durum = KK_KAPI_HAZIR;
	}
	mutex_unlock(&k->kilit);
	return r ? -EIO : len;
}
static DEVICE_ATTR_RW(kapi);

static struct attribute *kk_i3_attrs[] = { &dev_attr_kapi.attr, NULL };
ATTRIBUTE_GROUPS(kk_i3);

static int kk_i3_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource r[3];
	struct kk_i3_oku o;
	struct kk_i3 *k;
	int i;

	k = devm_kzalloc(dev, sizeof(*k), GFP_KERNEL);
	if (!k)
		return -ENOMEM;
	k->dev = dev;
	mutex_init(&k->kilit);
	k->durum = KK_KAPI_DUR;
	platform_set_drvdata(pdev, k);

	k->i2c_np = of_parse_phandle(dev->of_node, "karakutu,i2c", 0);
	if (!k->i2c_np)
		return dev_err_probe(dev, -EINVAL, "kk: i2c3 kapi: karakutu,i2c yok\n");
	for (i = 0; i < 3; i++)
		if (of_address_to_resource(dev->of_node, i, &r[i]))
			goto dt_hata;
	if (of_address_to_resource(k->i2c_np, 0, &k->i2c_res[0]) || of_irq_to_resource(k->i2c_np, 0, &k->i2c_res[1]) <= 0)
		goto dt_hata;
	if (!kk_i3_taban_ok(r[0].start, resource_size(&r[0]), r[1].start, resource_size(&r[1]), r[2].start,
			    resource_size(&r[2]), k->i2c_res[0].start, IS_ENABLED(CONFIG_KK_S1_SAHTE))) {
		dev_err(dev, "kk: i2c3 kapi: fiziksel tabanlar beklenen degil (crg %pR pmx %pR cfg %pR i2c %pR); HICBIR SEY OKUNMADI\n",
			&r[0], &r[1], &r[2], &k->i2c_res[0]);
		of_node_put(k->i2c_np);
		return -EINVAL;
	}
	/* of_iomap/devm_ioremap: bölge talep edilmez (CRG başka sürücülerin de); yalnız gereken boy */
	k->b[KK_I3_B_CRG] = devm_ioremap(dev, r[0].start, KK_I3_CRG_BOY);
	k->b[KK_I3_B_PMX] = devm_ioremap(dev, r[1].start, KK_I3_PIN_BOY);
	k->b[KK_I3_B_CFG] = devm_ioremap(dev, r[2].start, KK_I3_PIN_BOY);
	if (!k->b[0] || !k->b[1] || !k->b[2]) {
		of_node_put(k->i2c_np);
		return -ENOMEM;
	}

	dev_info(dev, "kk: i2c3 kapi: basladi (MMIO beyaz listesi 4 girdi: PEREN2/PERRSTDIS2 <- 0x80, iomg 0x700/0x704 <- 1; acilista yazma YOK)%s\n",
		 IS_ENABLED(CONFIG_KK_S1_SAHTE) ? " [SAHTE tabanlara izin: yalniz QEMU]" : "");
	kk_i3_oku(k, &o);
	kk_i3_durum_log(k, &o, "acilis");
	k->plan = kk_i3_plan(&o);
	if (k->plan < 0) {
		dev_warn(dev, "kk: i2c3 kapi: DUR (%s); hicbir sey yazilmadi, i2c3 olusturulmadi\n",
			 k->plan == KK_I3_DUR_PMX ? "pinmux 0 ya da 1 degil" : "pinconf 4.9 degeri degil; pinconf yazmasi onayli degil");
		return 0;
	}
	if (k->plan == 0) {
		dev_info(dev, "kk: i2c3 kapi: kapilar acik (yazma gerekmiyor)\n");
		if (!kk_i3_olustur(k))
			k->durum = KK_KAPI_HAZIR;
		return 0;
	}
	k->durum = KK_KAPI_BEKLIYOR;
	dev_info(dev, "kk: i2c3 kapi: YAZMA GEREKIYOR (plan 0x%x:%s%s%s%s); acilista yazilmadi, oturumda sysfs 'kapi' <- 'ac' bekleniyor\n",
		 k->plan, k->plan & KK_I3_P_SAAT ? " PEREN2<-0x80" : "", k->plan & KK_I3_P_RESET ? " PERRSTDIS2<-0x80" : "",
		 k->plan & KK_I3_P_PMX0 ? " iomg700<-1" : "", k->plan & KK_I3_P_PMX1 ? " iomg704<-1" : "");
	return 0;

dt_hata:
	of_node_put(k->i2c_np);
	return dev_err_probe(dev, -EINVAL, "kk: i2c3 kapi: DT reg/irq okunamadi; hicbir sey okunmadi\n");
}

static void kk_i3_remove(struct platform_device *pdev)
{
	struct kk_i3 *k = platform_get_drvdata(pdev);

	if (k && k->i2c_pdev)
		platform_device_unregister(k->i2c_pdev);
	if (k)
		of_node_put(k->i2c_np);
}

static const struct of_device_id kk_i3_of[] = {
	{ .compatible = "karakutu,kirin710-i2c3-kapi" },
	{ }
};
MODULE_DEVICE_TABLE(of, kk_i3_of);

static struct platform_driver kk_i3_drv = {
	.probe = kk_i3_probe,
	.remove = kk_i3_remove,
	.driver = { .name = "kk-i2c3-kapi", .of_match_table = kk_i3_of, .dev_groups = kk_i3_groups },
};
module_platform_driver(kk_i3_drv);
MODULE_DESCRIPTION("Kara Kutu: i2c3 kapisi (G1-G4 okuma; gerekirse oturumda 4.9 gibi acma, geri okumali)");
MODULE_LICENSE("GPL");
