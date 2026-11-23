// SPDX-License-Identifier: GPL-2.0
/* OTOMATIK (araclar/stub_uret.py, G43): platform/oal/oal_hcc_test.c dislandi; bu dosya yalniz baglama icin
 * stub tanimlar. Asagida orijinalin onislemci yonergeleri (include/if/define) aynen,
 * sonra stub govdeleri. Davranis: void bos, isaretci NULL, tamsayi 0 (OAL_SUCC/false),
 * ozel durumlar araclar/elle_govde.py'de gerekceli. */
#define HISI_LOG_TAG "[HCC_TEST]"
#include "oal_hcc_host_if.h"
#include "oal_ext_if.h"
#ifdef _PRE_CONFIG_CONN_HISI_SYSFS_SUPPORT
#include "oal_kernel_file.h"
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/mmc/card.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/host.h>
#include <linux/version.h>
#ifdef _PRE_CONFIG_HISI_PANIC_DUMP_SUPPORT
#ifdef PLATFORM_DEBUG_ENABLE
#endif
#endif
#ifdef _PRE_CONFIG_HISI_PANIC_DUMP_SUPPORT
#endif
#ifdef _PRE_CONFIG_HISI_PANIC_DUMP_SUPPORT
#endif
#ifdef CONFIG_MMC
#else
#endif
#if 0
#endif
#ifdef _PRE_CONFIG_WLAN_THRANS_THREAD_DEBUG
#endif
#ifdef CONFIG_PRINTK
#endif
#ifdef CONFIG_PRINTK
#endif
#ifdef _PRE_CONFIG_HISI_PANIC_DUMP_SUPPORT
#endif
#ifdef PLATFORM_DEBUG_ENABLE
#endif
#ifdef _PRE_CONFIG_HISI_PANIC_DUMP_SUPPORT
#endif
#ifdef _PRE_CONFIG_HISI_PANIC_DUMP_SUPPORT
#endif
#ifdef PLATFORM_DEBUG_ENABLE
#endif
#ifdef _PRE_CONFIG_HISI_PANIC_DUMP_SUPPORT
#endif
#ifdef PLATFORM_DEBUG_ENABLE
#endif
#endif

/* ---- kk stub tanimlari ---- */
oal_void hcc_test_exit_module(struct hcc_handler* hcc)
{
    /* bos */
}

oal_int32 hcc_test_init_module(struct hcc_handler* hcc)
{
    return 0;
}

void hwifi_panic_log_dump(char* print_level)
{
    /* bos */
}

oal_void hwifi_panic_log_register(hwifi_panic_log* pst_log, oal_void* data)
{
    /* bos */
}
EXPORT_SYMBOL_GPL(hwifi_panic_log_register);

oal_void hwifi_panic_log_unregister(hwifi_panic_log* pst_log)
{
    /* bos */
}
EXPORT_SYMBOL_GPL(hwifi_panic_log_unregister);

