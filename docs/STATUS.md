# Project status

## What is implemented

- **Hardware map**: generated from Amazon's 3.18 DTS. `--check` mode fails the
  build if it drifts from the sources (skipped with a warning on hosts without
  Python `libfdt`).  Generated text is independent of whether `--kernel` was
  given as a relative or absolute path.
- **KarnakBootShim** (`platform/edk2/KarnakPkg/BootShim/BootShim.S`):
  - preserves LK's `x0` DTB pointer,
  - copies a validated blob to `0x4fe00000` (bounded to 1 MiB),
  - publishes the pointer at `0x4ff00000`,
  - relocates the appended FD to `0x50000000` and branches.
- **EDK2 `KarnakPkg`**:
  - GIC-400/GICv2, 13 MHz architected timer (INTID 29/30/27/26),
  - runtime memory map from the saved DTB (falls back to the DTS floor),
  - `DtPlatformDxe` fed by `KarnakDtbLoaderLib`,
  - UART0 console at 921600 (LK-configured),
  - SimpleFbDxe, watchdog kicker, shell, FAT, USB, RamDisk.
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
  A bootable XNU still needs the GICv2 driver, the architected-timer wiring,
  SMP/PSCI, A53 errata handling and an MSDC0 block driver (no UFS on this SoC).
  Tracked in `xnu/README.md`.
- **XNU patch series**: `scripts/stage_xnu.sh` fetches the pinned
  `xnu-8796.141.3` tag, copies the overlay and applies two reviewed patches;
  CI stages a real tree and re-runs the staging to prove idempotence.  The
  patches apply cleanly but have not been compiled by an XNU build yet.
- **XnuLoader**: the Mach-O parser, a `boot_args` builder (`KarnakBootArgsLib`,
  layout asserted against Apple's xnu-8796 arm64 header) and a validation-only
  application exist; it prints the segment map and a `boot_args` preview from
  the shim's DTB, then refuses to boot.  The `ExitBootServices` / MMU-off
  handoff does not exist yet.
- **Disk-backed rootfs**: the GPT container is built and tested, but the HFS+
  payload (IPSW-derived Darwin tree) and the MSDC0 block driver are missing.
- **Touch/display panel drivers**: the panel is driven by LK; XNU framebuffer
  output is not yet wired.
- **PMIC/GPIO**: `MtkGpioImplLib` and `PmicWrapperImplLib` are bound to the
  MT6789 instances so the DSC is complete, but the drivers are not in the FV.
  A real MT8163 + MT6323 port is required for battery/keys.
- **DRAM size**: see `docs/HARDWARE.md`. Until a live DTB proves 1.5 GB, treat
  512 MiB as the safe floor.

## Hardware test readiness

The first on-device test is the UEFI stage only (`LK -> BootShim -> EDK2`):

- `BOOT_HEADER_VERSION=0` produces `out/uefi-karnak-lk.img` in the legacy
  layout the stock LK expects (2048-byte pages, explicit `kernel_addr` and
  `tags_addr`, both mandatory).
- `scripts/flash_karnak.sh` validates the image, the `karnak` product string,
  the unlock state and a stock boot-image backup before writing.
- Still blocked on the real `LK_KERNEL_ADDR`/`LK_TAGS_ADDR` values from the
  device's stock boot image; they are not guessed.
- `tools/boot_header.py boot-stock.img --shell` prints them (and the page
  size).  `scripts/build_lk_image.sh --stock boot-stock.img` then builds the
  v0 image, or dispatch the `uefi` workflow with the two addresses as inputs.

Expected on success: UEFI console/shell plus `KarnakInfoDxe` DTB/DRAM lines.
There is no XNU kernel and no internal eMMC driver yet.

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
