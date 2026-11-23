// SPDX-License-Identifier: GPL-2.0
/* OTOMATIK (araclar/stub_uret.py, G43): wifi/wal/wal_linux_atcmdsrv.c dislandi; bu dosya yalniz baglama icin
 * stub tanimlar. Asagida orijinalin onislemci yonergeleri (include/if/define) aynen,
 * sonra stub govdeleri. Davranis: void bos, isaretci NULL, tamsayi 0 (OAL_SUCC/false),
 * ozel durumlar araclar/elle_govde.py'de gerekceli. */
#ifdef __cplusplus
#if __cplusplus
#endif
#endif
#include "oal_ext_if.h"
#include "oal_profiling.h"
#include "oal_kernel_file.h"
#include "oam_ext_if.h"
#include "frw_ext_if.h"
#include "wlan_spec.h"
#include "wlan_types.h"
#include "mac_vap.h"
#include "mac_resource.h"
#include "mac_ie.h"
#include "hmac_resource.h"
#include "hmac_scan.h"
#include "hmac_ext_if.h"
#include "hmac_chan_mgmt.h"
#include "wal_main.h"
#include "wal_config.h"
#include "wal_regdb.h"
#include "wal_linux_scan.h"
#include "wal_linux_atcmdsrv.h"
#include "wal_linux_bridge.h"
#include "wal_linux_flowctl.h"
#include "wal_linux_event.h"
#if ((defined(_PRE_PRODUCT_ID_HI110X_DEV)) || (defined(_PRE_PRODUCT_ID_HI110X_HOST)))
#include "plat_cali.h"
#include "oal_hcc_host_if.h"
#if (_PRE_OS_VERSION_LINUX == _PRE_OS_VERSION)
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/pinctrl/consumer.h>
#include "board.h"
#ifdef _PRE_PLAT_FEATURE_CUSTOMIZE
#include "hisi_customize_wifi.h"
#endif /* #ifdef _PRE_PLAT_FEATURE_CUSTOMIZE */
#endif
#endif
#undef  THIS_FILE_ID
#define THIS_FILE_ID OAM_FILE_ID_WAL_LINUX_ATCMDSRV_C
#if (defined(_PRE_PRODUCT_ID_HI110X_DEV) || defined(_PRE_PRODUCT_ID_HI110X_HOST))
#if (_PRE_OS_VERSION_LINUX == _PRE_OS_VERSION)
#endif
#ifdef _PRE_WLAN_FEATURE_SMARTANT
#endif
#endif
#ifdef _PRE_WLAN_FEATURE_EQUIPMENT_TEST
#endif
#if (defined(_PRE_PRODUCT_ID_HI110X_DEV) || defined(_PRE_PRODUCT_ID_HI110X_HOST))
#ifdef _PRE_WLAN_FEATURE_11D
#endif
#if (_PRE_OS_VERSION_LINUX == _PRE_OS_VERSION)
#endif
#if (_PRE_OS_VERSION_LINUX == _PRE_OS_VERSION)
#endif
#if (_PRE_OS_VERSION_LINUX == _PRE_OS_VERSION)
#endif
#if 0
 #if (_PRE_OS_VERSION_LINUX == _PRE_OS_VERSION)
 #endif
#endif
#if (_PRE_OS_VERSION_LINUX == _PRE_OS_VERSION)
#else
#endif
#ifdef CONFIG_HUAWEI_DSM
#endif
#ifdef _PRE_WLAN_FEATURE_STA_PM
#endif
#ifdef _PRE_PLAT_FEATURE_CUSTOMIZE
#endif
#ifdef _PRE_WLAN_FEATURE_SMARTANT
#if 0//??????????????????????
#endif
#endif
#ifdef _PRE_PLAT_FEATURE_CUSTOMIZE
#endif
#endif
#if (_PRE_PRODUCT_ID == _PRE_PRODUCT_ID_HI1151) && defined(_PRE_WLAN_FEATURE_EQUIPMENT_TEST)
#endif
#ifdef __cplusplus
    #if __cplusplus
    #endif
#endif

/* ---- kk stub tanimlari ---- */
oal_uint8 g_uc_dev_lte_gpio_level;

oal_uint16 g_us_efuse_buffer[16];

oal_int32 wal_atcmdsrv_wifi_priv_cmd(oal_net_device_stru *pst_net_dev, oal_ifreq_stru *pst_ifr, oal_int32 ul_cmd)
{
    return 0;
}

oal_void wal_atcmsrv_ioctl_get_fem_pa_status(oal_net_device_stru *pst_net_dev, oal_int32 *pl_fem_pa_status)
{
    /* bos */
}

oal_int32 wal_atcmsrv_ioctl_get_rx_pckg(oal_net_device_stru *pst_net_dev, oal_int32 *pl_rx_pckg_succ_num)
{
    return 0;
}

