// SPDX-License-Identifier: GPL-2.0
/* OTOMATIK (araclar/stub_uret.py, G43): wifi/wal/wal_linux_cfgvendor.c dislandi; bu dosya yalniz baglama icin
 * stub tanimlar. Asagida orijinalin onislemci yonergeleri (include/if/define) aynen,
 * sonra stub govdeleri. Davranis: void bos, isaretci NULL, tamsayi 0 (OAL_SUCC/false),
 * ozel durumlar araclar/elle_govde.py'de gerekceli. */
#ifdef __cplusplus
#if __cplusplus
#endif
#endif
#include "oal_net.h"
#include "oal_cfg80211.h"
#include "oal_ext_if.h"
#include "frw_ext_if.h"
#include "wal_linux_cfgvendor.h"
#include "wal_linux_ioctl.h"
#include "wal_linux_cfg80211.h"
#undef  THIS_FILE_ID
#define THIS_FILE_ID OAM_FILE_ID_WAL_LINUX_CFGVENDOR_C
#define OUI_VENDOR      0x001A11
#define OUI_HISI        0x001018
#if (defined(_PRE_PRODUCT_ID_HI110X_HOST) || (LINUX_VERSION_CODE >= KERNEL_VERSION(3,14,0))) && (_PRE_OS_VERSION_LINUX == _PRE_OS_VERSION)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4,7,0))
#else
#endif
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4,7,0))
#else
#endif
#ifdef _PRE_WLAN_FEATURE_11D
#else
#endif
#ifdef _PRE_WLAN_FEATURE_HS20
#endif
#endif  /* (defined(_PRE_PRODUCT_ID_HI110X_HOST) || (LINUX_VERSION_CODE >= KERNEL_VERSION(3,14,0))) && (_PRE_OS_VERSION_LINUX == _PRE_OS_VERSION) */
#ifdef __cplusplus
    #if __cplusplus
    #endif
#endif

/* ---- kk stub tanimlari ---- */
wal_cfgvendor_radio_stat_stru g_st_wifi_radio_stat;

oal_void wal_cfgvendor_deinit(oal_wiphy_stru *wiphy)
{
    /* bos */
}

oal_void wal_cfgvendor_init(oal_wiphy_stru *wiphy)
{
    /* bos */
}

