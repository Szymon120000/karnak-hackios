# XNU on karnak

## Reality check

karnak is an **ARMv8.0 Cortex-A53 with a GIC-400 (GICv2)**, no UFS, no Apple
platform devices, and a MediaTek MT6323 PMIC.  XNU assumes a lot of Apple
platform behaviour; the hackios ports reach a shell by patching a large
surface of the kernel (see the 80+ patch series in `dipa-studio/ios-sweet`
and `zekasio/beyond2lte-ios`).

This directory contains:

- `overlay/config/MASTER.arm64.karnak` — the kernel configuration.  It is
  `MASTER.arm64.MacOSX` with `nos_arm_asm nos_arm_pmap` (so the open-source
  arm64 assembly and pmap are linked instead of Apple's arm64e KDK library)
  and with `kperf`/`kpc` kept, because XNU does not build without kperf; the
  kpc backend is a countable no-op on non-Apple hardware.
- `overlay/pexpert/pexpert/arm64/KARNAK.h` — board constants, standalone and
  host-testable (`tests/board_static_test.c`).
- `overlay/pexpert/arm64/karnak_early.{c,h}` — polled UART0 console.  It
  inherits LK's 921600 8N1 setup and uses the physical address until the UART
  is mapped; `tests/karnak_early_test.c` runs it against a simulated 16550.
- `overlay/pexpert/arm64/board_mt8163.c` — `karnak_board_early_init()`, called
  from `PE_init_platform(TRUE)`: maps UART0 and prints the boot `boot_args`
  facts (DTB pointer/length, memory size, framebuffer geometry).
- `overlay/iokit/Kernel/arm/KarnakPlatformExpert.cpp` — minimal
  `IODTPlatformExpert` that publishes the stock MT8163 device tree without an
  Apple `/options` or NVRAM node.
- `overlay/iokit/Kernel/arm/KarnakCPU.cpp` — `IOCPU` bring-up of the four
  Cortex-A53s: SMC32 `PSCI_CPU_ON` from the DTB (`0x84000003`) and SGI 1 for
  IPIs.
- `patches/` — the upstream edits.  `0001` selects the board (machine config,
  `board_config.h`, cache-line size); `0002` adds the early console to
  `files.arm64` and calls it from `pe_init.c`/`pe_serial.c`; `0003` adds the
  GIC-400 (GICv2) interrupt controller, the architected timer as PPI 27, the
  PSCI secondary entry point and the `ml_is_boot_cpu` fallback for DTBs without
  an Apple `state` property (and it exposes `cpu_signal_handler` to the KARNAK
  IRQ path); `0004` backports the `IORPC.kernelContent` field
  that Xcode 16's `iig` generates code against; `0005` applies the Xcode 16
  `UIO_STACKBUF` fix and gives the non-PAC pmap a distinct spare field;
  `0006` fills the gaps left by Apple's closed per-SoC library for the
  open-source pmap/assembly path (pmap preemption/lockdown symbols, cache
  asm labels, the no-op monitor hook).  The overlay also provides the missing
  AMCC/SART/UAT/tunables no-ops and a kpc backend that reports no counters.
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
| Pages | 16K | 4K |

The generic SWEET patch series already supports a GICv2 board via
`-DB2L_BOARD` and redefines `SWEET_VTIMER_INTID`, `GICD_*`/`GICC_*` and the
counter rate.  karnak follows the same route with its own
`ARM64_BOARD_CONFIG_KARNAK` path and `KARNAK.h` constants.

## Remaining work (ordered)

1. ~~Board selection~~: done in `0001`
   (`ARM64_BOARD_CONFIG_KARNAK`, `MACHINE_FLAGS_ARM64_KARNAK`, 4 KiB pages).
2. ~~Early console~~: done in `0002`; `karnak_early.c` drives UART0 at
   921600 8N1.
3. ~~GICv2~~: done in `0003`; the GIC-400 is mapped at
   `0x10221000`/`0x10222000`, all SPIs are masked, and PPI 27 plus SGI 1 are
   enabled as non-secure Group 1 IRQs with combined EOI.
4. ~~Timer~~: done in `0003`; `CNTFRQ_EL0` (13 MHz) feeds
   `gPEClockFrequencyInfo`, the virtual timer is PPI 27, and `ml_set_decrementer`
   keeps using `CNTV_TVAL_EL0`.
5. ~~SMP~~: done in `0003` + `KarnakCPU.cpp`; secondaries enter through
   `karnak_secondary_entry`, drop EL2 if needed, and issue SMC32
   `PSCI_CPU_ON` (`0x84000003`).  The stock DTB has no `state` property, so
   `ml_is_boot_cpu` matches `reg` against `MPIDR_EL1.Aff0`.
6. **Storage**: XNU's UFS stack is useless here.  Either port a simple block
   driver for MSDC0 or boot from a RAM disk first.  The disk-backed rootfs
   goal needs an MSDC0 driver plus a partition scheme the loader understands.
7. **Cache/errata**: Cortex-A53 erratum 843419 handling and `-mstrict-align`
   are still open.  Pages are 4 KiB to match Android and simplify the rootfs
   layout.
8. **Rootfs**: HFS+ image on eMMC (see `rootfs/`), or the userspace bundle
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

The full build still needs macOS + a KDK.  `xnu.yml`'s `kernel` job (manual:
set `build_kernel` when dispatching) fetches `blacktop/darwin-xnu-build` at a
pinned commit, stages this tree into `dxb/xnu` with `scripts/stage_xnu.sh`,
and builds with `MACHINE_CONFIG=KARNAK`.  It runs on `macos-15`/Xcode 16;
`0004` supplies the `IORPC.kernelContent` field that this Xcode's `iig`
generates code against and `0005` carries the other two Xcode 16 compile
fixes (the retired macos-14 runner had Xcode 15 and needed none of them).  The job builds plain arm64 (no arm64e slice) because the
Cortex-A53 has no pointer authentication.

`xnu.yml` also runs the host-side board, early-console, boot_args and Mach-O
tests plus the patch-series check on every push.  The kernel job does not
claim to produce a bootable kernel yet: the MSDC0 block driver and rootfs
listed above are still missing.  The first real hardware attempt should show
the `0002` banner on UART0, then the `0003` GIC line, followed by one line per
secondary CPU from `KarnakCPU`.
