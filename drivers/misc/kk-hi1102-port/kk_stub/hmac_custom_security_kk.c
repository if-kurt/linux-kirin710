// SPDX-License-Identifier: GPL-2.0
/* OTOMATIK (araclar/stub_uret.py, G43): wifi/hmac/hmac_custom_security.c dislandi; bu dosya yalniz baglama icin
 * stub tanimlar. Asagida orijinalin onislemci yonergeleri (include/if/define) aynen,
 * sonra stub govdeleri. Davranis: void bos, isaretci NULL, tamsayi 0 (OAL_SUCC/false),
 * ozel durumlar araclar/elle_govde.py'de gerekceli. */
#ifdef _PRE_WLAN_FEATURE_CUSTOM_SECURITY
#ifdef __cplusplus
#if __cplusplus
#endif
#endif
#include "hmac_main.h"
#include "hmac_custom_security.h"
#include "mac_vap.h"
#include "hmac_vap.h"
#include "mac_resource.h"
#include "hmac_user.h"
#include "hmac_mgmt_ap.h"
#undef  THIS_FILE_ID
#define THIS_FILE_ID OAM_FILE_ID_HMAC_CUSTOM_SECURITY_C
#define BROADCAST_ISOLATION(_a)   ((_a) & CS_ISOLATION_MODE_BROADCAST)
#define MULTICAST_ISOLATION(_a)   ((_a) & CS_ISOLATION_MODE_MULTICAST)
#define UNICAST_ISOLATION(_a)     ((_a) & CS_ISOLATION_MODE_UNICAST)
#if 0
#endif
#ifdef __cplusplus
    #if __cplusplus
    #endif
#endif
#endif	/* #ifdef _PRE_WLAN_FEATURE_CUSTOM_SECURITY */

/* ---- kk stub tanimlari ---- */
oal_uint32 hmac_autoblacklist_enable(mac_vap_stru *pst_mac_vap, oal_uint8 uc_enabled)
{
    return 0;
}

oal_uint32 hmac_autoblacklist_set_aging(mac_vap_stru *pst_mac_vap, oal_uint32 ul_aging_time)
{
    return 0;
}

oal_uint32 hmac_autoblacklist_set_reset_time(mac_vap_stru *pst_mac_vap, oal_uint32 ul_reset_time)
{
    return 0;
}

oal_uint32 hmac_autoblacklist_set_threshold(mac_vap_stru *pst_mac_vap, oal_uint32 ul_threshold)
{
    return 0;
}

oal_uint32 hmac_blacklist_add(mac_vap_stru *pst_mac_vap, oal_uint8 *puc_mac_addr, oal_uint32 ul_aging_time)
{
    return 0;
}

oal_uint32 hmac_blacklist_add_only(mac_vap_stru *pst_mac_vap, oal_uint8 *puc_mac_addr, oal_uint32 ul_aging_time)
{
    return 0;
}

oal_uint32 hmac_blacklist_del(mac_vap_stru *pst_mac_vap, oal_uint8 *puc_mac_addr)
{
    return 0;
}

oal_uint32 hmac_blacklist_set_mode(mac_vap_stru *pst_mac_vap, oal_uint8 uc_mode)
{
    return 0;
}

oal_uint32 hmac_isolation_clear_counter(mac_vap_stru *pst_mac_vap)
{
    return 0;
}

cs_isolation_forward_enum hmac_isolation_filter(mac_vap_stru *pst_mac_vap, oal_uint8 *puc_mac_addr)
{
    return 0;
}

oal_uint32 hmac_isolation_set_forward(mac_vap_stru *pst_mac_vap, oal_uint8 uc_forward)
{
    return 0;
}

oal_uint32 hmac_isolation_set_mode(mac_vap_stru *pst_mac_vap, oal_uint8 uc_mode)
{
    return 0;
}

oal_uint32 hmac_isolation_set_type(mac_vap_stru *pst_mac_vap, oal_uint8 uc_type)
{
    return 0;
}

oal_void hmac_show_blacklist_info(mac_vap_stru *pst_mac_vap)
{
    /* bos */
}

oal_void hmac_show_isolation_info(mac_vap_stru *pst_mac_vap)
{
    /* bos */
}

