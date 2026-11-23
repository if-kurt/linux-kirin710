// SPDX-License-Identifier: GPL-2.0
/* OTOMATIK (araclar/stub_uret.py, G43): wifi/hmac/hmac_btcoex.c dislandi; bu dosya yalniz baglama icin
 * stub tanimlar. Asagida orijinalin onislemci yonergeleri (include/if/define) aynen,
 * sonra stub govdeleri. Davranis: void bos, isaretci NULL, tamsayi 0 (OAL_SUCC/false),
 * ozel durumlar araclar/elle_govde.py'de gerekceli. */
#ifndef __HMAC_BTCOEX_C__
#define __HMAC_BTCOEX_C__
#ifdef __cplusplus
    #if __cplusplus
    #endif
#endif
#ifdef _PRE_WLAN_FEATURE_BTCOEX
#include "hmac_vap.h"
#include "hmac_ext_if.h"
#include "mac_data.h"
#include "hmac_resource.h"
#include "hmac_btcoex.h"
#include "hmac_fsm.h"
#include "hmac_mgmt_sta.h"
#undef  THIS_FILE_ID
#define THIS_FILE_ID OAM_FILE_ID_HMAC_BTCOEX_C
#endif /* #ifdef _PRE_WLAN_FEATURE_COEXIST_BT */
#ifdef __cplusplus
    #if __cplusplus
    #endif
#endif
#endif /* end of __HMAC_BTCOEX_C__ */

/* ---- kk stub tanimlari ---- */
oal_void hmac_btcoex_arp_fail_delba_process(oal_netbuf_stru *pst_netbuf, mac_vap_stru *pst_mac_vap)
{
    /* bos */
}

oal_uint32 hmac_btcoex_check_by_ba_size(hmac_user_stru *pst_hmac_user)
{
    return 0;
}

oal_uint32 hmac_btcoex_check_exception_in_list(hmac_vap_stru *pst_hmac_vap, oal_uint8 *auc_addr)
{
    return 0;
}

oal_void hmac_btcoex_check_rx_same_baw_start_from_addba_req(hmac_vap_stru *pst_hmac_vap, hmac_user_stru *pst_hmac_user, mac_ieee80211_frame_stru *pst_frame_hdr, oal_uint8 *puc_action, oal_uint32 frame_body_len)
{
    /* bos */
}

oal_uint32 hmac_btcoex_rx_delba_trigger(mac_vap_stru *pst_mac_vap, oal_uint8 uc_len, oal_uint8 *puc_param)
{
    return 0;
}

oal_uint32 hmac_config_print_btcoex_status(mac_vap_stru *pst_mac_vap, oal_uint16 us_len, oal_uint8 *puc_param)
{
    return 0;
}

