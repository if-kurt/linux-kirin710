// SPDX-License-Identifier: GPL-2.0
/* OTOMATIK (araclar/stub_uret.py, G43): wifi/hmac/hmac_mgmt_ap.c dislandi; bu dosya yalniz baglama icin
 * stub tanimlar. Asagida orijinalin onislemci yonergeleri (include/if/define) aynen,
 * sonra stub govdeleri. Davranis: void bos, isaretci NULL, tamsayi 0 (OAL_SUCC/false),
 * ozel durumlar araclar/elle_govde.py'de gerekceli. */
#ifdef __cplusplus
#if __cplusplus
#endif
#endif
#include "oal_cfg80211.h"
#include "oam_ext_if.h"
#include "wlan_spec.h"
#include "hmac_mgmt_ap.h"
#include "hmac_encap_frame.h"
#include "hmac_encap_frame_ap.h"
#include "hmac_mgmt_bss_comm.h"
#include "hmac_mgmt_sta.h"
#include "mac_frame.h"
#include "hmac_rx_data.h"
#include "hmac_uapsd.h"
#include "hmac_tx_amsdu.h"
#include "mac_ie.h"
#include "mac_user.h"
#include "hmac_user.h"
#include "hmac_11i.h"
#include "hmac_protection.h"
#include "hmac_chan_mgmt.h"
#include "hmac_smps.h"
#include "hmac_fsm.h"
#include "hmac_ext_if.h"
#include "hmac_config.h"
#ifdef _PRE_WLAN_FEATURE_CUSTOM_SECURITY
#include "hmac_custom_security.h"
#endif
#include "hmac_dfs.h"
#include "hmac_p2p.h"
#include "hmac_blockack.h"
#if (_PRE_MULTI_CORE_MODE_OFFLOAD_DMAC == _PRE_MULTI_CORE_MODE)
#include "hmac_ext_if.h"
#endif
#ifdef _PRE_WLAN_FEATURE_HILINK
#include "hmac_fbt_main.h"
#endif
#ifdef _PRE_WLAN_FEATURE_WMMAC
#include "hmac_wmmac.h"
#endif
#undef  THIS_FILE_ID
#define THIS_FILE_ID OAM_FILE_ID_HMAC_MGMT_AP_C
#if (_PRE_WLAN_FEATURE_PMF != _PRE_PMF_NOT_SUPPORT)
#endif
#if (_PRE_MULTI_CORE_MODE_OFFLOAD_DMAC == _PRE_MULTI_CORE_MODE)
#endif
#if (_PRE_MULTI_CORE_MODE_OFFLOAD_DMAC == _PRE_MULTI_CORE_MODE)
#else
#endif
#ifdef _PRE_DEBUG_MODE
#endif
#ifdef _PRE_DEBUG_MODE
#endif /* DEBUG_MODE */
#if (_PRE_WLAN_FEATURE_PMF != _PRE_PMF_NOT_SUPPORT)
#endif
#if defined(_PRE_WLAN_FEATURE_WPA) || defined(_PRE_WLAN_FEATURE_WPA2)
#endif /*(_PRE_WLAN_FEATURE_WPA) || defined(_PRE_WLAN_FEATURE_WPA2)*/
#ifdef _PRE_WLAN_FEATURE_TXBF
#endif
#if defined(_PRE_WLAN_FEATURE_WPA) || defined(_PRE_WLAN_FEATURE_WPA2)
#endif /* defined (_PRE_WLAN_FEATURE_WPA) ||??defined(_PRE_WLAN_FEATURE_WPA2) */
#ifdef _PRE_WLAN_FEATURE_TXBF
#endif
#if (_PRE_WLAN_FEATURE_PMF != _PRE_PMF_NOT_SUPPORT)
#endif
#ifdef _PRE_WLAN_FEATURE_OPMODE_NOTIFY
#endif
#if (_PRE_MULTI_CORE_MODE_OFFLOAD_DMAC != _PRE_MULTI_CORE_MODE)
#endif
#if (_PRE_WLAN_FEATURE_PMF != _PRE_PMF_NOT_SUPPORT)
#endif
#ifdef _PRE_WLAN_FEATURE_SMPS
#endif
#ifdef _PRE_WLAN_FEATURE_P2P
#endif
#ifdef _PRE_WLAN_FEATURE_SMPS
#endif
#if (_PRE_WLAN_FEATURE_PMF != _PRE_PMF_NOT_SUPPORT)
#endif
        #ifdef _PRE_DEBUG_MODE_USER_TRACK
        #endif
#if (_PRE_MULTI_CORE_MODE_OFFLOAD_DMAC == _PRE_MULTI_CORE_MODE)
#endif
        #if defined (_PRE_WLAN_FEATURE_WPA) || defined(_PRE_WLAN_FEATURE_WPA2)
        #endif /* defined(_PRE_WLAN_FEATURE_WPA) ||??defined(_PRE_WLAN_FEATURE_WPA2) */
        #ifdef _PRE_DEBUG_MODE_USER_TRACK
        #endif
