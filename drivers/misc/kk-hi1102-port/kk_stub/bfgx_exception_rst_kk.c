// SPDX-License-Identifier: GPL-2.0
/* OTOMATIK (araclar/stub_uret.py, G43): bfgx/bfgx_exception_rst.c dislandi; bu dosya yalniz baglama icin
 * stub tanimlar. Asagida orijinalin onislemci yonergeleri (include/if/define) aynen,
 * sonra stub govdeleri. Davranis: void bos, isaretci NULL, tamsayi 0 (OAL_SUCC/false),
 * ozel durumlar araclar/elle_govde.py'de gerekceli. */
#include <linux/delay.h>
#include <linux/rtc.h>
#include <linux/tty.h>
#include "plat_type.h"
#include "plat_debug.h"
#include "hw_bfg_ps.h"
#include "plat_pm.h"
#include "plat_pm_wlan.h"
#include "bfgx_exception_rst.h"
#include "plat_firmware.h"
#include "plat_uart.h"
#include "oal_sdio.h"
#include "oal_sdio_host_if.h"
#include "oal_hcc_host_if.h"
#include "oal_ext_if.h"
#define WIFI_PUB_REG_BLOCKS      (12)
#define WIFI_PRIV_REG_BLOCKS     (9)
#define WIFI_MEM_BLOCKS          (3)
#define WIFI_PUB_GLB_CTL_LEN              (4*1024)
#define WIFI_PUB_PMU_CMU_CTL_LEN          (4*1024)
#define WIFI_PUB_RF_WB_CTL_LEN            (2*1024)
#define WIFI_PUB_RF_WB_TRX_REG_LEN        (2*1024)
#define WIFI_PUB_RF_WB_PLL_REG_LEN        (2*1024)
#define WIFI_PUB_RF_FG_CTL_LEN            (2*1024)
#define WIFI_PUB_RF_FG_TRX_REG_LEN        (2*1024)
#define WIFI_PUB_RF_FG_PLL_REG_LEN        (2*1024)
#define WIFI_PUB_COEX_CTL_LEN             (1*1024)
#define WIFI_PUB_DIAG_CTL_LEN             (1*1024)
#define WIFI_PUB_COM_CTL_LEN              (2*1024)
#define WIFI_PUB_AON_GPIO_RTC_LEN         (16*1024)
#define WIFI_PUB_REG_TOTAL_LEN            (WIFI_PUB_GLB_CTL_LEN \
                                           + WIFI_PUB_PMU_CMU_CTL_LEN \
                                           + WIFI_PUB_RF_WB_CTL_LEN \
                                           + WIFI_PUB_RF_WB_TRX_REG_LEN \
                                           + WIFI_PUB_RF_WB_PLL_REG_LEN \
                                           + WIFI_PUB_RF_FG_CTL_LEN \
                                           + WIFI_PUB_RF_FG_TRX_REG_LEN \
                                           + WIFI_PUB_RF_FG_PLL_REG_LEN \
                                           + WIFI_PUB_COEX_CTL_LEN \
                                           + WIFI_PUB_DIAG_CTL_LEN \
                                           + WIFI_PUB_COM_CTL_LEN \
                                           + WIFI_PUB_AON_GPIO_RTC_LEN)
#define WIFI_PRIV_W_CTL_LEN               (1*1024)
#define WIFI_PRIV_W_WDT_LEN               (1*1024)
#define WIFI_PRIV_W_TIMER_LEN             (1*1024)
#define WIFI_PRIV_W_CPU_CTL_LEN           (1*1024)
#define WIFI_PRIV_W_PHY_BANK1_LEN         (1*1024)
#define WIFI_PRIV_W_PHY_BANK2_LEN         (1*1024)
#define WIFI_PRIV_W_PHY_BANK3_LEN         (1*1024)
#define WIFI_PRIV_W_PHY_BANK4_LEN         (1*1024)
#define WIFI_PRIV_W_MAC_BANK_LEN          (2*1024 + 512)
#define WIFI_PRIV_W_DMA_LEN               (1*1024)
#define WIFI_PRIV_REG_TOTAL_LEN           (WIFI_PRIV_W_CTL_LEN \
                                           + WIFI_PRIV_W_WDT_LEN \
                                           + WIFI_PRIV_W_TIMER_LEN \
                                           + WIFI_PRIV_W_CPU_CTL_LEN \
                                           + WIFI_PRIV_W_PHY_BANK1_LEN \
                                           + WIFI_PRIV_W_PHY_BANK2_LEN \
                                           + WIFI_PRIV_W_PHY_BANK3_LEN \
                                           + WIFI_PRIV_W_PHY_BANK4_LEN \
                                           + WIFI_PRIV_W_MAC_BANK_LEN \
                                           + WIFI_PRIV_W_DMA_LEN)
#define WIFI_MEM_W_TCM_WRAM_LEN           (512*1024)
#define WIFI_MEM_W_PKT_SHARE_RAM_LEN      (256*1024)
#define WIFI_MEM_B_SHARE_RAM_LEN          (64*1024)
#define WIFI_MEM_TOTAL_LEN                (WIFI_MEM_W_TCM_WRAM_LEN \
                                           + WIFI_MEM_W_PKT_SHARE_RAM_LEN \
                                           + WIFI_MEM_B_SHARE_RAM_LEN)
