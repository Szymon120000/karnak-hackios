#!/usr/bin/env python3
"""Extract the device tree blob appended to an Android boot image kernel.

karnak's 2018 MediaTek LK refuses to boot a payload unless it can find an FDT
in the kernel payload: it decompresses the kernel and then looks for a DTB
right after the (possibly gzip-compressed) image.  Without one it logs
"Can't find device tree. Please check your kernel image", never jumps, and the
SoC resets when LK's watchdog expires.

The stock boot image carries the authoritative DTB for the device, so build
scripts extract it from the pre-flash backup instead of compiling a DTS:

    python3 tools/extract_dtb.py device-backup/boot-stock.img out/karnak.dtb

Supports amonet-injected boot partitions (real header at 0x400) and plain v0
images, gzip-compressed or raw kernels.
"""

from __future__ import annotations

import argparse
import struct
import sys
import zlib

BOOT_MAGIC = b"ANDROID!"
FDT_MAGIC = b"\xd0\x0d\xfe\xed"
HEADER_V0_SIZE = 1632
LEGACY_VERSIONS = (0, 1, 2)


def find_header(data: bytes) -> tuple[int, int, int]:
    """Return (header_offset, page_size, kernel_size) for a v0..v2 image."""
    base = 0
    if data[0:8] != BOOT_MAGIC:
        # amonet keeps its stub in the first page and the real header at 0x400.
        if data[0x400:0x408] != BOOT_MAGIC:
            raise ValueError("not an Android boot image")
        base = 0x400

    if len(data) < base + 44:
        raise ValueError("boot image header is truncated")

    header_version, = struct.unpack_from("<I", data, base + 40)
    if header_version not in LEGACY_VERSIONS:
        raise ValueError(f"header v{header_version} has no page-aligned kernel")

    page_size, = struct.unpack_from("<I", data, base + 36)
    if (page_size < 2048 or page_size & (page_size - 1)) \
            and base == 0 and data[0x400:0x408] == BOOT_MAGIC:
        base = 0x400
        page_size, = struct.unpack_from("<I", data, base + 36)

    if page_size < 2048 or page_size & (page_size - 1):
        raise ValueError(f"implausible page size {page_size}")

    kernel_size, = struct.unpack_from("<I", data, base + 8)
    return base, page_size, kernel_size


def kernel_offset(base: int, page_size: int) -> int:
    """Offset of the kernel relative to the header page.

    For an amonet-injected partition the first page holds the stub and the
    real header starts at 0x400; the kernel still begins at the next page
    (0x800 for the 2048-byte pages karnak uses).  For a plain image the kernel
    follows the header page.
    """
    if base:
        return base - (base % page_size) + page_size
    return page_size


def extract_dtb(data: bytes) -> bytes:
    base, page_size, kernel_size = find_header(data)
    start = kernel_offset(base, page_size)
    kernel = data[start:start + kernel_size]
    if len(kernel) != kernel_size:
        raise ValueError("boot image is truncated inside the kernel")

    tail = kernel
    if kernel[:2] == b"\x1f\x8b":
        # Decompress only the first gzip member: the appended DTB follows it
        # as plain bytes, which is why gzip.decompress() rejects the buffer.
        inflater = zlib.decompressobj(16 + zlib.MAX_WBITS)
        try:
            image = inflater.decompress(kernel)
            inflater.flush()
        except zlib.error as exc:
            raise ValueError(f"kernel gzip stream is corrupt: {exc}") from exc
        tail = inflater.unused_data
        if FDT_MAGIC not in tail and FDT_MAGIC in image:
            tail = image

    index = tail.find(FDT_MAGIC)
    if index < 0:
        raise ValueError("no appended device tree blob found")
    if index + 8 > len(tail):
        raise ValueError("device tree header is truncated")
    total, = struct.unpack_from(">I", tail, index + 4)
    if total < 8 or index + total > len(tail):
        raise ValueError(f"device tree size {total} does not fit in the kernel")
    return tail[index:index + total]


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("image", help="boot image with an appended DTB")
    ap.add_argument("output", nargs="?", help="where to write the DTB")
    ap.add_argument("--print-size", action="store_true",
                    help="only report the size")
    args = ap.parse_args(argv)

    try:
        dtb = extract_dtb(open(args.image, "rb").read())
    except (OSError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    if args.print_size:
        print(len(dtb))
        return 0
    if not args.output:
        print("error: pass an output path or --print-size", file=sys.stderr)
        return 1

    with open(args.output, "wb") as f:
        f.write(dtb)
    print(f"wrote {args.output} ({len(dtb)} bytes FDT from {args.image})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
