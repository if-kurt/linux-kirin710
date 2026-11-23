// SPDX-License-Identifier: GPL-2.0
/* OTOMATIK (araclar/stub_uret.py, G43): bfgx/bfgx_low_power.c dislandi; bu dosya yalniz baglama icin
 * stub tanimlar. Asagida orijinalin onislemci yonergeleri (include/if/define) aynen,
 * sonra stub govdeleri. Davranis: void bos, isaretci NULL, tamsayi 0 (OAL_SUCC/false),
 * ozel durumlar araclar/elle_govde.py'de gerekceli. */
#include <linux/delay.h>
#include "oal_ext_if.h"
#include "hw_bfg_ps.h"
#include "plat_debug.h"
#include "plat_pm.h"
#include "bfgx_exception_rst.h"

/* ---- kk stub tanimlari ---- */
void bfg_timer_expire(uint64 data)
{
    /* bos */
}

void host_allow_bfg_sleep(struct ps_core_s *ps_core_d)
{
    /* bos */
}