#ifdef _PRE_WLAN_FEATURE_P2P
#endif
#if (_PRE_OS_VERSION_LINUX == _PRE_OS_VERSION)
#if defined(_PRE_PRODUCT_ID_HI110X_HOST)
#endif
#endif
    #if (_PRE_MULTI_CORE_MODE_OFFLOAD_DMAC != _PRE_MULTI_CORE_MODE)
    #endif
    #ifdef _PRE_DEBUG_MODE_USER_TRACK
    #endif
#ifdef _PRE_WLAN_FEATURE_SMPS
#endif
#if (_PRE_MULTI_CORE_MODE_OFFLOAD_DMAC == _PRE_MULTI_CORE_MODE)
#if (_PRE_OS_VERSION_LINUX == _PRE_OS_VERSION)
#endif
#endif
#if (_PRE_WLAN_FEATURE_PMF != _PRE_PMF_NOT_SUPPORT)
 #endif
#ifdef _PRE_DEBUG_MODE_USER_TRACK
#endif
#ifdef _PRE_WLAN_FEATURE_20_40_80_COEXIST
#endif
#ifdef _PRE_WLAN_FEATURE_20_40_80_COEXIST
#endif
#ifdef _PRE_WLAN_FEATURE_SMPS
#endif
            #ifdef _PRE_WLAN_FEATURE_20_40_80_COEXIST
            #endif  /* _PRE_WLAN_FEATURE_20_40_80_COEXIST */
            #ifdef _PRE_WLAN_FEATURE_P2P
            #endif  /* _PRE_WLAN_FEATURE_P2P */
            #ifdef _PRE_WLAN_FEATURE_20_40_80_COEXIST
            #endif
            #ifdef _PRE_WLAN_FEATURE_SMPS
            #endif
#if (_PRE_WLAN_FEATURE_PMF != _PRE_PMF_NOT_SUPPORT)
#endif
            #ifdef _PRE_WLAN_FEATURE_OPMODE_NOTIFY
            #endif
    #ifdef _PRE_WLAN_FEATURE_P2P
    #endif
#ifdef _PRE_WLAN_FEATURE_WMMAC
#endif //_PRE_WLAN_FEATURE_WMMAC
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4,7,0))
#else
#endif
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4,7,0))
#else
#endif
#ifdef _PRE_WLAN_FEATURE_HILINK
#endif  /* _PRE_WLAN_FEATURE_HILINK */
#ifdef _PRE_WLAN_FEATURE_CUSTOM_SECURITY
#endif
#if (_PRE_MULTI_CORE_MODE_OFFLOAD_DMAC == _PRE_MULTI_CORE_MODE)
#ifdef _PRE_WLAN_WAKEUP_SRC_PARSE
#endif
#endif
#ifdef _PRE_WLAN_FEATURE_HILINK
#endif  /* _PRE_WLAN_FEATURE_HILINK */
#ifdef _PRE_WLAN_FEATURE_CUSTOM_SECURITY
#endif
#ifdef _PRE_WLAN_FEATURE_20_40_80_COEXIST
#endif
    #ifdef _PRE_WLAN_FEATURE_DFS
    #endif
    #ifdef _PRE_WLAN_FEATURE_DFS
    #endif
#ifdef __cplusplus
    #if __cplusplus
    #endif
#endif

/* ---- kk stub tanimlari ---- */
oal_uint32 hmac_ap_up_misc(hmac_vap_stru *pst_hmac_vap, oal_void *p_param)
{
    return 0;
}

oal_uint32 hmac_ap_up_rx_mgmt(hmac_vap_stru *pst_hmac_vap, oal_void *p_param)
{
    return 0;
}

oal_uint32 hmac_ap_wait_start_misc(hmac_vap_stru *pst_hmac_vap, oal_void *p_param)
{
    return 0;
}

oal_uint32 hmac_ap_wait_start_rx_mgmt(hmac_vap_stru *pst_hmac_vap, oal_void *p_param)
{
    return 0;
}

oal_void hmac_handle_disconnect_rsp_ap(hmac_vap_stru *pst_hmac_vap, hmac_user_stru *pst_hmac_user)
{
    /* bos */
}


/* G54 M3a P5 (elle): P2P kapalıyken hmac_mgmt_sta.c:2367-2431 (#ifndef P2P) hmac_sta_sync_bss_freq bunu çağırır. AP derlenmedi
 * (hmac_mgmt_ap.c dışlı); STA-yalnız akışta erişilmez (döngü yalnız BSS_AP VAP'larında). OAL_FAIL: senkron yapılmaz (güvenli yön). */
oal_uint32 hmac_ap_clean_bss(hmac_vap_stru *pst_hmac_vap)
{
    return OAL_FAIL;
}
