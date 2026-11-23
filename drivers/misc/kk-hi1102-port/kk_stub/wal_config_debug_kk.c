// SPDX-License-Identifier: GPL-2.0
/* OTOMATIK (araclar/stub_uret.py, G43): wifi/wal/wal_config_debug.c dislandi; bu dosya yalniz baglama icin
 * stub tanimlar. Asagida orijinalin onislemci yonergeleri (include/if/define) aynen,
 * sonra stub govdeleri. Davranis: void bos, isaretci NULL, tamsayi 0 (OAL_SUCC/false),
 * ozel durumlar araclar/elle_govde.py'de gerekceli. */
#ifdef __cplusplus
#if __cplusplus
#endif
#endif
#ifdef _PRE_WLAN_CFGID_DEBUG
#include "oal_types.h"
#include "oal_ext_if.h"
#include "oam_ext_if.h"
#include "wlan_types.h"
#include "mac_device.h"
#include "mac_vap.h"
#include "mac_resource.h"
#include "hmac_resource.h"
#include "hmac_device.h"
#include "hmac_scan.h"
#include "hmac_ext_if.h"
#include "hmac_config.h"
#include "wal_ext_if.h"
#include "wal_main.h"
#include "wal_config.h"
#include "wal_linux_bridge.h"
#if defined(_PRE_PRODUCT_ID_HI110X_HOST)
#include "hmac_cali_mgmt.h"
#endif
#ifdef _PRE_WLAN_CHIP_TEST
#include "hmac_test_main.h"
#endif
#ifdef _PRE_WLAN_DFT_REG
#include "hal_witp_debug.h"
#endif
#ifdef _PRE_WLAN_FEATURE_MCAST
#include "hmac_m2u.h"
#endif
#ifdef _PRE_WLAN_FEATURE_PROXY_ARP
#include "hmac_proxy_arp.h"
#endif
#ifdef _PRE_WLAN_FEATURE_WAPI
#include "hmac_wapi.h"
#endif
#if (_PRE_MULTI_CORE_MODE_OFFLOAD_DMAC == _PRE_MULTI_CORE_MODE)&&(_PRE_OS_VERSION_LINUX == _PRE_OS_VERSION)
#include "plat_pm_wlan.h"
#endif
#ifdef _PRE_WLAN_FEATURE_HILINK
#include "hmac_fbt_main.h"
#endif
#ifdef _PRE_WLAN_FEATURE_WMMAC
#include "hmac_wmmac.h"
#endif
#undef  THIS_FILE_ID
#define THIS_FILE_ID OAM_FILE_ID_WAL_CONFIG_DEBUG_C
#ifdef _PRE_WLAN_FEATURE_OFFLOAD_FLOWCTL
#endif
#ifdef _PRE_WLAN_FEATURE_EQUIPMENT_TEST
#endif
#ifdef _PRE_DEBUG_MODE_USER_TRACK
#endif
#ifdef _PRE_WLAN_FEATURE_TXOPPS
#endif
#if (_PRE_MULTI_CORE_MODE_OFFLOAD_DMAC == _PRE_MULTI_CORE_MODE)
#ifdef _PRE_DEBUG_MODE
#endif //#ifdef _PRE_DEBUG_MODE
#if 0
#endif
#endif
#ifdef _PRE_WLAN_RF_110X_CALI_DPD
#endif
#ifdef _PRE_WLAN_CHIP_TEST
#endif
#ifdef _PRE_WLAN_FEATURE_WMMAC
#endif
#ifdef _PRE_WLAN_FEATURE_11K
#endif
#ifdef _PRE_DEBUG_MODE
#endif //#ifdef _PRE_DEBUG_MODE
#ifdef _PRE_DEBUG_MODE
#endif //#ifdef _PRE_DEBUG_MODE
#ifdef _PRE_DEBUG_MODE
#endif //#ifdef _PRE_DEBUG_MODE
#ifdef _PRE_DEBUG_MODE
#endif //#ifdef _PRE_DEBUG_MODE
#ifdef _PRE_DEBUG_MODE
#endif //#ifdef _PRE_DEBUG_MODE
#ifdef _PRE_PROFILING_MODE
#endif
#ifdef _PRE_DEBUG_MODE
#endif //#ifdef _PRE_DEBUG_MODE
#ifdef _PRE_WLAN_FEATURE_DFR
#ifdef _PRE_DEBUG_MODE
#endif
#endif
#ifdef _PRE_WLAN_FEATURE_UAPSD
#endif
#ifdef _PRE_WLAN_DFT_STAT
#ifdef _PRE_DEBUG_MODE
#endif //#ifdef _PRE_DEBUG_MODE
#ifdef _PRE_DEBUG_MODE
#endif //#ifdef _PRE_DEBUG_MODE
#ifdef _PRE_DEBUG_MODE
#endif //#ifdef _PRE_DEBUG_MODE
#ifdef _PRE_DEBUG_MODE
#endif //#ifdef _PRE_DEBUG_MODE
#endif
#ifdef _PRE_WLAN_FEATURE_11D
#endif
#if (_PRE_MULTI_CORE_MODE_OFFLOAD_DMAC == _PRE_MULTI_CORE_MODE)
#endif
#ifdef _PRE_DEBUG_MODE
#endif //#ifdef _PRE_DEBUG_MODE
#ifdef _PRE_DEBUG_MODE
#endif //#ifdef _PRE_DEBUG_MODE
#if defined (_PRE_WLAN_CHIP_TEST) || defined (_PRE_WLAN_FEATURE_ALWAYS_TX)
#ifdef _PRE_DEBUG_MODE
#endif //#ifdef _PRE_DEBUG_MODE
#endif
#ifdef _PRE_DEBUG_MODE
#endif
#ifdef _PRE_DEBUG_MODE
#endif //#ifdef _PRE_DEBUG_MODE
#ifdef _PRE_DEBUG_MODE
#endif //#ifdef _PRE_DEBUG_MODE
#ifdef _PRE_DEBUG_MODE
#endif //#ifdef _PRE_DEBUG_MODE
#ifdef _PRE_DEBUG_MODE
#endif //#ifdef _PRE_DEBUG_MODE
#if (_PRE_MULTI_CORE_MODE_OFFLOAD_DMAC != _PRE_MULTI_CORE_MODE)
#endif
#ifdef _PRE_SUPPORT_ACS
#endif
#ifdef _PRE_WLAN_PERFORM_STAT
#endif
#ifdef _PRE_WLAN_CHIP_TEST
#ifdef _PRE_WLAN_CHIP_TEST
#endif
#ifdef _PRE_WLAN_CHIP_TEST
#else
#endif
#ifdef _PRE_WLAN_CHIP_TEST
#else
#endif
#if (_PRE_WLAN_FEATURE_PMF != _PRE_PMF_NOT_SUPPORT)
#if (_PRE_MULTI_CORE_MODE_OFFLOAD_DMAC == _PRE_MULTI_CORE_MODE)
#else
#endif
#endif
#endif /* #ifdef _PRE_WLAN_CHIP_TEST */
#ifdef _PRE_DEBUG_MODE
#endif //#ifdef _PRE_DEBUG_MODE
#ifdef _PRE_DEBUG_MODE
#endif //#ifdef _PRE_DEBUG_MODE
#ifdef _PRE_DEBUG_MODE
#endif
#ifdef _PRE_WLAN_FEATURE_DAQ
#endif
#ifdef _PRE_WLAN_FEATURE_SMPS
#ifdef _PRE_DEBUG_MODE
#endif
#endif
#ifdef _PRE_WLAN_FEATURE_PROXYSTA
#endif
#ifdef _PRE_WLAN_FEATURE_OPMODE_NOTIFY
#endif
#ifdef _PRE_DEBUG_MODE
#endif //#ifdef _PRE_DEBUG_MODE
#ifdef _PRE_WLAN_DFT_REG
#endif
#ifdef _PRE_DEBUG_MODE
#endif //#ifdef _PRE_DEBUG_MODE
#ifdef _PRE_WLAN_FEATURE_CUSTOM_SECURITY
#endif
#ifdef _PRE_WLAN_FEATURE_PROXY_ARP
#ifdef _PRE_DEBUG_MODE
#endif/* #ifdef _PRE_DEBUG_MODE */
#endif
#ifdef _PRE_WLAN_FEATURE_WAPI
#ifdef _PRE_WAPI_DEBUG
#endif/* #ifdef _PRE_DEBUG_MODE */
#endif /* _PRE_WLAN_FEATURE_WAPI */
#ifdef _PRE_DEBUG_MODE
#endif
#if (_PRE_MULTI_CORE_MODE_OFFLOAD_DMAC != _PRE_MULTI_CORE_MODE)
#endif
#if (_PRE_MULTI_CORE_MODE_OFFLOAD_DMAC != _PRE_MULTI_CORE_MODE)
#endif
#ifdef _PRE_WLAN_FEATURE_MCAST
            #ifdef _PRE_WLAN_FEATURE_SMP_SUPPORT
            #else
            #endif
