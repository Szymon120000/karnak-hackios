# XnuLoader

`XnuLoader` is the last UEFI stage before XNU.  The Mach-O parser, the
`boot_args` builder and the `ExitBootServices` / MMU-off handoff are
implemented; a kernel built for karnak does not exist yet, so the loader still
has nothing real to boot.

`XnuImageLib` validates the image (magic, CPU, filetype, every load command),
records the loadable segments, computes the file/VM footprint on 16 KiB
granularity, and checks a proposed physical destination against reserved
ranges.  Its tests live in `xnu/tests/xnu_image_test.c` and run in CI.

`KarnakBootArgsLib` builds the ARM64 `boot_args` block.  The layout comes from
Apple's `pexpert/pexpert/arm64/boot.h` (xnu-8796.141.3), vendored under
`xnu/include`; `xnu/tests/boot_args_test.c` asserts every field offset and the
736-byte size against that copy before the firmware build is allowed to use it.

`KarnakHandoffLib` owns the risky part:

- `KarnakHandoffExitBootServices` takes the memory map and retries the
  `GetMemoryMap` / `ExitBootServices` key race.
- `KarnakXnuHandoff` cleans the kernel, `boot_args`, loader and DTB ranges to
  the point of coherency, invalidates their I-cache lines, turns the MMU and
  caches off, and branches with `x0 = boot_args`.
- karnak's UEFI runs at **EL2** (MediaTek PrePi installs `HCR_EL2.TGE`), while
  XNU expects **EL1**.  The handoff detects the level and, at EL2, clears
  `TGE`/`E2H`, programs an EL1 state with `SCTLR_EL1.M/C/I = 0`, and drops
  through `ELR_EL2`/`SPSR_EL2`.  At EL1 it only disables translation/caches.

`KarnakHandoffTestPayload` is the hardware proof.  It is entered with the MMU
and caches off, uses no stack, and paints the panel directly:

| panel | meaning |
|---|---|
| white top 200 rows | the payload ran after `ExitBootServices` |
| green (rest) | `boot_args` valid, running at EL1 |
| yellow | `boot_args` valid, still at EL2 (drop failed) |
| magenta | unexpected EL3 |
| red | `boot_args` revision/version wrong |
| cyan | `ExitBootServices` was attempted but the payload never ran |

`KarnakProbeDxe` runs this test at `ReadyToBoot` when
`PcdKarnakHandoffSelfTest=TRUE` (bringing the boot to an end instead of
starting the Shell).  **Verified on hardware:** white + green, so
`ExitBootServices`, the EL2 -> EL1 drop, MMU/cache-off and direct framebuffer
writes all work on this tablet.

`Application/XnuLoader` is built by `Karnak.dsc` but **not** placed in the FV.
When a `\xnu\kernelcache` exists on the boot volume it parses the image,
allocates a destination from the UEFI allocator, copies every loadable segment,
builds `boot_args` (DTB from the shim, video from GOP, DRAM from the DTS floor),
and calls the same handoff.  It returns `EFI_UNSUPPORTED` without a karnak
kernel; it no longer refuses unconditionally.

## Contract with the firmware

KarnakPkg guarantees, before `XnuLoader.efi` runs:

- `DtPlatformDxe` has published the live DTB (via `KarnakDtbLoaderLib`),
- DRAM and firmware reservations are in the UEFI memory map,
- `KarnakFbDxe` has a GOP with the LK framebuffer (800x1280, RGBA),
- UART0 is usable for `kprintf` output at 921600.

## Remaining work

- A karnak XNU board port (`xnu/`), built with Apple's toolchain.
- Live DRAM size in `boot_args` (`KARN_DRAM_SIZE_DTS` is the 512 MiB floor; the
  retail device is 1.5 GiB).
- A `\xnu\bootargs` file for the kernel command line instead of the built-in
  string.
- Putting `XnuLoader` in the FV and creating a boot option once a kernel
  exists.

## References

- `beyond2lte-ios/uefi/XnuLoader/` — Exynos 9820 loader (GICv2, 16K pages).
- `ios-sweet/shim/` — bare-metal arm64 entry, MMU setup, FDT parsing.
- `xnu/include/pexpert/pexpert/arm64/boot.h` — vendored `boot_args` layout
  with provenance and license notes in `xnu/include/README.md`.
