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

- **XNU**: only the board header and patch scaffolding exist. A bootable XNU
  needs the full sweet/beyond2lte patch series adapted to MT8163 (GICv2,
  A53 errata, no UFS, MSDC0 instead of UFS, different PMIC). Tracked in
  `xnu/README.md`.
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
