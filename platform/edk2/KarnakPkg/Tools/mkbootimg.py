#!/usr/bin/env python3
#
# Minimal AOSP boot image writer.
#
# The Android SDK's mkbootimg is not packaged for Ubuntu and the PyPI name is
# not available, so this keeps the CI self-contained.
#
#   * header v3/v4: modern layout (header padded to 4096, no legacy addresses).
#   * header v0:    what a 2017-2018 MediaTek LK (Fire OS 6 / karnak) expects.
#                   The LK load addresses are *not* guessed: --kernel_addr and
#                   --tags_addr are mandatory for this mode.
#
# v1/v2 are intentionally not implemented until a stock karnak boot.img proves
# which variant the device uses.  The legacy layout mirrors
# system/tools/mkbootimg/include/bootimg/bootimg.h.
#
# SPDX-License-Identifier: Apache-2.0
#
from __future__ import annotations

import argparse
import os
import struct

BOOT_MAGIC = b"ANDROID!"
BOOT_ARGS_SIZE = 512
BOOT_EXTRA_ARGS_SIZE = 1024
HEADER_V3_SIZE = 1580
HEADER_V4_SIZE = 1584
HEADER_V0_SIZE = 1632
HEADER_PAGE = 4096


def filesize(path: str | None) -> int:
    return os.path.getsize(path) if path else 0


def pad_to(out, alignment: int) -> None:
    pad = (alignment - (out.tell() & (alignment - 1))) & (alignment - 1)
    if pad:
        out.write(b"\x00" * pad)


def write_padded(out, path: str | None, alignment: int) -> None:
    if not path:
        return
    with open(path, "rb") as f:
        while True:
            chunk = f.read(1024 * 1024)
            if not chunk:
                break
            out.write(chunk)
    pad_to(out, alignment)


def write_legacy_header(out, args, cmdline: bytes, os_version: int) -> None:
    """boot_img_hdr_v0, followed by page-aligned kernel/ramdisk/second."""
    page_size = args.pagesize
    out.write(struct.pack(
        "<8sIIIIIIIIII",
        BOOT_MAGIC,
        filesize(args.kernel),
        args.kernel_addr,
        filesize(args.ramdisk),
        args.ramdisk_addr,
        filesize(args.second),
        args.second_addr,
        args.tags_addr,
        page_size,
        0,                       # header_version
        os_version,
    ))
    out.write(struct.pack("<16s", args.name.encode()[:16].ljust(16, b"\x00")))
    out.write(struct.pack("<512s", cmdline[:BOOT_ARGS_SIZE]))
    out.write(struct.pack("<8I", 0, 0, 0, 0, 0, 0, 0, 0))   # id[], unused by LK
    out.write(struct.pack("<1024s", cmdline[BOOT_ARGS_SIZE:BOOT_ARGS_SIZE +
                                            BOOT_EXTRA_ARGS_SIZE]))
    assert out.tell() == HEADER_V0_SIZE, out.tell()
    pad_to(out, page_size)

    write_padded(out, args.kernel, page_size)
    write_padded(out, args.ramdisk, page_size)
    write_padded(out, args.second, page_size)


def write_v3_header(out, args, cmdline: bytes, os_version: int) -> None:
    header_size = HEADER_V4_SIZE if args.header_version >= 4 else HEADER_V3_SIZE
    out.write(struct.pack("8s", BOOT_MAGIC))
    out.write(struct.pack("<I", filesize(args.kernel)))
    out.write(struct.pack("<I", filesize(args.ramdisk)))
    out.write(struct.pack("<I", os_version))
    out.write(struct.pack("<I", header_size))
    out.write(struct.pack("<4I", 0, 0, 0, 0))
    out.write(struct.pack("<I", args.header_version))
    out.write(struct.pack("<1536s", cmdline))
    if args.header_version >= 4:
        out.write(struct.pack("<I", 0))  # signature_size
    pad_to(out, HEADER_PAGE)

    write_padded(out, args.kernel, args.pagesize)
    write_padded(out, args.ramdisk, args.pagesize)
    write_padded(out, args.second, args.pagesize)


def main() -> int:
    ap = argparse.ArgumentParser(
        description="create an Android boot image (v0, v3 or v4)")
    ap.add_argument("--kernel", required=True)
    ap.add_argument("--ramdisk")
    ap.add_argument("--second")
    ap.add_argument("--cmdline", default="")
    ap.add_argument("--output", required=True)
    ap.add_argument("--header_version", type=int, default=4)
    ap.add_argument("--pagesize", type=int, default=2048)
    ap.add_argument("--os_version", type=lambda s: float(s), default=0.0)
    ap.add_argument("--os_patch_level", default="")

    legacy = ap.add_argument_group("legacy header v0")
    legacy.add_argument("--kernel_addr", type=lambda s: int(s, 0),
                        help="physical load address for the kernel (required for v0)")
    legacy.add_argument("--tags_addr", type=lambda s: int(s, 0),
                        help="tags/DTB address LK uses (required for v0)")
    legacy.add_argument("--ramdisk_addr", type=lambda s: int(s, 0), default=0)
    legacy.add_argument("--second_addr", type=lambda s: int(s, 0), default=0)
    legacy.add_argument("--name", default="")

    args = ap.parse_args()

    if args.header_version not in (0, 3, 4):
        raise SystemExit(
            f"header v{args.header_version} is not implemented; use 0, 3 or 4")
    if args.header_version == 0:
        if args.kernel_addr is None or args.tags_addr is None:
            raise SystemExit(
                "legacy header v0 needs explicit --kernel_addr and --tags_addr "
                "(take them from the stock boot.img / LK, do not guess)")
        if args.kernel_addr == 0 or args.tags_addr == 0:
            raise SystemExit("--kernel_addr and --tags_addr must be non-zero")
        if args.pagesize < 2048 or (args.pagesize & (args.pagesize - 1)):
            raise SystemExit("--pagesize must be a power of two >= 2048")

    cmdline = args.cmdline.encode()
    if len(cmdline) > BOOT_ARGS_SIZE + BOOT_EXTRA_ARGS_SIZE:
        raise SystemExit("cmdline too long")

    os_version = 0
    os_patch_level = 0
    if args.os_version:
        major, minor = str(args.os_version).split(".")
        os_version = (int(major) << 7) | int(minor)
    if args.os_patch_level:
        year, month = args.os_patch_level.split("-")
        os_patch_level = ((int(year) - 2000) << 4) | int(month)
    os_version = (os_version << 11) | os_patch_level

    with open(args.output, "wb") as out:
        if args.header_version == 0:
            write_legacy_header(out, args, cmdline, os_version)
        else:
            write_v3_header(out, args, cmdline, os_version)

    print(f"wrote {args.output} ({os.path.getsize(args.output)} bytes, "
          f"header v{args.header_version})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