#endif
#ifdef _PRE_WLAN_FEATURE_PM
#endif
#if (_PRE_MULTI_CORE_MODE_OFFLOAD_DMAC == _PRE_MULTI_CORE_MODE)
#ifdef _PRE_DEBUG_MODE
#endif //#ifdef _PRE_DEBUG_MODE
#endif
#ifdef _PRE_WLAN_FEATURE_STA_PM
#endif
#ifdef _PRE_DEBUG_MODE
#endif
#ifdef _PRE_WLAN_FEATURE_P2P
#ifdef _PRE_DEBUG_MODE
#endif //#ifdef _PRE_DEBUG_MODE
#endif
#ifdef _PRE_WLAN_PROFLING_MIPS
#endif
#ifdef _PRE_WLAN_FEATURE_ARP_OFFLOAD
#ifdef _PRE_DEBUG_MODE
#endif //#ifdef _PRE_DEBUG_MODE
#ifdef _PRE_DEBUG_MODE
#endif //#ifdef _PRE_DEBUG_MODE
#endif
#ifdef _PRE_WLAN_TCP_OPT
#endif
#if (_PRE_OS_VERSION_LINUX == _PRE_OS_VERSION) && (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 44)) && (_PRE_MULTI_CORE_MODE_OFFLOAD_DMAC == _PRE_MULTI_CORE_MODE)
#ifdef _PRE_WLAN_DFT_STAT
#ifdef _PRE_DEBUG_MODE
#endif //#ifdef _PRE_DEBUG_MODE
#endif
#endif
#ifdef _PRE_WLAN_FEATURE_ROAM
#endif //_PRE_WLAN_FEATURE_ROAM
#ifdef _PRE_WLAN_FEATURE_20_40_80_COEXIST
#endif /* _PRE_WLAN_FEATURE_20_40_80_COEXIST */
#if (_PRE_PRODUCT_ID == _PRE_PRODUCT_ID_HI1151)
#endif
#ifdef _PRE_WLAN_FEATURE_AUTO_FREQ
#endif
#ifdef _PRE_WLAN_FEATURE_HILINK
#endif
#ifdef _PRE_WLAN_FEATURE_TX_CLASSIFY_LAN_TO_WLAN
#endif  /* _PRE_WLAN_FEATURE_TX_CLASSIFY_LAN_TO_WLAN */
#ifdef _PRE_WLAN_FEATURE_EQUIPMENT_TEST
#endif
#ifdef _PRE_WLAN_FEATURE_EQUIPMENT_TEST
#endif
#ifdef _PRE_DEBUG_MODE_USER_TRACK
#endif
#ifdef _PRE_WLAN_FEATURE_TXOPPS
#endif
#if (_PRE_MULTI_CORE_MODE_OFFLOAD_DMAC == _PRE_MULTI_CORE_MODE)
#ifdef _PRE_DEBUG_MODE
#endif //#ifdef _PRE_DEBUG_MODE
#endif
#ifdef _PRE_DEBUG_MODE
#endif //#ifdef _PRE_DEBUG_MODE
#ifdef _PRE_WLAN_CHIP_TEST
#endif
#ifdef _PRE_WLAN_RF_110X_CALI_DPD
#endif
#ifdef _PRE_WLAN_FEATURE_WMMAC
#endif
#ifdef _PRE_WLAN_FEATURE_11K
#endif //_PRE_WLAN_FEATURE_11K
#if (_PRE_MULTI_CORE_MODE_OFFLOAD_DMAC == _PRE_MULTI_CORE_MODE)
#endif
#ifdef _PRE_DEBUG_MODE
#endif //#ifdef _PRE_DEBUG_MODE
#ifdef _PRE_DEBUG_MODE
#endif //#ifdef _PRE_DEBUG_MODE
#ifdef _PRE_DEBUG_MODE
#endif //#ifdef _PRE_DEBUG_MODE
#ifdef _PRE_DEBUG_MODE
#endif //#ifdef _PRE_DEBUG_MODE
#ifdef _PRE_DEBUG_MODE
#endif //#ifdef _PRE_DEBUG_MODE
#ifdef _PRE_DEBUG_MODE
#endif //#ifdef _PRE_DEBUG_MODE
#ifdef _PRE_WLAN_FEATURE_DFR
#ifdef _PRE_DEBUG_MODE
#endif
#endif
#ifdef _PRE_WLAN_FEATURE_UAPSD
#endif
#ifdef _PRE_WLAN_DFT_STAT
#ifdef _PRE_DEBUG_MODE
#endif //#ifdef _PRE_DEBUG_MODE
#ifdef _PRE_DEBUG_MODE
#endif //#ifdef _PRE_DEBUG_MODE
#ifdef _PRE_DEBUG_MODE
#endif //#ifdef _PRE_DEBUG_MODE
#ifdef _PRE_DEBUG_MODE
#endif //#ifdef _PRE_DEBUG_MODE
#endif
#ifdef _PRE_DEBUG_MODE
#endif //#ifdef _PRE_DEBUG_MODE
#if (_PRE_MULTI_CORE_MODE_OFFLOAD_DMAC != _PRE_MULTI_CORE_MODE)
#endif
#ifdef _PRE_SUPPORT_ACS
#endif
#ifdef _PRE_DEBUG_MODE
#endif //#ifdef _PRE_DEBUG_MODE
#ifdef _PRE_DEBUG_MODE
#endif //#ifdef _PRE_DEBUG_MODE
#if defined (_PRE_WLAN_CHIP_TEST) || defined (_PRE_WLAN_FEATURE_ALWAYS_TX)
#ifdef _PRE_DEBUG_MODE
#endif //#ifdef _PRE_DEBUG_MODE
#endif
#ifdef _PRE_DEBUG_MODE
#endif
#ifdef _PRE_DEBUG_MODE
#endif //#ifdef _PRE_DEBUG_MODE
#ifdef _PRE_DEBUG_MODE
#endif //#ifdef _PRE_DEBUG_MODE
#ifdef _PRE_DEBUG_MODE
#endif //#ifdef _PRE_DEBUG_MODE
#ifdef _PRE_WLAN_PERFORM_STAT
#endif
#ifdef _PRE_WLAN_FEATURE_OFFLOAD_FLOWCTL
#endif
#ifdef _PRE_WLAN_CHIP_TEST
#if (_PRE_WLAN_FEATURE_PMF != _PRE_PMF_NOT_SUPPORT)
#endif
#endif /* #ifdef _PRE_WLAN_CHIP_TEST */
#ifdef _PRE_DEBUG_MODE
#endif //#ifdef _PRE_DEBUG_MODE
#ifdef _PRE_DEBUG_MODE
#endif //#ifdef _PRE_DEBUG_MODE
#ifdef _PRE_DEBUG_MODE
#endif
#ifdef _PRE_WLAN_FEATURE_DAQ
#endif
#ifdef _PRE_WLAN_FEATURE_PROXYSTA
#endif
#ifdef _PRE_WLAN_FEATURE_OPMODE_NOTIFY
#endif
#ifdef _PRE_DEBUG_MODE
#endif //#ifdef _PRE_DEBUG_MODE
#ifdef _PRE_WLAN_DFT_REG
#endif
#ifdef _PRE_DEBUG_MODE
#endif
#if (_PRE_MULTI_CORE_MODE_OFFLOAD_DMAC != _PRE_MULTI_CORE_MODE)
#endif
#ifdef _PRE_WLAN_FEATURE_CUSTOM_SECURITY
#endif
#ifdef _PRE_WLAN_FEATURE_MCAST
#endif
#ifdef _PRE_WLAN_FEATURE_PROXY_ARP
#ifdef _PRE_DEBUG_MODE
#endif /* #ifdef _PRE_DEBUG_MODE */
#endif
#ifdef _PRE_WLAN_FEATURE_WAPI
#ifdef _PRE_WAPI_DEBUG
#endif /* #ifdef _PRE_DEBUG_MODE */
#endif
#ifdef _PRE_WLAN_FEATURE_SMPS
#ifdef _PRE_DEBUG_MODE
#endif
#endif
#ifdef _PRE_WLAN_FEATURE_PM
#endif
#ifdef _PRE_DEBUG_MODE
#endif
#if (_PRE_MULTI_CORE_MODE_OFFLOAD_DMAC == _PRE_MULTI_CORE_MODE)
#ifdef _PRE_DEBUG_MODE
#endif //#ifdef _PRE_DEBUG_MODE
#endif
#ifdef _PRE_WLAN_FEATURE_STA_PM
#endif
#ifdef _PRE_WLAN_FEATURE_11D
#endif
#ifdef _PRE_WLAN_FEATURE_P2P
#ifdef _PRE_DEBUG_MODE
#endif //#ifdef _PRE_DEBUG_MODE
#endif
#ifdef _PRE_WLAN_PROFLING_MIPS
#endif
#if (_PRE_MULTI_CORE_MODE_OFFLOAD_DMAC != _PRE_MULTI_CORE_MODE)
#endif
#ifdef _PRE_WLAN_FEATURE_ARP_OFFLOAD
#ifdef _PRE_DEBUG_MODE
#endif //#ifdef _PRE_DEBUG_MODE
#ifdef _PRE_DEBUG_MODE
#endif //#ifdef _PRE_DEBUG_MODE
#endif
#ifdef _PRE_WLAN_TCP_OPT
#endif
#if (_PRE_OS_VERSION_LINUX == _PRE_OS_VERSION) && (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 44)) && (_PRE_MULTI_CORE_MODE_OFFLOAD_DMAC == _PRE_MULTI_CORE_MODE)
#ifdef _PRE_WLAN_DFT_STAT
#ifdef _PRE_DEBUG_MODE
#endif //#ifdef _PRE_DEBUG_MODE
#endif
#endif
#ifdef _PRE_WLAN_FEATURE_ROAM
#endif  //_PRE_WLAN_FEATURE_ROAM
#ifdef _PRE_WLAN_FEATURE_20_40_80_COEXIST
#endif
#ifdef _PRE_WLAN_FEATURE_AUTO_FREQ
#endif
#ifdef _PRE_WLAN_FEATURE_TX_CLASSIFY_LAN_TO_WLAN
#endif
#if (_PRE_PRODUCT_ID == _PRE_PRODUCT_ID_HI1151)
#endif
#ifdef _PRE_WLAN_FEATURE_HILINK
#endif
#endif
#ifdef __cplusplus
    #if __cplusplus
    #endif
#endif

/* ---- kk stub tanimlari ---- */
const wal_wid_op_stru g_ast_board_wid_op_debug[1];

oal_uint32 wal_config_get_debug_wid_arrysize(oal_void)
{
    return 0;
}

