// SPDX-License-Identifier: GPL-2.0
/* OTOMATIK (araclar/stub_uret.py, G43): wifi/hmac/hmac_dfx.c dislandi; bu dosya yalniz baglama icin
 * stub tanimlar. Asagida orijinalin onislemci yonergeleri (include/if/define) aynen,
 * sonra stub govdeleri. Davranis: void bos, isaretci NULL, tamsayi 0 (OAL_SUCC/false),
 * ozel durumlar araclar/elle_govde.py'de gerekceli. */
#ifdef __cplusplus
#if __cplusplus
#endif
#endif
#include "oal_ext_if.h"
#include "oam_ext_if.h"
#include "wlan_spec.h"
#include "hmac_dfx.h"
#include "hmac_ext_if.h"
#undef  THIS_FILE_ID
#define THIS_FILE_ID OAM_FILE_ID_HMAC_DFX_C
#ifdef _PRE_WLAN_FEATURE_DFR
#endif
#if defined(_PRE_PRODUCT_ID_HI110X_HOST)
#if (_PRE_MULTI_CORE_MODE_OFFLOAD_DMAC == _PRE_MULTI_CORE_MODE)
#else
#endif
#else
#endif
#if ((defined(_PRE_PRODUCT_ID_HI110X_DEV)) || (defined(_PRE_PRODUCT_ID_HI110X_HOST)))
#else
#ifdef _PRE_WLAN_FEATURE_PROXYSTA
#endif
#endif
#ifdef _PRE_WLAN_FEATURE_DFR
#endif
#ifdef __cplusplus
    #if __cplusplus
    #endif
#endif

/* ---- kk stub tanimlari ---- */
hmac_dfr_info_stru g_st_dfr_info;

oal_uint32 g_ul_wlan_vap_max_num_per_device = 4 + 1;

oal_uint16 g_us_wlan_assoc_user_max_num = 32;

oal_uint32 hmac_dfx_exit(void)
{
    return 0;
}

oal_uint32 hmac_dfx_init(void)
{
    /* 4.9 hmac_dfx.c:244-248 -> hmac_custom_init(OAL_FALSE), HI110X_HOST dali (:77-91) */
    g_us_wlan_assoc_user_max_num     = 8;
    g_ul_wlan_vap_max_num_per_device = WLAN_SERVICE_VAP_MAX_NUM_PER_DEVICE + WLAN_CONFIG_VAP_MAX_NUM_PER_DEVICE;
    return OAL_SUCC;
}