#define BFGX_PUB_REG_NUM                  (5)
#define BFGX_PRIV_REG_NUM                 (2)
#define BFGX_SHARE_RAM_NUM                (2)
#define BFGX_GLB_PMU_CMU_CTL_ADDR                       (0x50000000)
#define BFGX_RF_WB_CTL_ADDR                             (0x5000C000)
#define BFGX_RF_FG_CTL_ADDR                             (0x5000E000)
#define BFGX_COEX_DIAG_COM_CTL_ADDR                     (0x50010000)
#define BFGX_AON_WB_GPIO_RTC_ADDR                       (0x50006000)
#define BFGX_PATCH_PLAT_NFC_BFGNI_SHARE_RAM_ADDR        (0x300E4000)
#define BFGX_W_SHARE_RAM_ADDR                           (0x30178000)
#define BFGX_GNSS_SUB_ADDR                              (0x38000000)
#define BFGX_B_CTL_WDT_TIMER_UART_ADDR                  (0x40000000)
#define BFGX_IR_SUB_ADDR                                (0x40007000)
#define BFGX_B_DMA_CFG_ADDR                             (0x40010000)
#define BFGX_BT_FM_SUB_ADDR                             (0x41040000)
#define BFGX_NFC_APB_ADDR                               (0x60000000)
#define BFGX_GLB_PMU_CMU_CTL_LEN                        (1024*12)
#define BFGX_RF_WB_CTL_LEN                              (1024*8)
#define BFGX_RF_FG_CTL_LEN                              (1024*8)
#define BFGX_COEX_DIAG_COM_CTL_LEN                      (1024*20)
#define BFGX_AON_WB_GPIO_RTC_LEN                        (1024*16)
#define BFGX_PATCH_PLAT_NFC_BFGNI_SHARE_RAM_LEN         (1024*592)
#define BFGX_W_SHARE_RAM_LEN                            (1024*32)
#define BFGX_GNSS_SUB_LEN                               (1024*128)
#define BFGX_B_CTL_WDT_TIMER_UART_LEN                   (1024*20)
#define BFGX_IR_SUB_LEN                                 (1024*4)
#define BFGX_B_DMA_CFG_LEN                              (1024*4)
#define BFGX_BT_FM_SUB_LEN                              (1024*140)
#define BFGX_NFC_APB_LEN                                (1024*20)
#define GLB_PMU_CMU_CTL_FILE_NAME                       "glb_pmu_cmu_ctl"
#define RF_WB_CTL_FILE_NAME                             "rf_wb_ctl"
#define RF_FG_CTL_FILE_NAME                             "rf_fg_ctl"
#define COEX_DIAG_COM_CTL_FILE_NAME                     "coex_diag_com_ctl"
#define AON_WB_GPIO_RTC_FILE_NAME                       "aon_wb_gpio_rtc"
#define PATCH_PLAT_NFC_BFGNI_SHARE_RAM_FILE_NAME        "patch_plat_nfc_bfgni_share_ram"
#define W_SHARE_RAM_FILE_NAME                           "w_share_ram"
#define GNSS_SUB_FILE_NAME                              "gnss_sub"
#define B_CTL_WDT_TIMER_UART_FILE_NAME                  "b_ctl_wdt_timer_uart"
#define IR_SUB_FILE_NAME                                "ir_sub"
#define B_DMA_CFG_FILE_NAME                             "b_dma_cfg"
#define BT_FM_SUB_FILE_NAME                             "bt_fm_sub"
#define NFC_APB_FILE_NAME                               "nfc_apb"
#ifdef HI110X_HAL_MEMDUMP_ENABLE
#endif
#ifdef _PRE_WLAN_FEATURE_DFR
#else
#endif //_PRE_WLAN_FEATURE_DFR
#ifndef HI110X_HAL_MEMDUMP_ENABLE
#endif
#ifndef HI110X_HAL_MEMDUMP_ENABLE
#endif
#ifdef PLATFORM_DEBUG_ENABLE
#endif
#ifdef HI110X_HAL_MEMDUMP_ENABLE
#endif

/* ---- kk stub tanimlari ---- */
int32 bfgx_system_reset(void)
{
    return 0;
}

int32 exception_bcpu_dump_recv(uint8* str, oal_netbuf_stru* netbuf)
{
    return 0;
}

int32 is_bfgx_exception(void)
{
    return 0;
}

int32 plat_exception_reset_exit(void)
{
    return 0;
}

int32 plat_exception_reset_init(void)
{
    return 0;
}

int32 plat_power_fail_exception_info_set(uint32 subsys_type, uint32 thread_type, uint32 exception_type)
{
    return 0;
}

void plat_power_fail_process_done(void)
{
    /* bos */
}

int32 wifi_exception_mem_dump(struct st_wifi_dump_mem_info *pst_mem_dump_info, uint32 count, oal_int32 excep_type)
{
    return 0;
}

int32 wifi_exception_work_submit(uint32 wifi_excp_type)
{
    return 0;
}

oal_workqueue_stru* wifi_get_exception_workqueue(oal_void)
{
    return NULL;
}

int32 wifi_memdump_enquenue(uint8 *buf_ptr, uint16 count)
{
    return 0;
}

void wifi_memdump_finish(void)
{
    /* bos */
}

int32 wifi_notice_hal_memdump(void)
{
    return 0;
}

