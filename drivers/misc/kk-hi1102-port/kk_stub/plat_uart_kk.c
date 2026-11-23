// SPDX-License-Identifier: GPL-2.0
/* OTOMATIK (araclar/stub_uret.py, G43): platform/driver/plat_uart.c dislandi; bu dosya yalniz baglama icin
 * stub tanimlar. Asagida orijinalin onislemci yonergeleri (include/if/define) aynen,
 * sonra stub govdeleri. Davranis: void bos, isaretci NULL, tamsayi 0 (OAL_SUCC/false),
 * ozel durumlar araclar/elle_govde.py'de gerekceli. */
#include <linux/tty.h>
#include <linux/delay.h>
#include "plat_debug.h"
#include "plat_uart.h"
#include "hw_bfg_ps.h"
#include "bfgx_user_ctrl.h"
#include <linux/time.h>
#include <linux/rtc.h>
#include <linux/jiffies.h>
#include "oal_ext_if.h"
#ifdef PLATFORM_DEBUG_ENABLE
#endif
#ifdef PLATFORM_DEBUG_ENABLE
#endif

/* ---- kk stub tanimlari ---- */
int32 open_tty_drv(void *pm_data)
{
    return 0;
}

int32 ps_change_uart_baud_rate(int64 baud_rate, uint8 enable_flowctl)
{
    return 0;
}

void ps_uart_state_dump(struct tty_struct *tty)
{
    /* bos */
}

void ps_uart_state_pre(struct tty_struct *tty)
{
    /* bos */
}

int32 release_tty_drv(void *pm_data)
{
    return 0;
}

