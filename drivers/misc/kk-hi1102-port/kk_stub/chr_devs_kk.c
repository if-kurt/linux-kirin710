// SPDX-License-Identifier: GPL-2.0
/* OTOMATIK (araclar/stub_uret.py, G43): common/chr_log/chr_devs.c dislandi; bu dosya yalniz baglama icin
 * stub tanimlar. Asagida orijinalin onislemci yonergeleri (include/if/define) aynen,
 * sonra stub govdeleri. Davranis: void bos, isaretci NULL, tamsayi 0 (OAL_SUCC/false),
 * ozel durumlar araclar/elle_govde.py'de gerekceli. */
#ifdef __cplusplus
    #if __cplusplus
    #endif
#endif
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/semaphore.h>
#include <linux/miscdevice.h>
#include <linux/device.h>
#include <linux/sched.h>
#include <linux/workqueue.h>
#include <asm/atomic.h>
#include <stdarg.h>
#include <linux/slab.h>
#include <linux/unistd.h>
#include <linux/un.h>
#include <linux/skbuff.h>
#ifdef CONFIG_HWCONNECTIVITY
#include "hisi_oneimage.h"
#endif
#include "chr_devs.h"
#include "oneimage.h"
#include "board.h"
#include "oal_schedule.h"
#if (_PRE_OS_VERSION_LINUX == _PRE_OS_VERSION)
#endif
#if (_PRE_OS_VERSION_LINUX == _PRE_OS_VERSION)
#endif
#ifdef __cplusplus
    #if __cplusplus
    #endif
#endif

/* ---- kk stub tanimlari ---- */
int32 __chr_exception(uint32 errno)
{
    return 0;
}
EXPORT_SYMBOL_GPL(__chr_exception);

int32 __chr_printLog(CHR_LOGPRIORITY prio, CHR_DEV_INDEX dev_index, const int8 *fmt,...)
{
    return 0;
}

void chr_dev_exception_callback(void *buff, uint16 len)
{
    /* bos */
}

void chr_miscdevs_exit(void)
{
    /* bos */
}

int32 chr_miscdevs_init(void)
{
    return 0;
}

