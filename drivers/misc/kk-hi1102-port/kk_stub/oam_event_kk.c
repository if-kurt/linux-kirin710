// SPDX-License-Identifier: GPL-2.0
/* OTOMATIK (araclar/stub_uret.py, G43): platform/oam/oam_event.c dislandi; bu dosya yalniz baglama icin
 * stub tanimlar. Asagida orijinalin onislemci yonergeleri (include/if/define) aynen,
 * sonra stub govdeleri. Davranis: void bos, isaretci NULL, tamsayi 0 (OAL_SUCC/false),
 * ozel durumlar araclar/elle_govde.py'de gerekceli. */
#ifdef __cplusplus
#if __cplusplus
#endif
#endif
#include "oam_event.h"
#include "oam_main.h"
#include "oam_ext_if.h"
#undef THIS_FILE_ID
#define THIS_FILE_ID   OAM_FILE_ID_OAM_EVENT_C
#if 0
#endif
#if 0
#endif
#if 0
#endif
#if 0
#endif
#ifdef _PRE_WIFI_DMT
#endif
#if 0
#endif
#if (_PRE_PRODUCT_ID == _PRE_PRODUCT_ID_HI1151)
#else
#endif
#if 0
#endif
#if ((_PRE_OS_VERSION_RAW != _PRE_OS_VERSION) && (_PRE_OS_VERSION_WIN32_RAW != _PRE_OS_VERSION))
#if defined(_PRE_PRODUCT_ID_HI110X_HOST)
#else
#endif
#endif
#if 0
#endif
#if defined(_PRE_PRODUCT_ID_HI110X_HOST)
#else
#endif
#if (_PRE_OS_VERSION_RAW == _PRE_OS_VERSION)
#endif
#if defined(_PRE_PRODUCT_ID_HI110X_HOST)
#else
#endif
#if defined(_PRE_PRODUCT_ID_HI110X_HOST)
#else
#endif
#if defined(_PRE_PRODUCT_ID_HI110X_HOST)
#else
#endif
#if ((_PRE_OS_VERSION_RAW != _PRE_OS_VERSION) && (_PRE_OS_VERSION_WIN32_RAW != _PRE_OS_VERSION))
#if defined(_PRE_PRODUCT_ID_HI110X_HOST)
#else
#endif
#endif
#if 0
#if defined(_PRE_PRODUCT_ID_HI110X_HOST)
#else
#endif
#endif
#if defined(_PRE_PRODUCT_ID_HI110X_HOST)
#else
#endif
#if defined(_PRE_PRODUCT_ID_HI110X_HOST)
#else
#endif
#if 0
#endif
#if 0
#endif
#if 0
#endif
#if 0
#endif
#if 0
#endif
#ifdef __cplusplus
    #if __cplusplus
    #endif
#endif

/* ---- kk stub tanimlari ---- */
oal_uint32 oam_event_init(oal_void)
{
    return 0;
}

oal_uint32 oam_event_report( oal_uint8 *puc_mac_hdr_addr, oal_uint8 uc_vap_id, oam_module_id_enum_uint16 en_mod, oam_event_type_enum_uint16 en_event_type, oal_uint8 *puc_string)
{
    return 0;
}

oal_uint32 oam_event_set_switch( oal_uint8 uc_vap_id, oal_switch_enum_uint8 en_switch_type)
{
    return 0;
}
EXPORT_SYMBOL_GPL(oam_event_set_switch);

oal_uint32 oam_ota_report( oal_uint8 *puc_param_one_addr, oal_uint16 us_param_one_len, oal_uint8 *puc_param_two_addr, oal_uint16 us_param_two_len, oam_ota_type_enum_uint8 en_ota_type)
{
    return 0;
}
EXPORT_SYMBOL_GPL(oam_ota_report);

oal_uint32 oam_ota_set_beacon_switch( oal_uint8 uc_vap_id, oam_sdt_print_beacon_rxdscr_type_enum_uint8 en_switch_type)
{
    return 0;
}
EXPORT_SYMBOL_GPL(oam_ota_set_beacon_switch);

oal_uint32 oam_ota_set_rx_dscr_switch( oal_uint8 uc_vap_id, oal_switch_enum_uint8 en_switch_type)
{
    return 0;
}
EXPORT_SYMBOL_GPL(oam_ota_set_rx_dscr_switch);

oal_uint32 oam_report_80211_frame( oal_uint8 *puc_user_macaddr, oal_uint8 *puc_mac_hdr_addr, oal_uint8 uc_mac_hdr_len, oal_uint8 *puc_mac_body_addr, oal_uint16 us_mac_frame_len, oam_ota_frame_direction_type_enum_uint8 en_frame_direction)
{
    return 0;
}
EXPORT_SYMBOL_GPL(oam_report_80211_frame);

oal_uint32 oam_report_80211_mcast_get_switch( oam_ota_frame_direction_type_enum_uint8 en_mcast_direction, oam_user_track_frame_type_enum_uint8 en_frame_type, oal_switch_enum_uint8 *pen_frame_switch, oal_switch_enum_uint8 *pen_cb_switch, oal_switch_enum_uint8 *pen_dscr_switch)
{
    *pen_frame_switch = OAL_SWITCH_OFF;
    *pen_cb_switch = OAL_SWITCH_OFF;
    *pen_dscr_switch = OAL_SWITCH_OFF;
    return OAL_SUCC;
}
EXPORT_SYMBOL_GPL(oam_report_80211_mcast_get_switch);

