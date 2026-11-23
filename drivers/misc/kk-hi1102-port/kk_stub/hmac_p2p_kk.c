// SPDX-License-Identifier: GPL-2.0
/* OTOMATIK (araclar/stub_uret.py, G43): wifi/hmac/hmac_p2p.c dislandi; bu dosya yalniz baglama icin
 * stub tanimlar. Asagida orijinalin onislemci yonergeleri (include/if/define) aynen,
 * sonra stub govdeleri. Davranis: void bos, isaretci NULL, tamsayi 0 (OAL_SUCC/false),
 * ozel durumlar araclar/elle_govde.py'de gerekceli. */
#ifdef __cplusplus
#if __cplusplus
#endif
#endif
#ifdef _PRE_WLAN_FEATURE_P2P
#include "oal_util.h"
#include "hmac_ext_if.h"
#include "hmac_resource.h"
#include "hmac_device.h"
#include "hmac_vap.h"
#include "hmac_rx_filter.h"
#include "hmac_fsm.h"
#include "hmac_mgmt_bss_comm.h"
#include "mac_frame.h"
#include "hmac_p2p.h"
#undef  THIS_FILE_ID
#define THIS_FILE_ID OAM_FILE_ID_HMAC_P2P_C
#endif /* _PRE_WLAN_FEATURE_P2P */
#ifdef __cplusplus
    #if __cplusplus
    #endif
#endif

/* ---- kk stub tanimlari ---- */
oal_uint32 hmac_add_p2p_cl_vap(mac_vap_stru *pst_vap, oal_uint16 us_len, oal_uint8 *puc_param)
{
    return 0;
}

oal_uint32 hmac_check_p2p_vap_num(mac_device_stru *pst_mac_device, wlan_p2p_mode_enum_uint8 en_p2p_mode)
{
    return 0;
}

oal_void hmac_clr_p2p_status(oal_uint32 *pul_p2p_status, hmac_cfgp2p_status_enum_uint32 en_status)
{
    /* bos */
}

oal_uint32 hmac_del_p2p_cl_vap(mac_vap_stru *pst_vap, oal_uint16 us_len, oal_uint8 *puc_param)
{
    return 0;
}

oal_uint8 hmac_get_p2p_status(oal_uint32 ul_p2p_status, hmac_cfgp2p_status_enum_uint32 en_status)
{
    return 0;
}

oal_uint32 hmac_p2p_check_can_enter_state(mac_vap_stru *pst_mac_vap, hmac_fsm_input_type_enum_uint8 en_input_req)
{
    return 0;
}

oal_uint32 hmac_p2p_send_listen_expired_to_device(hmac_vap_stru *pst_hmac_vap)
{
    return 0;
}

oal_uint32 hmac_p2p_send_listen_expired_to_host(hmac_vap_stru *pst_hmac_vap)
{
    return 0;
}

oal_void hmac_set_p2p_status(oal_uint32 *pul_p2p_status, hmac_cfgp2p_status_enum_uint32 en_status)
{
    /* bos */
}

