// SPDX-License-Identifier: GPL-2.0
/* OTOMATIK (araclar/stub_uret.py, G43): platform/board/board-hi1103.c dislandi; bu dosya yalniz baglama icin
 * stub tanimlar. Asagida orijinalin onislemci yonergeleri (include/if/define) aynen,
 * sonra stub govdeleri. Davranis: void bos, isaretci NULL, tamsayi 0 (OAL_SUCC/false),
 * ozel durumlar araclar/elle_govde.py'de gerekceli. */
#ifdef __cplusplus
    #if __cplusplus
    #endif
#endif
#ifdef _PRE_CONFIG_USE_DTS
#include <linux/of.h>
#include <linux/of_gpio.h>
#endif
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/pinctrl/consumer.h>
#include "board.h"
#include "plat_debug.h"
#ifdef __cplusplus
    #if __cplusplus
    #endif
#endif

/* ---- kk stub tanimlari ---- */
int32 hi1103_board_wakeup_gpio_init(void)
{
    return BOARD_FAIL;
}

void hi1103_free_board_power_gpio(void)
{
    /* bos */
}

int32 hi1103_get_board_gpio(void)
{
    return BOARD_FAIL;
}

