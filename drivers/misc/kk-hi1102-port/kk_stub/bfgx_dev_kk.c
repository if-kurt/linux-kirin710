// SPDX-License-Identifier: GPL-2.0
/* OTOMATIK (araclar/stub_uret.py, G43): bfgx/bfgx_dev.c dislandi; bu dosya yalniz baglama icin
 * stub tanimlar. Asagida orijinalin onislemci yonergeleri (include/if/define) aynen,
 * sonra stub govdeleri. Davranis: void bos, isaretci NULL, tamsayi 0 (OAL_SUCC/false),
 * ozel durumlar araclar/elle_govde.py'de gerekceli. */
#include <linux/platform_device.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/tty.h>
#include <linux/poll.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/reboot.h>
#include "board.h"
#include "hw_bfg_ps.h"
#include "plat_type.h"
#include "plat_debug.h"
#include "plat_uart.h"
#include "plat_pm.h"
#include "bfgx_user_ctrl.h"
#include "bfgx_exception_rst.h"
#include "oal_ext_if.h"
#define wait_close_times  (100)
#ifndef HI110X_HAL_MEMDUMP_ENABLE
#define RETRY_TIME 3
#else
#endif
#ifdef HI110X_HAL_MEMDUMP_ENABLE
#endif
#ifdef PLATFORM_DEBUG_ENABLE
#endif
#ifdef PLATFORM_DEBUG_ENABLE
#endif
#ifdef HI110X_HAL_MEMDUMP_ENABLE
#endif
#ifdef HAVE_HISI_NFC
#endif
#ifdef HI110X_HAL_MEMDUMP_ENABLE
#endif
#ifdef HAVE_HISI_NFC
#endif
#ifdef HI110X_HAL_MEMDUMP_ENABLE
#endif
#ifdef HAVE_HISI_NFC
#endif
#ifdef HAVE_HISI_NFC
#endif
#ifdef HI110X_HAL_MEMDUMP_ENABLE
#endif
#if 0
#else
#endif
#if 0
#else
#endif
#ifdef HAVE_HISI_NFC
#endif
#ifdef _PRE_CONFIG_USE_DTS
#endif
#ifdef _PRE_CONFIG_USE_DTS
#endif

/* ---- kk stub tanimlari ---- */
const uint8 *g_bfgx_subsys_name[BFGX_BUTT];

void hw_ps_exit(void)
{
    /* bos */
}

int32 hw_ps_init(void)
{
    return 0;
}

bool ps_chk_bfg_active(struct ps_core_s *ps_core_d)
{
    return 0;
}

bool ps_chk_only_gnss_and_cldslp(struct ps_core_s *ps_core_d)
{
    return 0;
}

int32 ps_get_core_reference(struct ps_core_s **core_data)
{
    if (core_data)
        *core_data = NULL;
    return -1;
}

int32 uart_bfgx_close_cmd(void)
{
    return 0;
}

int32 uart_wifi_close(void)
{
    return 0;
}

int32 uart_wifi_open(void)
{
    return 0;
}

