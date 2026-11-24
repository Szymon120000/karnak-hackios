# XNU on karnak

## Reality check

karnak is an **ARMv8.0 Cortex-A53 with a GIC-400 (GICv2)**, no UFS, no Apple
platform devices, and a MediaTek MT6323 PMIC.  XNU assumes a lot of Apple
platform behaviour; the hackios ports reach a shell by patching a large
surface of the kernel (see the 80+ patch series in `dipa-studio/ios-sweet`
and `zekasio/beyond2lte-ios`).

This directory contains:

- `overlay/pexpert/pexpert/arm64/KARNAK.h` — board constants, standalone and
  host-testable (`tests/board_static_test.c`).
- `README.md` — the porting plan and what is still missing.

It does **not** yet contain a bootable kernel.

## What the board header fixes

| Thing | sweet (SM7150) | karnak (MT8163) |
|-------|----------------|-----------------|
| GIC | v3 redistributors | GIC-400 v2 at `0x10221000`/`0x10222000` |
| Virtual timer | INTID 19 | INTID 27 (PPI 11) |
| Counter | 19.2 MHz | 13 MHz |
| Cores | 8 | 4 |
| Storage | UFS | MSDC0 eMMC |
| Pages | 16K | 16K or 4K (port choice) |

The generic SWEET patch series already supports a GICv2 board via
`-DB2L_BOARD` and redefines `SWEET_VTIMER_INTID`, `GICD_*`/`GICC_*` and the
counter rate.  karnak can follow the same route: add a
`-DKARNAK_BOARD` path in `SWEET.h` (or a new `ARM64_BOARD_CONFIG_KARNAK`) and
reuse the rest of the series.

## Remaining work (ordered)

1. **Board selection**: add `ARM64_BOARD_CONFIG_KARNAK` to
   `pexpert/pexpert/arm64/board_config.h` and to
   `makedefs/MakeInc.cmd` `SUPPORTED_ARM64_MACHINE_CONFIGS`; include
   `KARNAK.h` from there.  Model the diff on
   `beyond2lte-ios/patches/0001-b2l-machine-and-local-dependencies.patch`.
2. **Early console**: the SWEET series has `sweet_early.h`/`sweet_early_mark`
   writing to a framebuffer.  Replace its MMIO base with
   `KARNAK_UART0_PA` so bring-up output goes to the serial console.
3. **GICv2**: MPIDR affinity parsing, SGI 1 for IPIs, `GICC_PMR`/`GICC_CTLR`
   enable, and the `1020..1023` spurious range handling (already present in
   the b2l GICv2 patches).
4. **Timer**: no Apple `configure_timer_apple_regs`; use
   `KARNAK_COUNTER_HZ` and CNTV PPI 27.
5. **Storage**: XNU's UFS stack is useless here.  Either port a simple block
   driver for MSDC0 or boot from a RAM disk first.  The disk-backed rootfs
   goal needs an MSDC0 driver plus a partition scheme the loader understands.
6. **Cache/errata**: Cortex-A53 erratum 843419 handling, `-mstrict-align`,
   and choosing 4K vs 16K pages.  4K matches the rest of Android and makes
   the rootfs layout easier; the SWEET series currently assumes 16K.
7. **Rootfs**: HFS+ image on eMMC (see `rootfs/`), or the userspace bundle
   from an IPSW.

## Building (when the patches exist)

Use the same approach as `ios-sweet/.github/workflows/xnu.yml`: fetch
`blacktop/darwin-xnu-build`, drop our patch series into `dxb/patches/`, and
build on a macOS runner with a KDK.  Xcode 15 (macos-14) is required for the
iig/IORPC ABI of xnu-8796.

`xnu.yml` in this repository is a **manual** workflow that runs the host-side
board test; it does not claim to produce a kernel yet.
