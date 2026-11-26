# Project status

## What is implemented

- **Hardware map**: generated from Amazon's 3.18 DTS. `--check` mode fails the
  build if it drifts from the sources (skipped with a warning on hosts without
  Python `libfdt`).  Generated text is independent of whether `--kernel` was
  given as a relative or absolute path.
- **KarnakBootShim** (`platform/edk2/KarnakPkg/BootShim/BootShim.S`):
  - disables the MT8163 `toprgu` watchdog before the first FD instruction,
  - paints a red band on every LK framebuffer layer as an early proof-of-life,
  - preserves LK's `x0` DTB pointer,
  - copies a validated blob to `0x4fe00000` (bounded to 1 MiB),
  - publishes the pointer at `0x4ff00000`,
  - relocates the appended FD to `0x50000000`, cleans/invalidates caches and
    branches.
- **EDK2 `KarnakPkg`**:
  - GIC-400/GICv2, 13 MHz architected timer (INTID 29/30/27/26),
  - runtime memory map from the saved DTB (falls back to the DTS floor),
  - `DtPlatformDxe` fed by `KarnakDtbLoaderLib`,
  - UART0 console at 921600 (LK-configured),
  - `KarnakFbDxe` GOP on the LK framebuffer (see Framebuffer below),
  - no-op watchdog architectural protocol (`KarnakWatchdogDxe`), required by
    DxeCore before BDS can start,
  - shell, FAT, USB, RamDisk.
- **XnuImageLib** (`platform/edk2/XnuLoaderPkg/Library/XnuImageLib`): validates
  an ARM64 Mach-O kernel, records its loadable segments, computes the physical
  footprint on 16 KiB pages and checks a proposed destination against reserved
  ranges.  Host tests (`xnu/tests/xnu_image_test.c`) run in CI.
- **Rootfs container** (`rootfs/mkrootfs.py`): dependency-free GPT builder for a
  FAT boot partition plus an HFS+ `KARNAK-ROOT` partition, with host tests and
  `sgdisk --verify` in CI.
- **CI**: `uefi.yml` builds `out/uefi-karnak.img` with the GCC5 toolchain on
  Ubuntu 24.04, then checks the boot header, the shim payload offset and the FV
  header checksum with `tools/check_boot_image.py`.  `rootfs.yml` and `xnu.yml`
  validate their own pieces.

## Not implemented / unverified

- **XNU**: the board is selectable (`ARM64_BOARD_CONFIG_KARNAK`, machine config
  and patch series in `xnu/patches/`), a polled UART0 early console exists and
  is host-tested, and `karnak_board_early_init()` prints the boot `boot_args`.
  The GIC-400 (GICv2) driver, the architected timer as PPI 27, and PSCI/SMP
  with SGI 1 are implemented in `0003` and `KarnakCPU.cpp`; a bootable XNU
  still needs A53 errata handling and an MSDC0 block driver (no UFS on this
  SoC), and none of the new code has been compiled by an XNU build yet.
  Tracked in `xnu/README.md`.
- **XNU patch series**: `scripts/stage_xnu.sh` fetches the pinned
  `xnu-8796.141.3` tag, copies the overlay and applies three reviewed patches;
  CI stages a real tree and re-runs the staging to prove idempotence.  The
  patches apply cleanly but have not been compiled by an XNU build yet.
