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
| 2 | EDK2 `KarnakPkg` (UEFI firmware) | source complete, CI build |
| 3 | XnuLoader | design + scaffolding (see `xnu/README.md`) |
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
out/uefi-karnak.fd     # BootShim + EDK2 FD, flashable/loadable payload
out/uefi-karnak.img    # Android boot image (header v4) wrapping the above
```

CI runs the same steps on every push (`.github/workflows/uefi.yml`).

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

## Flashing (once a boot.img exists)

The Fire HD 8 uses Amazon's locked LK. Unlock/root first (amonet-karnak), then
write the image to the `boot` partition with `fastboot` or `dd` from recovery.
This project does **not** ship an exploit or an unlock method.

## License

MIT, except `platform/edk2/KarnakPkg` which is BSD-2-Clause-Patent to match
EDK2. See `LICENSE` and file headers.
