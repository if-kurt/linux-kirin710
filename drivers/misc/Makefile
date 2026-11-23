# SPDX-License-Identifier: GPL-2.0
#
# Makefile for misc devices that really don't fit anywhere else.
#

obj-$(CONFIG_IBM_ASM)		+= ibmasm/
obj-$(CONFIG_IBMVMC)		+= ibmvmc.o
obj-$(CONFIG_AD525X_DPOT)	+= ad525x_dpot.o
obj-$(CONFIG_AD525X_DPOT_I2C)	+= ad525x_dpot-i2c.o
obj-$(CONFIG_AD525X_DPOT_SPI)	+= ad525x_dpot-spi.o
obj-$(CONFIG_ATMEL_SSC)		+= atmel-ssc.o
obj-$(CONFIG_KK_SENSOR_OKUMA)	+= kk-sensor-okuma.o
obj-$(CONFIG_KK_I2C6_KAPI)	+= kk-i2c6-kapi.o
obj-$(CONFIG_KK_HI6523_OKU)	+= kk-hi6523-oku.o
obj-$(CONFIG_KK_SARJ_SAHTE)	+= kk-sarj-sahte.o
obj-$(CONFIG_KK_SARJ_POLITIKA)	+= kk-sarj.o
obj-$(CONFIG_KK_PIL_KORUMA)	+= kk-pil-koruma.o
obj-$(CONFIG_KK_HI6523_DENEY)	+= kk-hi6523-deney.o
obj-$(CONFIG_KK_WIFI_M0)	+= kk-wifi-m0.o
obj-$(CONFIG_KK_GUC_TESHIS)	+= kk-guc-teshis.o
obj-$(CONFIG_KK_RESET_NEDENI)	+= kk-reset-nedeni.o
obj-$(CONFIG_KK_S1_SAHTE)	+= kk-s1-sahte.o
obj-$(CONFIG_KK_I2C3_KAPI)	+= kk-i2c3-kapi.o
obj-$(CONFIG_KK_LM36274_KK)	+= kk-lm36274.o
obj-$(CONFIG_KK_SDIO_KAPI)	+= kk-sdio-kapi.o
# G48 M2a: Hi1102 ROM yükleyicisi (gömülü firmware; yollar CONFIG_KK_HI1102_FW_DIR) + yalnız QEMU sahte ROM modeli
obj-$(CONFIG_KK_HI1102_YUKLE)	+= kk-hi1102-yukle.o
obj-$(CONFIG_KK_HI1102_PORT)	+= kk-wifi.o
# G55 M3b: Huawei host katmanlari (port, gomulu)
obj-$(CONFIG_KK_HI1102_M3B)	+= kk-hi1102-port/
# G49 M2b: KK_HI1102_YALNIZ_KIP1=y iken firmware nesnesi (kk-hi1102-fw.o, .incbin) DERLENMEZ, FW_DIR kullanılmaz
ifneq ($(CONFIG_KK_HI1102_YALNIZ_KIP1),y)
KK_H_FW := $(patsubst "%",%,$(CONFIG_KK_HI1102_FW_DIR))
obj-$(CONFIG_KK_HI1102_YUKLE)	+= kk-hi1102-fw.o
CFLAGS_kk-hi1102-yukle.o := -I$(KK_H_FW)
AFLAGS_kk-hi1102-fw.o := -DKK_H_FW_0=\"$(KK_H_FW)/cr4_asic.bin\" -DKK_H_FW_1=\"$(KK_H_FW)/CPU_RAM_SCHED.bin\" -DKK_H_FW_2=\"$(KK_H_FW)/CPU_RAM_WBS.bin\"
$(obj)/kk-hi1102-fw.o: $(KK_H_FW)/cr4_asic.bin $(KK_H_FW)/CPU_RAM_SCHED.bin $(KK_H_FW)/CPU_RAM_WBS.bin
$(obj)/kk-hi1102-yukle.o: $(KK_H_FW)/kk-hi1102-fw-ozet.h
endif
obj-$(CONFIG_DUMMY_IRQ)		+= dummy-irq.o
obj-$(CONFIG_ICS932S401)	+= ics932s401.o
obj-$(CONFIG_LKDTM)		+= lkdtm/
obj-$(CONFIG_TIFM_CORE)       	+= tifm_core.o
obj-$(CONFIG_TIFM_7XX1)       	+= tifm_7xx1.o
obj-$(CONFIG_PHANTOM)		+= phantom.o
obj-$(CONFIG_RPMB)		+= rpmb-core.o
obj-$(CONFIG_QCOM_COINCELL)	+= qcom-coincell.o
obj-$(CONFIG_QCOM_FASTRPC)	+= fastrpc.o
obj-$(CONFIG_SENSORS_BH1770)	+= bh1770glc.o
obj-$(CONFIG_SENSORS_APDS990X)	+= apds990x.o
obj-$(CONFIG_ENCLOSURE_SERVICES) += enclosure.o
obj-$(CONFIG_KGDB_TESTS)	+= kgdbts.o
obj-$(CONFIG_SGI_XP)		+= sgi-xp/
obj-$(CONFIG_SGI_GRU)		+= sgi-gru/
obj-$(CONFIG_SMPRO_ERRMON)	+= smpro-errmon.o
obj-$(CONFIG_SMPRO_MISC)	+= smpro-misc.o
obj-$(CONFIG_CS5535_MFGPT)	+= cs5535-mfgpt.o
obj-$(CONFIG_GEHC_ACHC)		+= gehc-achc.o
obj-$(CONFIG_HP_ILO)		+= hpilo.o
obj-$(CONFIG_APDS9802ALS)	+= apds9802als.o
obj-$(CONFIG_ISL29003)		+= isl29003.o
obj-$(CONFIG_ISL29020)		+= isl29020.o
obj-$(CONFIG_SENSORS_TSL2550)	+= tsl2550.o
obj-$(CONFIG_DS1682)		+= ds1682.o
obj-$(CONFIG_C2PORT)		+= c2port/
obj-$(CONFIG_HMC6352)		+= hmc6352.o
obj-y				+= eeprom/
obj-y				+= cb710/
obj-$(CONFIG_VMWARE_BALLOON)	+= vmw_balloon.o
obj-$(CONFIG_PCH_PHUB)		+= pch_phub.o
obj-y				+= ti-st/
obj-y				+= lis3lv02d/
obj-$(CONFIG_ALTERA_STAPL)	+=altera-stapl/
obj-$(CONFIG_INTEL_MEI)		+= mei/
obj-$(CONFIG_VMWARE_VMCI)	+= vmw_vmci/
obj-$(CONFIG_LATTICE_ECP3_CONFIG)	+= lattice-ecp3-config.o
obj-$(CONFIG_SRAM)		+= sram.o
obj-$(CONFIG_SRAM_EXEC)		+= sram-exec.o
obj-$(CONFIG_GENWQE)		+= genwqe/
obj-$(CONFIG_ECHO)		+= echo/
obj-$(CONFIG_CXL_BASE)		+= cxl/
obj-$(CONFIG_DW_XDATA_PCIE)	+= dw-xdata-pcie.o
obj-$(CONFIG_PCI_ENDPOINT_TEST)	+= pci_endpoint_test.o
obj-$(CONFIG_OCXL)		+= ocxl/
obj-$(CONFIG_BCM_VK)		+= bcm-vk/
obj-y				+= cardreader/
obj-$(CONFIG_PVPANIC)   	+= pvpanic/
obj-$(CONFIG_UACCE)		+= uacce/
obj-$(CONFIG_XILINX_SDFEC)	+= xilinx_sdfec.o
obj-$(CONFIG_HISI_HIKEY_USB)	+= hisi_hikey_usb.o
obj-$(CONFIG_NTSYNC)		+= ntsync.o
obj-$(CONFIG_HI6421V600_IRQ)	+= hi6421v600-irq.o
obj-$(CONFIG_OPEN_DICE)		+= open-dice.o
obj-$(CONFIG_GP_PCI1XXXX)	+= mchp_pci1xxxx/
obj-$(CONFIG_VCPU_STALL_DETECTOR)	+= vcpu_stall_detector.o
obj-$(CONFIG_TMR_MANAGER)      += xilinx_tmr_manager.o
obj-$(CONFIG_TMR_INJECT)	+= xilinx_tmr_inject.o
obj-$(CONFIG_TPS6594_ESM)	+= tps6594-esm.o
obj-$(CONFIG_TPS6594_PFSM)	+= tps6594-pfsm.o
obj-$(CONFIG_NSM)		+= nsm.o
obj-$(CONFIG_MARVELL_CN10K_DPI)	+= mrvl_cn10k_dpi.o
obj-y				+= keba/
