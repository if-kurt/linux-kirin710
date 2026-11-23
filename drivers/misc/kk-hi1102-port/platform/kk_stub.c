// SPDX-License-Identifier: GPL-2.0
/*
 * kk_stub.c - Kara Kutu G43 (Wi-Fi dilim 3): Hi1102 STA alt kumesinin ana hatta
 * (6.12) karsiligi olmayan dis sembolleri. Yalniz derleme/baglama icindir;
 * cihazda yukleme ayri onayla.
 *
 * Bolum 1: Huawei NVE (hisi_nve_direct_access) - ana hatta NVE surucusu yok.
 *   - Okuma, MACWLAN (#193): kk_wlan_mac modul parametresi verilmisse (12 onaltilik
 *     karakter, ornek "001122334455") onu dondurur; verilmemisse -ENODEV.
 *     -ENODEV'de hwifi_get_mac_addr rastgele MAC (xx:11:02:..) uretir (4.9 ile ayni hata yolu).
 *   - Okuma, diger numaralar (WINVRAM #340 dahil): -ENODEV. read_conf_from_nvram
 *     "ret < -1" ise INI_FAILED doner -> INI/DTS yedegine duser.
 *   - Yazma: her zaman -EACCES (-13). DIKKAT: -EPERM (-1) KULLANILMAZ; cagiranlar
 *     "ret < -1" ile hata ariyor, -1 basari sayilirdi.
 */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/mtd/hisi_nve_interface.h>

/* Lisans: oal_main.c:278 oal_module_license("GPL") (burada tekrar edilmez). */
MODULE_DESCRIPTION("Hi1102 platform (Kara Kutu ana hat portu, deneme)");

#define KK_NV_MACWLAN	193

#ifdef CONFIG_KK_HI1102_M3C
/* G57 M3c: varsayilan = CONFIG_KK_HI1102_M3C_MAC (bos degilse; cihazda 4.9 NV MAC'i). Ayni deger wlan0 + MIB StationID. */
static char *kk_wlan_mac = (sizeof(CONFIG_KK_HI1102_M3C_MAC) > 1) ? (char *)CONFIG_KK_HI1102_M3C_MAC : NULL;
const char *kk_m3c_mac_kaynak(void)
{
	if (!kk_wlan_mac)
		return "rastgele (kk_wlan_mac yok; 4.9 hata yolu xx:11:02)";
	if (strlen(kk_wlan_mac) != 12)
		return "rastgele (kk_wlan_mac 12 karakter degil)";
	return (sizeof(CONFIG_KK_HI1102_M3C_MAC) > 1 && !strcmp(kk_wlan_mac, CONFIG_KK_HI1102_M3C_MAC)) ?
		"CONFIG_KK_HI1102_M3C_MAC (4.9 NV MACWLAN degeri)" : "hi1102_plat.kk_wlan_mac";
}
#else
static char *kk_wlan_mac;
#endif
module_param(kk_wlan_mac, charp, 0444);
MODULE_PARM_DESC(kk_wlan_mac, "NVE #193 MACWLAN yerine: 12 onaltilik karakter (ornek 001122334455)");

int hisi_nve_direct_access(struct hisi_nve_info_user *user_info)
{
	size_t n;

	if (!user_info)
		return -EINVAL;
	if (user_info->nv_operation != NV_READ)
		return -EACCES;
	if (user_info->nv_number == KK_NV_MACWLAN && kk_wlan_mac) {
		n = strlen(kk_wlan_mac);
		if (n != 12)
			return -EINVAL;
		memset(user_info->nv_data, 0, sizeof(user_info->nv_data));
		memcpy(user_info->nv_data, kk_wlan_mac, n);
		return 0;
	}
	return -ENODEV;
}