oal_uint32 oam_report_80211_mcast_set_switch( oam_ota_frame_direction_type_enum_uint8 en_mcast_direction, oam_user_track_frame_type_enum_uint8 en_frame_type, oal_switch_enum_uint8 en_frame_switch, oal_switch_enum_uint8 en_cb_switch, oal_switch_enum_uint8 en_dscr_switch)
{
    return 0;
}
EXPORT_SYMBOL_GPL(oam_report_80211_mcast_set_switch);

oal_uint32 oam_report_80211_probe_get_switch( oam_ota_frame_direction_type_enum_uint8 en_probe_direction, oal_switch_enum_uint8 *pen_frame_switch, oal_switch_enum_uint8 *pen_cb_switch, oal_switch_enum_uint8 *pen_dscr_switch)
{
    *pen_frame_switch = OAL_SWITCH_OFF;
    *pen_cb_switch = OAL_SWITCH_OFF;
    *pen_dscr_switch = OAL_SWITCH_OFF;
    return OAL_SUCC;
}
EXPORT_SYMBOL_GPL(oam_report_80211_probe_get_switch);

oal_uint32 oam_report_80211_probe_set_switch( oam_ota_frame_direction_type_enum_uint8 en_probe_direction, oal_switch_enum_uint8 en_frame_switch, oal_switch_enum_uint8 en_cb_switch, oal_switch_enum_uint8 en_dscr_switch)
{
    return 0;
}
EXPORT_SYMBOL_GPL(oam_report_80211_probe_set_switch);

oal_uint32 oam_report_80211_ucast_get_switch( oam_ota_frame_direction_type_enum_uint8 en_ucast_direction, oam_user_track_frame_type_enum_uint8 en_frame_type, oal_switch_enum_uint8 *pen_frame_switch, oal_switch_enum_uint8 *pen_cb_switch, oal_switch_enum_uint8 *pen_dscr_switch, oal_uint16 us_user_idx)
{
    *pen_frame_switch = OAL_SWITCH_OFF;
    *pen_cb_switch = OAL_SWITCH_OFF;
    *pen_dscr_switch = OAL_SWITCH_OFF;
    return OAL_SUCC;
}
EXPORT_SYMBOL_GPL(oam_report_80211_ucast_get_switch);

oal_uint32 oam_report_80211_ucast_set_switch( oam_ota_frame_direction_type_enum_uint8 en_ucast_direction, oam_user_track_frame_type_enum_uint8 en_frame_type, oal_switch_enum_uint8 en_frame_switch, oal_switch_enum_uint8 en_cb_switch, oal_switch_enum_uint8 en_dscr_switch, oal_uint16 us_user_idx)
{
    return 0;
}
EXPORT_SYMBOL_GPL(oam_report_80211_ucast_set_switch);

oal_uint32 oam_report_beacon( oal_uint8 *puc_beacon_hdr_addr, oal_uint8 uc_beacon_hdr_len, oal_uint8 *puc_beacon_body_addr, oal_uint16 us_beacon_len, oam_ota_frame_direction_type_enum_uint8 en_beacon_direction)
{
    return 0;
}
EXPORT_SYMBOL_GPL(oam_report_beacon);

oal_uint32 oam_report_dft_params(oal_uint8 *puc_user_mac_addr, oal_uint8 *puc_param, oal_uint16 us_param_len, oam_ota_type_enum_uint8 en_type)
{
    return 0;
}
EXPORT_SYMBOL_GPL(oam_report_dft_params);

oal_switch_enum_uint8 oam_report_dhcp_arp_get_switch(oal_void)
{
    return 0;
}
EXPORT_SYMBOL_GPL(oam_report_dhcp_arp_get_switch);

oal_uint32 oam_report_dhcp_arp_set_switch(oal_switch_enum_uint8 en_switch)
{
    return 0;
}
EXPORT_SYMBOL_GPL(oam_report_dhcp_arp_set_switch);

oal_uint32 oam_report_eth_frame(oal_uint8 *puc_user_mac_addr, oal_uint8 *puc_eth_frame_hdr_addr, oal_uint16 us_eth_frame_len, oam_ota_frame_direction_type_enum_uint8 en_eth_frame_direction)
{
    return 0;
}
EXPORT_SYMBOL_GPL(oam_report_eth_frame);

oal_uint32 oam_report_eth_frame_get_switch( oal_uint16 us_user_idx, oam_ota_frame_direction_type_enum_uint8 en_eth_direction, oal_switch_enum_uint8 *pen_eth_switch)
{
    if (pen_eth_switch)
        *pen_eth_switch = OAL_SWITCH_OFF;
    return OAL_SUCC;
}
EXPORT_SYMBOL_GPL(oam_report_eth_frame_get_switch);

oal_uint32 oam_report_eth_frame_set_switch(oal_uint16 us_user_idx, oal_switch_enum_uint8 en_switch, oam_ota_frame_direction_type_enum_uint8 en_eth_direction)
{
    return 0;
}
EXPORT_SYMBOL_GPL(oam_report_eth_frame_set_switch);

oal_uint32 oam_report_netbuf_cb(oal_uint8 *puc_user_mac_addr, oal_uint8 *puc_netbuf_cb, oam_ota_type_enum_uint8 en_ota_type)
{
    return 0;
}
EXPORT_SYMBOL_GPL(oam_report_netbuf_cb);

oal_uint32 oam_report_set_all_switch(oal_switch_enum_uint8 en_switch)
{
    return 0;
}
EXPORT_SYMBOL_GPL(oam_report_set_all_switch);

