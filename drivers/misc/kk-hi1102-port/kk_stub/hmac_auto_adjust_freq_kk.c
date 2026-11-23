// SPDX-License-Identifier: GPL-2.0
/* OTOMATIK (araclar/stub_uret.py, G43): wifi/hmac/hmac_auto_adjust_freq.c dislandi; bu dosya yalniz baglama icin
 * stub tanimlar. Asagida orijinalin onislemci yonergeleri (include/if/define) aynen,
 * sonra stub govdeleri. Davranis: void bos, isaretci NULL, tamsayi 0 (OAL_SUCC/false),
 * ozel durumlar araclar/elle_govde.py'de gerekceli. */
#ifdef __cplusplus
#if __cplusplus
#endif
#endif
#ifdef _PRE_WLAN_FEATURE_AUTO_FREQ
#include "oal_net.h"
#include "oal_types.h"
#include "oam_ext_if.h"
#include  "mac_vap.h"
#include  "mac_resource.h"
#include  "hmac_vap.h"
#include  "hmac_auto_adjust_freq.h"
#include  "hmac_ext_if.h"
#include  "hmac_blockack.h"
#if (_PRE_MULTI_CORE_MODE_OFFLOAD_DMAC == _PRE_MULTI_CORE_MODE)&&(_PRE_OS_VERSION_LINUX == _PRE_OS_VERSION)
#include "plat_pm_wlan.h"
#endif
#if (_PRE_OS_VERSION_LINUX == _PRE_OS_VERSION)
#include <linux/pm_qos.h>
#endif
#undef  THIS_FILE_ID
#define THIS_FILE_ID OAM_FILE_ID_MAC_AUTO_ADJUST_FREQ_C
#define HMAC_AUTO_FREQ_NORMAL_CPU   0
#define HMAC_AUTO_FREQ_BUSY_CPU     1
#define HMAC_AUTO_FREQ_CPU_NUM      4
#ifndef WIN32
#else
#endif
#ifdef WIN32
#define mutex_init(mux)
#define mutex_lock(mux)
#define mutex_unlock(mux)
#define spin_lock_init(mux)
#define mutex_destroy(mux)
#define spin_unlock_bh(mux)
#endif
#if (_PRE_OS_VERSION_LINUX == _PRE_OS_VERSION)
#if 0
#else
#endif
#endif
#ifdef CONFIG_NR_CPUS
#if (CONFIG_NR_CPUS > HMAC_AUTO_FREQ_CPU_NUM)
#endif  /* CONFIG_NR_CPUS > HMAC_AUTO_FREQ_CPU_NUM */
#endif  /* CONFIG_NR_CPUS */
#if 0
#endif
#if (_PRE_OS_VERSION_LINUX == _PRE_OS_VERSION)
#endif
#if (_PRE_OS_VERSION_LINUX == _PRE_OS_VERSION)
#if 0
#else
#endif
#endif
#if (_PRE_OS_VERSION_LINUX == _PRE_OS_VERSION)
#if 1
#endif
#endif
#if 0
#endif
#if 0
#endif
#ifdef _PRE_WLAN_TCP_OPT
#endif  /* _PRE_WLAN_TCP_OPT */
#endif
#ifdef __cplusplus
    #if __cplusplus
    #endif
#endif

/* ---- kk stub tanimlari ---- */
device_speed_freq_level_stru g_device_speed_freq_level[4];

freq_lock_control_stru g_freq_lock_control;

host_speed_freq_level_stru g_host_speed_freq_level[4];

oal_bool_enum_uint8 hmac_set_auto_freq_debug_print(oal_bool_enum_uint8 en_debug_print)
{
    return 0;
}

oal_uint8 hmac_set_auto_freq_mod(oal_uint8 uc_freq_enable)
{
    return 0;
}

oal_int32 hmac_set_auto_freq_process_func(oal_void)
{
    return 0;
}

oal_bool_enum_uint8 hmac_set_cpu_freq_raw(oal_uint8 uc_freq_type, oal_uint32 ul_freq_value)
{
    return 0;
}

oal_bool_enum_uint8 hmac_set_ddr_freq_raw(oal_uint8 uc_freq_type, oal_uint32 ul_freq_value)
{
    return 0;
}

oal_void hmac_wifi_auto_freq_ctrl_deinit(void)
{
    /* bos */
}

oal_void hmac_wifi_auto_freq_ctrl_init(void)
{
    /* bos */
}