- **XnuLoader**: the Mach-O parser, the `boot_args` builder (`KarnakBootArgsLib`,
  layout asserted against Apple's xnu-8796 arm64 header), the segment copy and
  the `ExitBootServices` / MMU-off handoff (`KarnakHandoffLib`) exist.  The
  application is in the FV; it opens `\xnu\kernelcache` (or a root-level
  `\kernelcache`), parses it (including `LC_UNIXTHREAD`, which real XNU kernels
  use), copies every loadable segment to allocator-provided memory, builds
  `boot_args` and enters it.  No kernel built for karnak exists yet; the path
  is proved end-to-end on hardware with a synthetic Mach-O (see below).
- **UEFI exception level**: MediaTek PrePi runs at **EL2** (it sets
  `HCR_EL2.TGE`).  `KarnakXnuHandoff` clears `TGE`/`E2H`, programs an EL1 state
  with `SCTLR_EL1.M/C/I = 0` and returns through `ELR_EL2`/`SPSR_EL2`.  This is
  verified on hardware (see below).
- **Disk-backed rootfs**: the GPT container is built and tested, but the HFS+
  payload (IPSW-derived Darwin tree) and the MSDC0 block driver are missing.
- **Touch/display panel drivers**: the panel is driven by LK; XNU framebuffer
  output is not yet wired.
- **PMIC/GPIO**: `MtkGpioImplLib` and `PmicWrapperImplLib` are bound to the
  MT6789 instances so the DSC is complete, but the drivers are not in the FV.
  A real MT8163 + MT6323 port is required for battery/keys.
- **DRAM size**: the live DTB on a retail karnak reports two banks,
  `0x40000000+0x40000000` and `0x80000000+0x19600000` (top `0x99600000`,
  ~1.4 GiB usable), so the 512 MiB DTS value is only the build-time floor.
- **Framebuffer**: `/chosen:atag,videolfb` on the same device gives
  `fb_base=0x98800000`, `vram=0x00e00000`.  `KarnakFbDxe` is still driven by a
  fixed PCD pinned to those verified values; it should read the property.
  It declares RGBA byte order (the panel scans bytes R,G,B,A) and cleans the
  dirty cache lines after each Blt.  `EfiBltBufferToVideo` bypasses
  `FrameBufferBltLib` with a direct copy: the library's RGB-reserved
  conversion path silently produced nothing on this panel, which is why
  console text and glyphs were invisible while solid fills worked.

## Hardware test readiness

The first on-device test is the UEFI stage only (`LK -> BootShim -> EDK2`):

- `BOOT_HEADER_VERSION=0` produces `out/uefi-karnak-lk.img` in the legacy
  layout the stock LK expects (2048-byte pages, explicit `kernel_addr` and
  `tags_addr`, both mandatory).  The kernel is a gzip of just the small shim
  with the stock DTB appended, and the EDK2 FD is the boot image ramdisk:
  LK's decompressor fails on kernels with large compressed bodies, but a tiny
  kernel plus a ramdisk boots reliably.  BootShim copies the FD from
  `KARN_RAMDISK_BASE` (0x43480000, from the stock boot header) to
  `KARN_UEFI_BASE`.  The DTB is taken from `KARNAK_DTB=<file>` or extracted
  from `device-backup/boot-stock.img` with `tools/extract_dtb.py`.
- Verified on hardware: LK executes the payload, BootShim runs, the real
  EDK2 firmware takes over, DXE dispatches, and **BDS completes**:
  `KarnakProbeDxe` paints yellow at EndOfDxe and blue at ReadyToBoot, then the
  UEFI Shell starts and is **readable on the panel** (white-on-blue text,
  `FS0:`/`FS1:` mapping table, `Shell>` prompt).  With
  `PcdKarnakBringupMarkers=TRUE` the probe also draws solid
  buffer-to-video squares and an 8x8 `A` glyph; the Shell clears them when it
  starts, which is expected.
  The panel scans bytes R,G,B,A; a little-endian word is `0xAABBGGRR`, so
  opaque red is `0xFF0000FF`.
- Verified on hardware (handoff): with `PcdKarnakHandoffSelfTest=TRUE` the
  probe builds a real `boot_args` block at `ReadyToBoot`, calls
  `ExitBootServices`, and branches through `KarnakXnuHandoff` to
  `KarnakHandoffTestPayload` with the MMU and caches off.  The panel shows a
  white top band followed by solid green, i.e. the payload ran, `boot_args`
  was valid and the CPU was at **EL1**.  This proves the EL2 -> EL1 drop,
  cache cleaning, MMU-off entry and direct physical framebuffer writes.  A
  self-test build intentionally stops there (no Shell); power-cycle to
  recover.
- Verified on hardware (loader end-to-end): with
  `PcdKarnakSyntheticKernelTest=TRUE` the probe loads `XnuLoader.efi` from the
  FV at `ReadyToBoot`.  The loader opens the named FV file `kernelcache`
  through `EFI_FILE_PROTOCOL`, parses it as an ARM64 Mach-O with
  `LC_UNIXTHREAD`, copies its `__TEXT` segment, builds real `boot_args`, calls
  `ExitBootServices` and enters the copy with the MMU/caches off.  The
  synthetic payload paints white (entry reached), a cyan band (this is the
  loaded image, not the probe payload) and green (boot_args valid, EL1).  This
  covers the complete file -> parse -> copy -> handoff path that a real
  kernelcache will use.  Power-cycle to recover from a test build.
- `tools/check_boot_image.py` enforces the appended DTB and the ramdisk FD for
  v0 images; `tools/test_extract_dtb.py` and `tools/test_check_boot_image.py`
  cover both layouts.
- `scripts/flash_karnak.sh` validates the image, the `karnak` product string,
  the unlock state and a stock boot-image backup before writing.
- The retail boot image has an amonet microloader stub in `0x0..0x400` and the
  real header at `0x400` (kernel at `0x800`).  The partition must be written
  with that stub: `tools/inject_microloader.py --stub-from boot-stock.img`
  (also `scripts/build_lk_image.sh --inject-from boot-stock.img`).
- Measured LK values from the device's stock boot header:
  `LK_KERNEL_ADDR=0x40080000`, `LK_TAGS_ADDR=0x48000000`, `LK_PAGE_SIZE=2048`.
- First boot test reached LK's splash and then reset.  The live kernel log
  showed `bootreason=wdt_by_pass_pwk` / "Warm Boot By HW Watchdog", which is
  why the shim and `WatchDogDxe` now disable the watchdog.  The same log gave
  the real framebuffer base, replacing the 512 MiB fallback.

Expected on success: UEFI console/shell on the panel, with `FS0:` (the
firmware volume) and `FS1:` (the GPIO/regulator RAM disk) mapped.  There is no
XNU kernel and no internal eMMC driver yet.

## Boot chain checkpoints

When serial logging works, the expected markers are:

```
KarnakBootShim: ...
Karnak: DT <n> bytes at 0x4fe00000
Karnak: DT <n> bytes, 1 DRAM range(s), <size> MiB total
Karnak: framebuffer 0x... + 0x...
```

## Safety

Writing a boot image to `boot` on a locked device bricks it. Unlock first and
keep a known-good boot image. This repository never touches the device
automatically.
