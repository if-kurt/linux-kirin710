// SPDX-License-Identifier: GPL-2.0
/* OTOMATIK (araclar/stub_uret.py, G43): wifi/hmac/hmac_roam_main.c dislandi; bu dosya yalniz baglama icin
 * stub tanimlar. Asagida orijinalin onislemci yonergeleri (include/if/define) aynen,
 * sonra stub govdeleri. Davranis: void bos, isaretci NULL, tamsayi 0 (OAL_SUCC/false),
 * ozel durumlar araclar/elle_govde.py'de gerekceli. */
#ifdef __cplusplus
#if __cplusplus
#endif
#endif
#ifdef _PRE_WLAN_FEATURE_ROAM
#include "oam_ext_if.h"
#include "mac_ie.h"
#include "mac_device.h"
#include "mac_resource.h"
#include "dmac_ext_if.h"
#include "hmac_fsm.h"
#include "hmac_sme_sta.h"
#include "hmac_mgmt_sta.h"
#include "hmac_resource.h"
#include "hmac_device.h"
#include "hmac_roam_main.h"
#include "hmac_roam_connect.h"
#include "hmac_roam_alg.h"
#ifdef _PRE_PLAT_FEATURE_CUSTOMIZE
#include "hisi_customize_wifi.h"
#endif /* #ifdef _PRE_PLAT_FEATURE_CUSTOMIZE */
#undef  THIS_FILE_ID
#define THIS_FILE_ID OAM_FILE_ID_HMAC_ROAM_MAIN_C
#if (_PRE_OS_VERSION_LINUX == _PRE_OS_VERSION)
#endif
#ifdef _PRE_PLAT_FEATURE_CUSTOMIZE
#else
#endif
#ifdef _PRE_PLAT_FEATURE_CUSTOMIZE
#else
#endif
#ifdef _PRE_WLAN_FEATURE_STA_PM
#endif
#ifdef _PRE_WLAN_FEATURE_11R
#endif //_PRE_WLAN_FEATURE_11R
#ifdef _PRE_WLAN_FEATURE_11R
#endif //_PRE_WLAN_FEATURE_11R
#if 0
#endif
#ifdef _PRE_WLAN_FEATURE_STA_PM
#endif
#ifdef  _PRE_WLAN_FEATURE_VOWIFI
#endif /* _PRE_WLAN_FEATURE_VOWIFI */
#ifdef _PRE_WLAN_FEATURE_11R
#endif //_PRE_WLAN_FEATURE_11R
#if 0
#endif
#endif //_PRE_WLAN_FEATURE_ROAM
#ifdef __cplusplus
    #if __cplusplus
    #endif
#endif

/* ---- kk stub tanimlari ---- */
oal_void hmac_roam_add_key_done(hmac_vap_stru *pst_hmac_vap)
{
    /* bos */
}

oal_uint32 hmac_roam_band(hmac_vap_stru *pst_hmac_vap, oal_uint8 uc_scan_band)
{
    return 0;
}

oal_uint32 hmac_roam_check_bkscan_result(hmac_vap_stru *pst_hmac_vap, void *p_scan_record)
{
    return 0;
}

oal_uint32 hmac_roam_enable(hmac_vap_stru *pst_hmac_vap, oal_uint8 uc_enable)
{
    return 0;
}

oal_uint32 hmac_roam_exit(hmac_vap_stru *pst_hmac_vap)
{
    return 0;
}

oal_uint32 hmac_roam_info_init(hmac_vap_stru *pst_hmac_vap)
{
    return 0;
}

oal_uint32 hmac_roam_init(hmac_vap_stru *pst_hmac_vap)
{
    return 0;
}

oal_uint32 hmac_roam_org(hmac_vap_stru *pst_hmac_vap, oal_uint8 uc_scan_orthogonal)
{
    return 0;
}

oal_uint32 hmac_roam_pause_user(hmac_vap_stru *pst_hmac_vap, oal_void *p_param)
{
    return 0;
}

oal_uint32 hmac_roam_reassoc(hmac_vap_stru *pst_hmac_vap)
{
    return 0;
}

oal_uint32 hmac_roam_resume_user(hmac_vap_stru *pst_hmac_vap, oal_void *p_param)
{
    return 0;
}

oal_uint32 hmac_roam_rx_ft_action(hmac_vap_stru *pst_hmac_vap, oal_netbuf_stru *pst_netbuf)
{
    return 0;
}

oal_uint32 hmac_roam_show(hmac_vap_stru *pst_hmac_vap)
{
    return 0;
}

oal_uint32 hmac_roam_start(hmac_vap_stru *pst_hmac_vap,oal_bool_enum_uint8 en_no_scan)
{
    return 0;
}

oal_uint32 hmac_roam_trigger_handle(hmac_vap_stru *pst_hmac_vap, oal_int8 c_rssi)
{
    return 0;
}

oal_void hmac_roam_wpas_connect_state_notify(hmac_vap_stru *pst_hmac_vap, wpas_connect_state_enum_uint32 conn_state)
{
    /* bos */
}

oal_uint32 hmac_sta_roam_rx_mgmt(hmac_vap_stru *pst_hmac_vap, oal_void *p_param)
{
    return 0;
}

