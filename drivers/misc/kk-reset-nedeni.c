// SPDX-License-Identifier: GPL-2.0
/*
 * Kara Kutu (G47, KV7) — önceki reset nedeninin YALNIZ OKUNMASI, açılışta BİR kez log.
 *
 *  PMIC HRST_REG13 (0x1E1; 4.9 PMU_RESET_REG_OFFSET, mntn_public_interface.h:7): kk-spmi-syscon regmap'i
 *     (kk-sensor-okuma ile aynı yol: spmi_find_device_by_of_node + dev_get_regmap + regmap_read).
 *  SCTRL SCSYSSTAT (0xFFF0A004; soc_sctrl_interface.h:9, bitler 270-320): SCTRL syscon regmap'i
 *     (kk-sensor-okuma SCBAKDATA4 ile aynı yol: syscon_regmap_lookup_by_phandle + regmap_read).
 *
 * YAZMA YOK: bu dosyada regmap_write/regmap_update_bits/writel çağrısı yoktur (yazma kanıtı: g47/s2/yazma-kaniti).
 * DİKKAT: SCSYSSTAT'a herhangi bir değer YAZMAK sistemi sıfırlar (4.9 hisi_pmic_mntn_spmi.c:484 0xdeadbeef);
 * burada yalnız okunur. Okunan adresler derleme zamanında sabittir (KK_RN_PMIC_REG / KK_RN_SCTRL_OFS); DT'den
 * adres alınmaz. Bootloader 0x1E1'i kernel'e geçmeden 0xff yapıyor (RECON G24); okuma bunu ölçmek içindir.
 * QEMU: "karakutu,sahte-pmic-syscon" (yalnız QEMU DTB'si) ile PMIC yerine RAM'deki sahte syscon okunur.
 *
 * G57 G-2 (reboot-mode yeni değerler; YAZMA YİNE YOK): yazmayı değişmeden syscon-reboot-mode yapar (DT'deki
 * mode-* değerleri, reboot bildirimi öncelik 0). Bu sürücü ondan SONRA çalışan bir reboot bildirimi (öncelik -10)
 * kurar: 0x1E1'i bir kez OKUR ve "yazma kanıtı" satırını basar (KERN_EMERG -> konsol/pstore; kmsg_dump
 * KMSG_DUMP_SHUTDOWN bildirimlerden sonra olduğu için dmesg-ramoops'a da girer). Açılışta reboot-mode düğümündeki
 * mode-* değerleri beyaz listeyle karşılaştırılır (4.9 kaynağı: mntn_public_interface.h:194-209, rdr_common.c +
 * mntn_reboot_reason_map.h, hisi_poweroff.c:92-118): bootloader 0x01, erecovery 0x08, normal 0x10 (= 4.9 COLDBOOT,
 * parametresiz reboot). Liste dışı mod ya da offset/mask farkı = "TUTARSIZ" uyarısı (çalışma anında YAZMA
 * engellenemez: engel build.sh DTB denetimidir; geri okuma satırı kanıttır).
 */
#include <linux/bits.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/regmap.h>
#include <linux/spmi.h>
#include <linux/workqueue.h>

#define KK_RN_PMIC_REG		0x1e1	/* HRST_REG13: reboot nedeni (alt 8 bit) */
#define KK_RN_SCTRL_OFS		0x004	/* SCSYSSTAT: yalnız OKUMA */
#define KK_RN_DENEME		20	/* PMIC regmap'i hazır olana kadar 20 x 500 ms */

/* G57 G-2: reboot-mode beyaz listesi (ad, 0x1E1 değeri) — 4.9 ile aynı sayılar */
static const struct { const char *ad; u32 deger; } kk_rn_beyaz[] = {
	{ "bootloader",	0x01 },	/* 4.9 bootloader (G25'ten beri ana hatta kullanılan tek değer) */
	{ "erecovery",	0x08 },	/* 4.9 erecovery (G20: 4.9 "reboot erecovery" -> bootloader mod 3) */
	{ "normal",	0x10 },	/* 4.9 COLDBOOT: parametresiz reboot; bootloader'ın "fastboot reboot" değeri */
};

struct kk_rn {
	struct device *dev;
	struct device_node *pmic_np;
	struct regmap *sctrl;
	struct regmap *sahte_pmic;
	struct delayed_work is;
	int deneme;
	struct device_node *rm_np;	/* G57 G-2: PMIC düğümünün reboot-mode çocuğu (yoksa NULL) */
	struct notifier_block nb;
};

static const char *kk_rn_ad(unsigned int v)
{
	switch (v) {
	case 0x00: return "AP_S_COLDBOOT";
	case 0x01: return "bootloader";
	case 0x08: return "erecovery";
	case 0x10: return "COLDBOOT";
	case 0x43: return "AP_S_AWDT";
	case 0x60: return "AP_S_PANIC";
	case 0xff: return "bootloader-set_reboot_type";
	default: return "?";
	}
}

