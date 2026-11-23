/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Kara Kutu G55 M3b: port nesnelerinin HER birine -include ile girer (Kbuild KKSTUB).
 *  - Gömülü derlemede port'un modül parametreleri yazılamaz: bütün module_param'lar 0444
 *    (M3BCD §4.5: sdio_dump_mem_flag, hcc_flowctrl_detect_panic, hisdio_intr_mode, sdio_extend_func,
 *    wifi_patch_enable, auto_freq_enable ... panik/dump yolunu açıyordu).
 *  - oal_module_init/exit: oal_schedule.h'te (CONFIG_KK_HI1102_M3B) açılışta ÇAĞRILMAYAN sarmalayıcıya döner;
 *    sıra kk-m3b.c'de elle.
 */
#ifndef KK_PORT_ON_H
#define KK_PORT_ON_H
#include <linux/moduleparam.h>
#undef module_param
#define module_param(name, type, perm) module_param_named(name, name, type, 0444)
#include "kk_m3b_kapi.h"
/* G55: 6.x'te dev->dev_addr dogrudan yazilmaz (dev_addr_check WARNING); port'un MAC yazmalari eth_hw_addr_set'e */
#include <linux/etherdevice.h>
#define KK_DEV_ADDR_YAZ(d, m)	eth_hw_addr_set((d), (const u8 *)(m))
#include "kk_port_sahte.h"
/* G55 M3b: port'tan DOSYA SISTEMI ERISIMI YOK (okuma da yazma da): firmware/cfg/INI gomulu ya da P-D motorunda;
   log/dump/efuse/uart dosyalari derlenmez. Her cagri kk_m3b_dur + -EPERM. */
#include <linux/fs.h>
#include <linux/err.h>
struct file *kk_m3b_dosya_red(const char *ad);
ssize_t kk_m3b_dosya_io_red(const char *ad);
#define filp_open(...)		kk_m3b_dosya_red("filp_open")
#define kernel_read(...)	kk_m3b_dosya_io_red("kernel_read")
#define kernel_write(...)	kk_m3b_dosya_io_red("kernel_write")
#define vfs_read(...)		kk_m3b_dosya_io_red("vfs_read")
#define vfs_write(...)		kk_m3b_dosya_io_red("vfs_write")
#endif
