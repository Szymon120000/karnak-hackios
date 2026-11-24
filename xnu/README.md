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
- `overlay/pexpert/arm64/karnak_early.{c,h}` — polled UART0 console.  It
  inherits LK's 921600 8N1 setup and uses the physical address until the UART
  is mapped; `tests/karnak_early_test.c` runs it against a simulated 16550.
- `overlay/pexpert/arm64/board_mt8163.c` — `karnak_board_early_init()`, called
  from `PE_init_platform(TRUE)`: maps UART0 and prints the boot `boot_args`
  facts (DTB pointer/length, memory size, framebuffer geometry).
- `patches/` — the upstream edits.  `0001` selects the board (machine config,
  `board_config.h`, cache-line size); `0002` adds the early console to
  `files.arm64` and calls it from `pe_init.c`/`pe_serial.c`.
- `include/pexpert/...` — vendored xnu-8796 arm64 `boot.h` used only by the
  host test that asserts the firmware `boot_args` layout (see the README there).
- `tests/boot_args_test.c` — verifies `KarnakBootArgsLib` (in XnuLoaderPkg)
  field-for-field against that header.
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

## Staging the source

```sh
scripts/stage_xnu.sh            # fetch the pinned tag into vendor/xnu and apply
scripts/stage_xnu.sh --check    # non-destructive validation (CI)
```

The script pins `xnu-8796.141.3` (`1b191cb58250d0705d8a51287127505aa4bc0789`),
copies `xnu/overlay/` over the tree and applies `xnu/patches/*.patch` through
`tools/apply_xnu_patches.py`.  Both steps are idempotent, and the patcher
refuses a tree that is at the wrong revision or in a partial state.  CI runs
the real staging and then re-runs it to prove idempotence.

## Building the kernel

The full build still needs macOS + a KDK.  The intended path is the same as
`ios-sweet/.github/workflows/xnu.yml`: fetch `blacktop/darwin-xnu-build`, put
the staged source (or the patch series) into `dxb`, and build with
`MACHINE_CONFIG=KARNAK` on a macOS runner.  Xcode 15 (macos-14) is required
for the iig/IORPC ABI of xnu-8796.

`xnu.yml` in this repository runs the host-side board, early-console,
boot_args and Mach-O tests plus the patch-series check.  Its `kernel` job is
manual and does not claim to produce a kernel yet: the GICv2, timer, SMP and
MSDC0 work listed above is still missing.  `xnu/patches/0002` prints a banner
before any of that, which is the first thing a real hardware attempt should
show on UART0.
