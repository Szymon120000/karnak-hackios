#!/usr/bin/env python3
"""Read an Android boot image header and print the fields LK needs.

The stock Fire HD 8 boot image is the authority for the addresses karnak's
legacy v0 image must carry.  Never guess them; point this at the stock
boot.img (the one backed up before any flash):

    python3 tools/boot_header.py boot-stock.img
    python3 tools/boot_header.py boot-stock.img --shell   # export LK_* lines

Supports header v0, v1, v2 (legacy, page-aligned) and v3/v4 (modern, 4096-byte
header).  For v3/v4 there are no load addresses in the header: LK derives them
itself, so a v0 image cannot be built from such a stock image.  The tool says
so instead of inventing numbers.
"""

from __future__ import annotations

import argparse
import struct
import sys

BOOT_MAGIC = b"ANDROID!"
LEGACY_VERSIONS = (0, 1, 2)


def be32(data: bytes, offset: int) -> int:
    return struct.unpack_from(">I", data, offset)[0]


def main(argv=None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("--shell", action="store_true",
                    help="print LK_KERNEL_ADDR/LK_TAGS_ADDR exports")
    args = ap.parse_args(argv)

    data = open(args.image, "rb").read()
    if len(data) < 44 or data[:8] != BOOT_MAGIC:
        print(f"error: {args.image} is not an Android boot image", file=sys.stderr)
        return 1

    # amonet (the karnak bootloader unlock) leaves a 0x400-byte microloader
    # stub at offset 0 and puts the real boot image at 0x400.  Detect it by
    # the stub's implausible v0 page size rather than assuming a layout.
    base = 0
    kernel_size, = struct.unpack_from("<I", data, 8)
    header_version, = struct.unpack_from("<I", data, 40)
    if header_version in LEGACY_VERSIONS:
        page_size, = struct.unpack_from("<I", data, 36)
        if (page_size < 2048 or page_size & (page_size - 1)) \
                and len(data) >= 0x400 + 44 and data[0x400:0x408] == BOOT_MAGIC:
            base = 0x400
            kernel_size, = struct.unpack_from("<I", data, base + 8)
            header_version, = struct.unpack_from("<I", data, base + 40)
            print("note: amonet microloader stub at 0x0; "
                  "reading the real header at 0x400")

    print(f"image            {args.image}")
    print(f"size             {len(data)} bytes")
    print(f"header_version   {header_version}")
    print(f"kernel_size      {kernel_size} (0x{kernel_size:x})")

    if header_version in LEGACY_VERSIONS:
        kernel_addr, = struct.unpack_from("<I", data, base + 12)
        ramdisk_size, = struct.unpack_from("<I", data, base + 16)
        ramdisk_addr, = struct.unpack_from("<I", data, base + 20)
        second_size, = struct.unpack_from("<I", data, base + 24)
        second_addr, = struct.unpack_from("<I", data, base + 28)
        tags_addr, = struct.unpack_from("<I", data, base + 32)
        page_size, = struct.unpack_from("<I", data, base + 36)
        name = data[base + 48:base + 64].split(b"\x00", 1)[0].decode(errors="replace")
        cmdline = data[base + 64:base + 64 + 512].split(b"\x00", 1)[0].decode(errors="replace")

        print(f"page_size        {page_size}")
        print(f"kernel_addr      0x{kernel_addr:08x}")
        print(f"tags_addr        0x{tags_addr:08x}")
        print(f"ramdisk_addr     0x{ramdisk_addr:08x}  (size {ramdisk_size})")
        print(f"second_addr      0x{second_addr:08x}  (size {second_size})")
        print(f"name             {name!r}")
        print(f"cmdline          {cmdline!r}")

        if header_version >= 1:
            recovery_dtbo_size, = struct.unpack_from("<I", data, 1632)
            recovery_dtbo_offset, = struct.unpack_from("<Q", data, 1636)
            header_size, = struct.unpack_from("<I", data, 1644)
            print(f"recovery_dtbo    0x{recovery_dtbo_offset:x} (size {recovery_dtbo_size})")
            print(f"header_size      {header_size}")
        if header_version >= 2:
            dtb_size, = struct.unpack_from("<I", data, 1648)
            dtb_addr, = struct.unpack_from("<Q", data, 1652)
            print(f"dtb_addr         0x{dtb_addr:x} (size {dtb_size})")

        if kernel_addr == 0 or tags_addr == 0:
            print("error: legacy header has a zero load address; this image "
                  "cannot tell us where LK loads a kernel", file=sys.stderr)
            return 1
        if page_size < 2048 or page_size & (page_size - 1):
            print(f"error: implausible page_size {page_size}", file=sys.stderr)
            return 1

        if args.shell:
            print()
            print(f"LK_KERNEL_ADDR=0x{kernel_addr:08x}")
            print(f"LK_TAGS_ADDR=0x{tags_addr:08x}")
            print(f"LK_PAGE_SIZE={page_size}")
        return 0

    if header_version in (3, 4):
        print("page_size        4096 (fixed)")
        print()
        print("This is a modern (v3/v4) image: its header carries no kernel or")
        print("tags load address, so it cannot be used to derive LK_KERNEL_ADDR /")
        print("LK_TAGS_ADDR.  Use the device's stock boot image (v0/v1/v2) or the")
        print("LK sources/console for those values.")
        return 2

    print(f"error: unsupported header_version {header_version}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