static void kk_rn_oku(struct work_struct *w)
{
	struct kk_rn *k = container_of(to_delayed_work(w), struct kk_rn, is);
	struct spmi_device *sd = NULL;
	struct regmap *pm = k->sahte_pmic;
	unsigned int p = 0, s = 0;
	int rp = -ENODEV, rs = -ENODEV;

	if (!pm && k->pmic_np) {
		sd = spmi_find_device_by_of_node(k->pmic_np);
		if (sd)
			pm = dev_get_regmap(&sd->dev, NULL);
		if (!pm && ++k->deneme < KK_RN_DENEME) {
			if (sd)
				put_device(&sd->dev);
			schedule_delayed_work(&k->is, msecs_to_jiffies(500));
			return;
		}
	}
	if (pm)
		rp = regmap_read(pm, KK_RN_PMIC_REG, &p);	/* yalnız okuma */
	if (sd)
		put_device(&sd->dev);
	if (k->sctrl)
		rs = regmap_read(k->sctrl, KK_RN_SCTRL_OFS, &s);	/* yalnız okuma; YAZMA = reset */

	dev_info(k->dev, "kk: reset-nedeni: PMIC 0x1e1=0x%02x (%s, r=%d, %s) SCTRL SCSYSSTAT(0x004)=0x%08x (r=%d) soft=%lu wd0=%lu wd1=%lu lpmcu=%lu tsensor=%lu%lu%lu iomcu=%lu modem_wd=%lu; yalniz okuma, 1 kez\n",
		 p & 0xff, rp ? "OKUNMADI" : kk_rn_ad(p & 0xff), rp,
		 k->sahte_pmic ? "SAHTE syscon" : "SPMI", s, rs,
		 (s & BIT(3)) >> 3, (s & BIT(4)) >> 4, (s & BIT(5)) >> 5, (s & BIT(6)) >> 6,
		 (s & BIT(0)), (s & BIT(1)) >> 1, (s & BIT(2)) >> 2, (s & BIT(8)) >> 8, (s & BIT(11)) >> 11);
}

/* G57 G-2: ad beyaz listede ve değer aynı mı (0 = evet) */
static int kk_rn_beyaz_mi(const char *ad, u32 deger)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(kk_rn_beyaz); i++)
		if (!strcmp(kk_rn_beyaz[i].ad, ad))
			return kk_rn_beyaz[i].deger == deger ? 0 : -EINVAL;
	return -ENOENT;
}

/* G57 G-2: açılışta bir kez, yalnız DT okunur (donanım erişimi yok) */
static void kk_rn_rm_denetle(struct kk_rn *k)
{
	struct property *pr;
	char liste[160] = "";
	u32 v, off = 0, mask = 0;
	int n = 0, kotu = 0;

	if (!k->rm_np) {
		dev_warn(k->dev, "kk: reset-nedeni: reboot-mode dugumu YOK; geri okuma bildirimi kurulmadi\n");
		return;
	}
	if (of_property_read_u32(k->rm_np, "offset", &off) || off != KK_RN_PMIC_REG)
		kotu++;
	if (of_property_read_u32(k->rm_np, "mask", &mask) || mask != 0xff)
		kotu++;
	for_each_property_of_node(k->rm_np, pr) {
		if (strncmp(pr->name, "mode-", 5))
			continue;
		if (of_property_read_u32(k->rm_np, pr->name, &v)) {
			kotu++;
			continue;
		}
		n++;
		if (kk_rn_beyaz_mi(pr->name + 5, v))
			kotu++;
		scnprintf(liste + strlen(liste), sizeof(liste) - strlen(liste), "%s%s=0x%02x%s", n > 1 ? " " : "",
			  pr->name + 5, v, kk_rn_beyaz_mi(pr->name + 5, v) ? "(LISTE DISI)" : "");
	}
	if (kotu)
		dev_warn(k->dev, "kk: reset-nedeni: reboot-mode DT TUTARSIZ: offset 0x%x mask 0x%x modlar: %s (beyaz liste bootloader=0x01 erecovery=0x08 normal=0x10)\n",
			 off, mask, n ? liste : "-");
	else
		dev_info(k->dev, "kk: reset-nedeni: reboot-mode DT: offset 0x%x mask 0x%x modlar: %s; beyaz liste TUTTU; reboot'ta 0x1e1 geri okuma (yalniz okuma)\n",
			 off, mask, n ? liste : "-");
}

