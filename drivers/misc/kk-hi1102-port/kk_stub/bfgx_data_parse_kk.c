// SPDX-License-Identifier: GPL-2.0
/* OTOMATIK (araclar/stub_uret.py, G43): bfgx/bfgx_data_parse.c dislandi; bu dosya yalniz baglama icin
 * stub tanimlar. Asagida orijinalin onislemci yonergeleri (include/if/define) aynen,
 * sonra stub govdeleri. Davranis: void bos, isaretci NULL, tamsayi 0 (OAL_SUCC/false),
 * ozel durumlar araclar/elle_govde.py'de gerekceli. */
#include <linux/skbuff.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/tty.h>
#include "hw_bfg_ps.h"
#include "plat_type.h"
#include "plat_debug.h"
#include "plat_uart.h"
#include "bfgx_low_power.h"
#include "bfgx_exception_rst.h"
#include "bfgx_user_ctrl.h"
#include "plat_pm.h"
#include "oal_ext_if.h"
#ifdef HI110X_HAL_MEMDUMP_ENABLE
#endif
#ifdef HI110X_HAL_MEMDUMP_ENABLE
#endif
#ifdef HI110X_HAL_MEMDUMP_ENABLE
#else
#endif
#ifdef HI110X_HAL_MEMDUMP_ENABLE
#else
#endif
#ifdef PLATFORM_DEBUG_ENABLE
#endif

/* ---- kk stub tanimlari ---- */
int32 ps_tx_sys_cmd(struct ps_core_s *ps_core_d, uint8 type, uint8 content)
{
    return 0;
}

int32 ps_write_tty(struct ps_core_s *ps_core_d,const uint8 *data, int32 count)
{
    return 0;
}

void reset_uart_rx_buf(void)
{
    /* bos */
}

int32 wifi_choose_bfgn_channel_send_log2sdt(uint8* data, uint16 len)
{
    return 0;
}

