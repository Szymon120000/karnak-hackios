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
- **DRAM size**: the live DTB on a retail karnak reports two banks,
  `0x40000000+0x40000000` and `0x80000000+0x19600000` (top `0x99600000`,
  ~1.4 GiB usable), so the 512 MiB DTS value is only the build-time floor.
- **Framebuffer**: `/chosen:atag,videolfb` on the same device gives
  `fb_base=0x98800000`, `vram=0x00e00000`.  SimpleFbDxe is still driven by a
  fixed PCD pinned to those verified values; it should read the property.

## Hardware test readiness

The first on-device test is the UEFI stage only (`LK -> BootShim -> EDK2`):

- `BOOT_HEADER_VERSION=0` produces `out/uefi-karnak-lk.img` in the legacy
  layout the stock LK expects (2048-byte pages, explicit `kernel_addr` and
  `tags_addr`, both mandatory).
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

Expected on success: a red shim band followed by the UEFI console/shell plus
`KarnakInfoDxe` DTB/DRAM lines.  There is no XNU kernel and no internal eMMC
driver yet.

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