/* G57 G-2: syscon-reboot-mode'dan (öncelik 0) SONRA; 0x1E1 bir kez OKUNUR, kanıt satırı basılır. YAZMA YOK. */
static int kk_rn_reboot(struct notifier_block *nb, unsigned long olay, void *veri)
{
	struct kk_rn *k = container_of(nb, struct kk_rn, nb);
	const char *cmd = veri ? (const char *)veri : "normal";	/* reboot-mode.c ile aynı kural */
	const char *ol = olay == SYS_RESTART ? "restart" : olay == SYS_POWER_OFF ? "poweroff" :
			 olay == SYS_HALT ? "halt" : "?";
	struct spmi_device *sd = NULL;
	struct regmap *pm = k->sahte_pmic;
	char ad[48];
	unsigned int p = 0;
	u32 bek = 0;
	bool var = false;
	int r = -ENODEV;

	if (snprintf(ad, sizeof(ad), "mode-%s", cmd) < sizeof(ad))
		var = !of_property_read_u32(k->rm_np, ad, &bek);
	if (!pm && k->pmic_np) {
		sd = spmi_find_device_by_of_node(k->pmic_np);
		if (sd)
			pm = dev_get_regmap(&sd->dev, NULL);
	}
	if (pm)
		r = regmap_read(pm, KK_RN_PMIC_REG, &p);	/* yalnız okuma */
	if (sd)
		put_device(&sd->dev);
	if (r)
		pr_emerg("kk: reboot-mode: olay %s komut '%.32s' -> 0x1e1 OKUNMADI (r=%d); beklenen %s0x%02x; yalniz okuma\n",
			 ol, cmd, r, var ? "" : "(DT'de yok) ", var ? bek : 0);
	else if (!var)
		pr_emerg("kk: reboot-mode: olay %s komut '%.32s' DT'de YOK -> yazma yapilmadi; 0x1e1=0x%02x (%s; bootloader yorumu OLCULMEDI); yalniz okuma\n",
			 ol, cmd, p & 0xff, kk_rn_ad(p & 0xff));
	else
		pr_emerg("kk: reboot-mode: olay %s komut '%.32s' -> 0x1e1 beklenen 0x%02x geri okuma 0x%02x %s%s; yalniz okuma\n",
			 ol, cmd, bek, p & 0xff, (p & 0xff) == bek ? "TUTTU" : "TUTMADI",
			 kk_rn_beyaz_mi(cmd, bek) ? " (BEYAZ LISTE DISI)" : "");
	return NOTIFY_DONE;
}

static int kk_rn_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct kk_rn *k;

	k = devm_kzalloc(dev, sizeof(*k), GFP_KERNEL);
	if (!k)
		return -ENOMEM;
	k->dev = dev;
	k->pmic_np = of_parse_phandle(np, "karakutu,pmic-okuma", 0);
	k->sctrl = syscon_regmap_lookup_by_phandle(np, "karakutu,sctrl");
	if (IS_ERR(k->sctrl))
		k->sctrl = NULL;
	k->sahte_pmic = syscon_regmap_lookup_by_phandle(np, "karakutu,sahte-pmic-syscon");
	if (IS_ERR(k->sahte_pmic))
		k->sahte_pmic = NULL;
	INIT_DELAYED_WORK(&k->is, kk_rn_oku);
	platform_set_drvdata(pdev, k);
	schedule_delayed_work(&k->is, 0);
	/* G57 G-2: reboot-mode düğümü (cihaz: karakutu,pmic-okuma; QEMU: karakutu,sahte-pmic-syscon) */
	{
		struct device_node *pn = k->sahte_pmic ? of_parse_phandle(np, "karakutu,sahte-pmic-syscon", 0)
						       : of_node_get(k->pmic_np);

		k->rm_np = pn ? of_get_child_by_name(pn, "reboot-mode") : NULL;
		of_node_put(pn);
	}
	kk_rn_rm_denetle(k);
	if (k->rm_np) {
		k->nb.notifier_call = kk_rn_reboot;
		k->nb.priority = -10;	/* reboot-mode.c bildirimi öncelik 0: o yazar, bu sonra okur */
		register_reboot_notifier(&k->nb);
	}
	return 0;
}

static void kk_rn_remove(struct platform_device *pdev)
{
	struct kk_rn *k = platform_get_drvdata(pdev);

	cancel_delayed_work_sync(&k->is);
	if (k->rm_np) {
		unregister_reboot_notifier(&k->nb);
		of_node_put(k->rm_np);
	}
	of_node_put(k->pmic_np);
}

static const struct of_device_id kk_rn_of_match[] = {
	{ .compatible = "karakutu,reset-nedeni" },
	{ }
};
MODULE_DEVICE_TABLE(of, kk_rn_of_match);

static struct platform_driver kk_rn_driver = {
	.probe = kk_rn_probe,
	.remove = kk_rn_remove,
	.driver = {
		.name = "kk-reset-nedeni",
		.of_match_table = kk_rn_of_match,
	},
};
module_platform_driver(kk_rn_driver);

MODULE_DESCRIPTION("Kara Kutu: onceki reset nedeni (PMIC 0x1E1, SCTRL SCSYSSTAT) yalniz okuma");
MODULE_LICENSE("GPL");
