#!/usr/bin/env python3
#
# Minimal AOSP boot image writer (header v4, no GKI signature).
#
# The Android SDK's mkbootimg is not packaged for Ubuntu and the PyPI name is
# not available, so this keeps the CI self-contained.  The layout mirrors
# system/tools/mkbootimg/mkbootimg.py (write_header_v3_and_above) exactly:
#
#   magic "ANDROID!", kernel_size, ramdisk_size, os_version, header_size=1584,
#   4 reserved words, header_version, cmdline[1536], signature_size=0,
#   then header padded to 4096, then kernel/ramdisk padded to --pagesize.
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
HEADER_V4_SIZE = 1584
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


def main() -> int:
    ap = argparse.ArgumentParser(description="create an Android boot image (v4)")
    ap.add_argument("--kernel", required=True)
    ap.add_argument("--ramdisk")
    ap.add_argument("--second")
    ap.add_argument("--cmdline", default="")
    ap.add_argument("--output", required=True)
    ap.add_argument("--header_version", type=int, default=4, choices=[3, 4])
    ap.add_argument("--pagesize", type=int, default=2048)
    ap.add_argument("--os_version", type=lambda s: float(s), default=0.0)
    ap.add_argument("--os_patch_level", default="")
    args = ap.parse_args()

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

    header_size = HEADER_V4_SIZE if args.header_version >= 4 else 1580
    with open(args.output, "wb") as out:
        out.write(struct.pack("8s", BOOT_MAGIC))
        out.write(struct.pack("<I", filesize(args.kernel)))
        out.write(struct.pack("<I", filesize(args.ramdisk)))
        out.write(struct.pack("<I", (os_version << 11) | os_patch_level))
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

    print(f"wrote {args.output} ({os.path.getsize(args.output)} bytes, "
          f"header v{args.header_version})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
