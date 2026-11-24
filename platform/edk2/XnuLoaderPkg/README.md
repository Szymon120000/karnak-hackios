# XnuLoader (design)

`XnuLoader` is the last UEFI stage before XNU.  The Mach-O half is implemented
and host-tested in `Library/XnuImageLib`; the handoff itself is deliberately
not written yet: a loader that cannot hand off correctly is worse than none,
and the XNU side (`xnu/README.md`) is not ready to receive control.

`XnuImageLib` validates the image (magic, CPU, filetype, every load command),
records the loadable segments, computes the file/VM footprint on 16 KiB
granularity, and checks a proposed physical destination against reserved
ranges.  Its tests live in `xnu/tests/xnu_image_test.c` and run in CI.

`KarnakBootArgsLib` builds the ARM64 `boot_args` block.  The layout comes from
Apple's `pexpert/pexpert/arm64/boot.h` (xnu-8796.141.3), vendored under
`xnu/include`; `xnu/tests/boot_args_test.c` asserts every field offset and the
736-byte size against that copy before the firmware build is allowed to use it.
It fills the block but does not exit boot services or branch.  The loader also
prints a *preview* built from the shim's saved DTB, which proves the pointer and
the layout on real hardware without risking a half-built handoff.

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
5. Build the XNU `boot_args` structure (ARM64 has no memory-map array in it;
   XNU learns memory from the device tree):
   - `DeviceTree` pointer and `DeviceTreeLength` (the shim's copy),
   - `Video` (base, width, height, stride, depth) from the GOP / DTB,
   - `CommandLine` from `\xnu\bootargs` or a built-in string,
   - `PhysBase`, `MemSize`, `MemSizeActual`, `TopOfKernelData` from the live
     memory map.  `KarnakBootArgsLib` implements and validates the block; the
     values still need to come from the memory map instead of the DTS floor.
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
- `xnu/include/pexpert/pexpert/arm64/boot.h` — vendored `boot_args` layout
  with provenance and license notes in `xnu/include/README.md`.

## Why it is not in the FV yet

`Karnak.fdf` intentionally omits `XnuLoader` until the `ExitBootServices` /
MMU-off entry path is implemented.  The parser and the `boot_args` builder are
complete and host-tested, so the remaining work is the handoff and the XNU board
port.  The firmware can still be flashed and exercised (shell, DT dump,
framebuffer) today.
