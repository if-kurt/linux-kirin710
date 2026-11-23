// SPDX-License-Identifier: GPL-2.0
/* OTOMATIK (araclar/stub_uret.py, G43): wifi/hmac/hmac_edca_opt.c dislandi; bu dosya yalniz baglama icin
 * stub tanimlar. Asagida orijinalin onislemci yonergeleri (include/if/define) aynen,
 * sonra stub govdeleri. Davranis: void bos, isaretci NULL, tamsayi 0 (OAL_SUCC/false),
 * ozel durumlar araclar/elle_govde.py'de gerekceli. */
#ifdef __cplusplus
#if __cplusplus
#endif
#endif
#ifdef _PRE_WLAN_FEATURE_EDCA_OPT_AP
#include "hmac_edca_opt.h"
#include "hmac_vap.h"
#include "oam_wdk.h"
#undef  THIS_FILE_ID
#define THIS_FILE_ID OAM_FILE_ID_HMAC_EDCA_OPT_C
#define HMAC_EDCA_OPT_ADJ_STEP      2
#define WLAN_EDCA_OPT_MOD(X, Y, a) (((X)*(WLAN_EDCA_OPT_MAX_WEIGHT_STA - a) + (Y)*(a))/WLAN_EDCA_OPT_MAX_WEIGHT_STA);
#if 0 //??????????????????????????-wanran
#endif
#if 0 //??????????????????????????-wanran
#endif
#if 0
#endif
#if 0//????????????????????????-wanran
#endif
#if 0 //????????????????????????-wanran
#endif
#if 0//????????????????????????-wanran
#endif
#endif   /* end of _PRE_WLAN_FEATURE_EDCA_OPT_AP */
#ifdef __cplusplus
    #if __cplusplus
    #endif
#endif

/* ---- kk stub tanimlari ---- */
oal_void hmac_edca_opt_rx_pkts_stat(oal_uint16 us_assoc_id,oal_uint8 uc_tidno, mac_ip_header_stru *pst_ip)
{
    /* bos */
}

oal_uint32 hmac_edca_opt_timeout_fn(oal_void *p_arg)
{
    return 0;
}

oal_void hmac_edca_opt_tx_pkts_stat(mac_tx_ctl_stru *pst_tx_ctl,oal_uint8 uc_tidno, mac_ip_header_stru *pst_ip)
{
    /* bos */
}

