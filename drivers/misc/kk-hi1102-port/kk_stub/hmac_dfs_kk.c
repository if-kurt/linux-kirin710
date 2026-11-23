// SPDX-License-Identifier: GPL-2.0
/* OTOMATIK (araclar/stub_uret.py, G43): wifi/hmac/hmac_dfs.c dislandi; bu dosya yalniz baglama icin
 * stub tanimlar. Asagida orijinalin onislemci yonergeleri (include/if/define) aynen,
 * sonra stub govdeleri. Davranis: void bos, isaretci NULL, tamsayi 0 (OAL_SUCC/false),
 * ozel durumlar araclar/elle_govde.py'de gerekceli. */
#ifdef __cplusplus
#if __cplusplus
#endif
#endif
#ifdef _PRE_WLAN_FEATURE_DFS
#include "hmac_fsm.h"
#include "hmac_chan_mgmt.h"
#include "dmac_scan.h"
#include "hmac_dfs.h"
#include "hmac_scan.h"
#include "hmac_resource.h"
#undef  THIS_FILE_ID
#define THIS_FILE_ID OAM_FILE_ID_HMAC_DFS_C
#define HMAC_DFS_IS_CHAN_WEATHER_RADAR(_us_freq) \
    ((5600 <= (_us_freq)) && ((_us_freq) <= 5650))
#ifdef _PRE_WLAN_FEATURE_OFFCHAN_CAC
#endif
#if 0
#endif
#if defined(_PRE_SUPPORT_ACS) || defined(_PRE_WLAN_FEATURE_DFS) || defined(_PRE_WLAN_FEATURE_20_40_80_COEXIST)
#endif
#ifdef _PRE_WLAN_FEATURE_OFFCHAN_CAC
#endif
#define HMAC_40MHZ_BITMAP_MASK     0xF7BFFFF
#define HMAC_80MHZ_BITMAP_MASK     0xFFFFFF
#ifdef _PRE_WLAN_FEATURE_OFFCHAN_CAC
#endif
#ifdef _PRE_WLAN_FEATURE_OFFCHAN_CAC
#endif
#if 0
#endif
#ifdef _PRE_WLAN_FEATURE_OFFCHAN_CAC
#endif
#ifdef _PRE_WLAN_FEATURE_OFFCHAN_CAC
#endif
#ifdef _PRE_SUPPORT_ACS
#endif
#ifdef _PRE_SUPPORT_ACS
#else
#endif
#ifdef _PRE_SUPPORT_ACS
#endif
#ifdef _PRE_WLAN_CHIP_TEST
#if (_PRE_MULTI_CORE_MODE_OFFLOAD_DMAC == _PRE_MULTI_CORE_MODE)
#endif
#endif
#endif   /* end of _PRE_WLAN_FEATURE_DFS */
#ifdef __cplusplus
    #if __cplusplus
    #endif
#endif

/* ---- kk stub tanimlari ---- */
oal_void hmac_dfs_cac_start(mac_device_stru *pst_mac_device, hmac_vap_stru *pst_hmac_vap)
{
    /* bos */
}

oal_void hmac_dfs_cac_stop(mac_device_stru *pst_mac_device)
{
    /* bos */
}

oal_void hmac_dfs_channel_list_init(mac_device_stru *pst_mac_device)
{
    /* bos */
}

oal_void hmac_dfs_init(mac_device_stru *pst_mac_device)
{
    /* bos */
}

oal_uint32 hmac_dfs_init_scan_hook(hmac_scan_record_stru *pst_scan_record, hmac_device_stru *pst_hmac_device)
{
    return 0;
}

oal_void hmac_dfs_off_cac_stop(mac_device_stru *pst_mac_device, mac_vap_stru *pst_mac_vap)
{
    /* bos */
}

oal_uint32 hmac_dfs_radar_detect_event(frw_event_mem_stru *pst_event_mem)
{
    return 0;
}

oal_void hmac_dfs_radar_wait(mac_device_stru *pst_mac_device, oal_uint8 uc_chan_idx)
{
    /* bos */
}

