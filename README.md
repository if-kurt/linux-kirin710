# linux-kirin710

Linux 6.12.117 with out-of-tree bring-up changes for the HiSilicon Kirin 710 SoC as found in the Huawei P30 Lite (MAR-LX1A, codename Marie), used as a headless board. This is a research tree, not a mainline-ready series.

Base: `v6.12.117` (stable). The first commit is an unmodified import of the signed release tarball (sha256 and signature check are in its commit message); the topic commits on top are the whole delta (`git log --oneline 059d524..`). Companion tooling, recovery procedures and the safety design live in the [kara-kutu](https://github.com/if-kurt/kara-kutu) repository.

Türkçe özet: Huawei P30 Lite (Kirin 710) kartını başsız (ekransız) çalıştırmak için resmî Linux 6.12.117 üstüne eklenen sürücüler ve cihaz ağacı. Araştırma ağacıdır; ana hatta gönderime hazır değildir. Çalışan ve çalışmayan parçalar aşağıdaki tabloda.

## Status / Durum

| Area | State |
|---|---|
| Device tree (`kirin710-huawei-marie*.dts`) | boots; base plus P1 (power saving), M1 (Wi-Fi) and combined variants |
| eMMC (DesignWare MSHC, HS200) | read works; writes only through a gated, time-boxed window (closed by default) |
| USB2 (DWC3 wrapper) | works as a gadget (USB NCM networking) |
| Charging / battery | hi6523 charge-enable policy with a register allow-list, chip watchdog feed, low-battery protection |
| Sensors / thermal fuse | battery and SoC temperature log; fuse stops charging and reboots to the bootloader |
| Watchdog (SP805) | `irq-is-reset` mode, write allow-list |
| Reboot mode | reboot reason through the PMIC syscon (reboot-mode) |
| cpufreq | ported 4.9 hardware-vote mechanism |
| cpuidle | PSCI CPU_SUSPEND via hvc; deepest state off by default and enabled from user space with a watchdog |
| Wi-Fi (Hi1102, SDIO) | ROM loader, firmware download, station mode; associates (open / WPA2-PSK); data path is behind an L3 filter, so internet access is limited |
| zram | works (swap on RAM only) |
| Display / DSS, GPU, audio, modem, camera, Bluetooth | not working (not ported) |
| poweroff / halt | not available: the SoC hangs after "Power down" and the watchdog resets it; use reboot |

## Safety model

The kernel is deliberately restrictive on this hardware because a wrong write can brick the device:

- eMMC command filter with a CMD6 allow-list; block-layer write gate (`KK_EMMC_YAZMA`) closed by default.
- PMIC, charger, Type-C and LPM3 are read-only except for per-register allow-lists, each with read-back.
- Thermal fuse: battery > 45 C or SoC > 70 C (two consecutive samples) clears charge enable and reboots to the bootloader.
- No `poweroff`/`halt` path: use reboot.

## Test-only code

`drivers/misc/kk-*-sahte.c` provide fake backends (charger/battery model, i2c3 + LM36274, Hi1102 ROM model, SDIO register model) so that policy and loader logic can be exercised in QEMU. They sit behind `KK_*_SAHTE` Kconfig symbols that must stay disabled in device images. Their commit subjects start with `kk-test:`; safety-related ones start with `kk-safety:`.

## Third-party code / Kaynak notu

`drivers/misc/kk-hi1102-port/` contains the Hi1102 Wi-Fi/BT host-side driver layers. They were carried over from Huawei's open-source kernel release for this device; the base of our 4.9 fork is `jglasovic/android_kernel_huawei_kirin710` at `3ca6a91`. The code is used under the GPL-2.0 terms of that release, and the original copyright lines are kept unchanged. It was adapted for 6.12 and restricted to station mode (`kk_inc`, `kk_stub` and `kk_*.c` are ours). The Hi1102 firmware images are **not** included; the build takes them from a path you provide (`CONFIG_KK_HI1102_FW_DIR`).

Hi1102 Wi-Fi sürücü dosyaları Huawei'nin açık kaynak çekirdek yayınından taşınmıştır (4.9 çatalımızın tabanı `jglasovic/android_kernel_huawei_kirin710` @ `3ca6a91`), GPL-2.0 kapsamındadır; özgün telif satırları korunmuştur.

## Building / Derleme

```
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- tinyconfig
# merge the config fragments from the kara-kutu repository, then:
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- olddefconfig
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- Image dtbs -j$(nproc)
```

The config fragments, the DTB variants and the boot image build script (embedded initramfs; the bootloader of this device does not load a separate ramdisk on normal boot) are in the `kara-kutu` repository. Do not flash anything before reading its recovery guide.

## License

GPL-2.0-only, same as the Linux kernel. See `COPYING`. The upstream `README` is unchanged.
