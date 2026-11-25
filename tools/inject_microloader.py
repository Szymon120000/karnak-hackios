#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Wrap a boot image with the amonet microloader header.

karnak's bootloader unlock (amonet, by xyz`) takes over the first 0x400 bytes
of the boot and recovery partitions.  The patched LK runs that microloader,
which in turn makes LK see the *real* boot image at partition offset 0.

Layout written to the partition:

    0x000..0x400   microloader header (fixed per device/LK version)
    0x400..0x800   real boot image, first 0x400 bytes (its header)
    0x800..        real boot image from 0x800 (kernel, because page_size=2048)

The LK read hook ("demangle") moves 0x400..0x800 down to 0 and zeroes the gap,
so LK parses a normal boot image whose kernel sits at 0x800.

Flash the result only from TWRP/recovery: nothing else knows about the
microloader, and a boot image written without it can leave the device
unbootable until the bootrom steps are repeated.

The stub is *not* rebuilt here.  Take it from a working backup of the device's
own boot partition (``--stub-from boot-stock.img``); that guarantees it matches
the installed amonet/LK version.

    tools/inject_microloader.py --stub-from boot-stock.img \\
        boot-v0.img boot-injected.img [--partition-size 0x1000000]
"""

from __future__ import annotations

import argparse
import struct
import sys

BOOT_MAGIC = b"ANDROID!"
STUB_SIZE = 0x400
LEGACY_PAGE = 0x800        # amonet's inject script seeks to 0x800


def main(argv=None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("image", help="real boot image (header v0, page size 2048)")
    ap.add_argument("output", help="injected image to write to the partition")
    ap.add_argument("--stub-from",
                    help="working boot partition backup to take the 0x400-byte stub from")
    ap.add_argument("--stub", help="0x400-byte stub file instead of --stub-from")
    ap.add_argument("--partition-size", default="0x1000000",
                    help="pad the output to this size (karnak boot is 16 MiB)")
    args = ap.parse_args(argv)

    if bool(args.stub) == bool(args.stub_from):
        print("error: pass exactly one of --stub-from or --stub", file=sys.stderr)
        return 1

    if args.stub_from:
        with open(args.stub_from, "rb") as f:
            stub = f.read(STUB_SIZE)
        if len(stub) != STUB_SIZE:
            print(f"error: {args.stub_from} is shorter than 0x400", file=sys.stderr)
            return 1
        if stub[:8] != BOOT_MAGIC:
            print(f"error: {args.stub_from} does not start with an amonet stub",
                  file=sys.stderr)
            return 1
    else:
        stub = open(args.stub, "rb").read()
        if len(stub) != STUB_SIZE:
            print(f"error: stub must be exactly 0x400 bytes, got {len(stub)}",
                  file=sys.stderr)
            return 1

    src = open(args.image, "rb").read()
    if len(src) < LEGACY_PAGE or src[:8] != BOOT_MAGIC:
        print(f"error: {args.image} is not an Android boot image", file=sys.stderr)
        return 1

    header_version, = struct.unpack_from("<I", src, 40)
    page_size, = struct.unpack_from("<I", src, 36)
    kernel_addr, = struct.unpack_from("<I", src, 12)
    kernel_size, = struct.unpack_from("<I", src, 8)
    if header_version != 0:
        print(f"error: injection expects header v0, got v{header_version}",
              file=sys.stderr)
        return 1
    if page_size != 2048:
        # The demangle hook assumes the kernel follows at 0x800.
        print(f"error: injection requires page_size 2048, got {page_size}",
              file=sys.stderr)
        return 1
    if kernel_addr == 0 or kernel_size == 0:
        print("error: image has no kernel address/size", file=sys.stderr)
        return 1

    out = bytearray()
    out += stub
    out += src[:STUB_SIZE]
    out += src[LEGACY_PAGE:]

    partition_size = int(args.partition_size, 0)
    if len(out) > partition_size:
        print(f"error: image is {len(out)} bytes, larger than the "
              f"{partition_size}-byte partition", file=sys.stderr)
        return 1
    out += b"\x00" * (partition_size - len(out))

    # What LK will parse after the read hook demangles the partition.
    demangled = (out[STUB_SIZE:LEGACY_PAGE] + b"\x00" * (LEGACY_PAGE - STUB_SIZE)
                 + out[LEGACY_PAGE:])[:len(src)]
    if demangled[:STUB_SIZE] != src[:STUB_SIZE] or demangled[LEGACY_PAGE:] != src[LEGACY_PAGE:]:
        print("error: demangled image does not match the input", file=sys.stderr)
        return 1

    with open(args.output, "wb") as f:
        f.write(out)

    print(f"wrote {args.output} ({len(out)} bytes, partition)")
    print(f"  stub     {len(stub)} bytes from "
          f"{args.stub_from or args.stub}")
    print(f"  image    kernel_addr=0x{kernel_addr:08x} kernel_size=0x{kernel_size:x}")
    print("  warning: flash from recovery/TWRP only")
    return 0


if __name__ == "__main__":
    sys.exit(main())
