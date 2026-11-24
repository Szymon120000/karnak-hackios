# karnak-hackios

Bring-up project for running a Darwin/XNU kernel on the **Amazon Fire HD 8
(8th generation, 2018)**, codename **karnak** — MediaTek MT8163, 4× Cortex-A53,
1.5 GB RAM, 800×1280 panel.

The target boot chain is:

```
LK  ->  KarnakBootShim  ->  EDK2 UEFI (KarnakPkg)  ->  XnuLoader  ->  XNU
```

Everything is built from source. The device tree in Amazon's 3.18 kernel is the
authority for addresses; generated values live in
`include/karnak/hardware_map.h` and carry source comments.

## Status

| Stage | Component | State |
|-------|-----------|-------|
| 0 | Hardware map from DTS | done, generated (`tools/extract_karnak_hw.py`) |
| 1 | KarnakBootShim (LK handoff, DTB save) | source complete |
| 2 | EDK2 `KarnakPkg` (UEFI firmware) | built in CI; LK v0 image + flash guard ready, hardware run pending |
| 3 | XnuLoader | Mach-O parser + `boot_args` builder done; handoff missing (see `xnu/README.md`) |
| 4 | Disk-backed rootfs / 1.5 GB tuning | scripts |

Read `docs/STATUS.md` before flashing anything: there is an unresolved
DRAM-size conflict between the shipped DTS (512 MiB) and the product (1.5 GB).

## Build the firmware

Requirements: `git`, `make`, `python3`, `clang`, `lld`, `llvm`,
`aarch64-linux-gnu-gcc`, `nasm`, `iasl`, `uuid-dev`.  Options: `dtc` and
Python `libfdt` (only for regenerating the hardware map).

```sh
./scripts/check_env.sh      # what is missing?
./build_uefi.sh             # fetches edk2-mtk, stages KarnakPkg, builds (DEBUG)
./build_uefi.sh RELEASE     # optimized image
```

The build drives EDK2's `build` tool directly; no `edk2-pytool`/`stuart`
environment is required.  It uses the `GCC5` toolchain by default (the one
MediaTek's tree is tested with); set `TOOLCHAIN=CLANG38` to try clang.

Output:

```
out/uefi-karnak.fd       # BootShim + EDK2 FD, the raw payload
out/uefi-karnak.img      # Android boot image, header v4 (CI/QEMU default)
out/uefi-karnak-lk.img   # Android boot image, header v0 (stock LK)
```

The stock 2018 LK does **not** understand header v4.  For a real device build,
ask for v0 and pass the LK load addresses that come from the stock boot image:

```sh
BOOT_HEADER_VERSION=0 \
LK_KERNEL_ADDR=0x... LK_TAGS_ADDR=0x... \
./build_uefi.sh DEBUG
```

Those addresses are deliberately mandatory: they are never guessed.  `v0` also
pads with the legacy 2048-byte pages expected by MediaTek LK.

CI runs the v4 build and the host tests on every push
(`.github/workflows/uefi.yml`).

## Repository layout

```
include/karnak/          generated hardware map (header + JSON sidecar)
tools/extract_karnak_hw.py  DTS -> hardware map generator
platform/edk2/KarnakPkg/ EDK2 platform package (UEFI, BootShim, drivers)
xnu/                     XNU board overlay and patch series
rootfs/                  disk-backed rootfs tooling
scripts/                 fetch/stage/probe/build helpers
docs/                    hardware citations and status
```

## Testing on the tablet (UEFI stage only)

Right now only stage 2 can run on hardware: `LK -> BootShim -> EDK2`.  There is
no XNU kernel yet, and UEFI has no internal eMMC driver, so expect the UEFI
console/shell and the `KarnakInfoDxe` DRAM/DTB lines, not a booting OS.

1. Unlock/root with `amonet-karnak` (this project does **not** ship an exploit
   or an unlock method) and make a stock backup:

   ```sh
   # from TWRP or a root shell
   dd if=/dev/block/by-name/boot of=/sdcard/boot-stock.img
   ```

2. Build the LK image with the addresses from the stock `boot.img`/LK (see
   above).  `tools/check_boot_image.py` validates the result.
3. Prefer the non-destructive path if the unlocked LK supports it:

   ```sh
   scripts/flash_karnak.sh out/uefi-karnak-lk.img --boot-test
   ```

   Otherwise flash, passing the backup so the script refuses to run without it:

   ```sh
   scripts/flash_karnak.sh out/uefi-karnak-lk.img --backup boot-stock.img
   ```

The script checks the image, the fastboot product (`karnak`), the unlock state
and the backup before it writes anything.  Recovery is restoring the stock
image to `/dev/block/by-name/boot` from TWRP.

Known blocker before the first hardware test: confirm `LK_KERNEL_ADDR` and
`LK_TAGS_ADDR` from the device's stock boot image.  Do not flash guessed
addresses.

## License

MIT, except `platform/edk2/KarnakPkg` which is BSD-2-Clause-Patent to match
EDK2. See `LICENSE` and file headers.
