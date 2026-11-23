/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Kara Kutu G48 (Wi-Fi M2a): Hi1102 firmware ikililerinin imaja gömülmesi (.rodata). Yollar Makefile'dan
 * (CONFIG_KK_HI1102_FW_DIR); boy + CRC32 kk-hi1102-fw-ozet.h ile, sha256 build.sh ile denetlenir.
 * FW_LOADER yok: çalışma anında dosya okunmaz.
 */
	.section .rodata, "a"
	.balign 64
	.globl kk_h_fw_0
kk_h_fw_0:
	.incbin KK_H_FW_0
	.globl kk_h_fw_0_son
kk_h_fw_0_son:
	.balign 64
	.globl kk_h_fw_1
kk_h_fw_1:
	.incbin KK_H_FW_1
	.globl kk_h_fw_1_son
kk_h_fw_1_son:
	.balign 64
	.globl kk_h_fw_2
kk_h_fw_2:
	.incbin KK_H_FW_2
	.globl kk_h_fw_2_son
kk_h_fw_2_son:
