# XnuLoader (design)

`XnuLoader` is the last UEFI stage before XNU.  The Mach-O half is implemented
and host-tested in `Library/XnuImageLib`; the handoff itself is deliberately
not written yet: a loader that cannot hand off correctly is worse than none,
and the XNU side (`xnu/README.md`) is not ready to receive control.

`XnuImageLib` validates the image (magic, CPU, filetype, every load command),
records the loadable segments, computes the file/VM footprint on 16 KiB
granularity, and checks a proposed physical destination against reserved
ranges.  Its tests live in `xnu/tests/xnu_image_test.c` and run in CI.

`Application/XnuLoader` is built by `Karnak.dsc` but **not** placed in the FV.
It opens `\xnu\kernelcache` on the boot volume, runs it through the parser and
prints the segment map, then returns `EFI_UNSUPPORTED`.  That makes the file
path, FAT driver and parser testable on hardware without risking a half-built
handoff.

This document records the design and the concrete work items so the next
session can implement it against a known-good firmware.

## Contract with the firmware

KarnakPkg guarantees, before `XnuLoader.efi` runs:

- `DtPlatformDxe` has published the live DTB (via `KarnakDtbLoaderLib`),
- DRAM and firmware reservations are in the UEFI memory map,
- `SimpleFbDxe` has a GOP with the LK framebuffer (800×1280, BGRA),
- UART0 is usable for `kprintf` output at 921600.

`XnuLoader` should:

1. Open the boot volume's `\xnu\kernelcache` (or a raw `\xnu\kernel`).
2. Validate the Mach-O 64 header (`MH_MAGIC_64`, CPU `CPU_TYPE_ARM64`).
3. Walk `LC_SEGMENT_64` commands and record `vmaddr`, `vmsize`, `fileoff`,
   `filesize`, `maxprot/initprot`.
4. Reject any segment that intersects a firmware reservation or the DTB copy
   window (`0x4fe00000..0x50000000`).
5. Build the XNU `boot_args` structure:
   - `MemoryMap` / `MemoryMapSize` from `GetMemoryMap`,
   - `DeviceTree` pointer and `DeviceTreeLength`,
   - `Video` (base, width, height, stride, depth) from the GOP / DTB,
   - `CommandLine` from `\xnu\bootargs` or a built-in string,
   - `TopOfKernelData`, `PhysicalMemorySize` from the memory map.
6. `ExitBootServices`, disable the MMU/caches, copy segments to their link
   addresses, and branch to the entry point with `x0 = &boot_args`.

## The hard part: virtual addresses

XNU's ARM64 kernel is linked at a high VA (`0xfffffff007004000` on recent
kernels).  UEFI runs with its own identity map.  The loader must either:

- copy the kernel to a physical address and enter it with the MMU off, letting
  XNU's early boot code install its own tables (the approach used by
  `ios-sweet/shim` and `beyond2lte-ios/uefi/XnuLoader`), or
- build the kernel's initial translation tables in the loader.

For karnak the first option is preferred: it keeps the loader small and matches
the `ios-sweet/shim/start.S` handoff.

## References

- `beyond2lte-ios/uefi/XnuLoader/` — Exynos 9820 loader (GICv2, 16K pages).
- `ios-sweet/shim/` — bare-metal arm64 entry, MMU setup, FDT parsing.
- `xnu/osfmk/arm64/boot.h` and `pexpert/pexpert/arm64/boot.h` — `boot_args`.

## Why it is not in the FV yet

`Karnak.fdf` intentionally omits `XnuLoader` until the Mach-O parser and
`boot_args` builder are complete and host-tested.  The firmware can still be
flashed and exercised (shell, DT dump, framebuffer) today.
