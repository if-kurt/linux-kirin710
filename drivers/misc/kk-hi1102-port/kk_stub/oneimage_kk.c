// SPDX-License-Identifier: GPL-2.0
/* OTOMATIK (araclar/stub_uret.py, G43): common/oneimage/oneimage.c dislandi; bu dosya yalniz baglama icin
 * stub tanimlar. Asagida orijinalin onislemci yonergeleri (include/if/define) aynen,
 * sonra stub govdeleri. Davranis: void bos, isaretci NULL, tamsayi 0 (OAL_SUCC/false),
 * ozel durumlar araclar/elle_govde.py'de gerekceli. */
#ifdef __cplusplus
    #if __cplusplus
    #endif
#endif
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/stat.h>
#include <linux/of.h>
#include <linux/namei.h>
#include "oneimage.h"
#include "hisi_oneimage.h"
#include "board.h"
#include "plat_debug.h"
#if (_PRE_OS_VERSION_LINUX == _PRE_OS_VERSION)
#ifdef CONFIG_HWCONNECTIVITY
#else
#endif
#ifdef CONFIG_HWCONNECTIVITY
#else
#endif
#ifdef CONFIG_HWCONNECTIVITY
#else
#endif
#endif
#ifdef __cplusplus
    #if __cplusplus
    #endif
#endif

/* ---- kk stub tanimlari ---- */
void hw_misc_connectivity_exit(void)
{
    /* bos */
}

int hw_misc_connectivity_init(void)
{
    return 0;
}

bool is_hisi_chiptype(int32 chip)
{
    return (chip == BOARD_VERSION_HI1102);
}
EXPORT_SYMBOL_GPL(is_hisi_chiptype);

bool is_my_chip(void)
{
    return true;
}

