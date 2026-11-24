#!/usr/bin/env python3
"""Structural check of a karnak boot image (BootShim + EDK2 FD).

Verifies the pieces that are easy to get subtly wrong and that no compiler will
catch:

  * Android boot image, header v0 (LK) or v3/v4 (modern), with kernel_size
    matching the payload;
  * for v0, a sane page size and non-zero LK load addresses;
  * the payload starts with the KarnakBootShim branch and ARM64 Image magic;
  * the Image header's image_size covers the whole FD;
  * the FD starts with an EDK2 firmware volume whose length matches the
    configured FD size, and whose FV header checksum is valid.

Exit status is 0 on success, 1 with a list of failures otherwise.

    tools/check_boot_image.py out/uefi-karnak.img [--fd-size 0x700000]
"""

from __future__ import annotations

import argparse
import struct
import sys

BOOT_MAGIC = b"ANDROID!"
ARM64_MAGIC = b"ARM\x64"
FV_SIG = b"_FVH"
MODERN_HEADER_PAGE = 4096              # v3/v4 header is padded to this
SHIM_PAYLOAD_OFFSET = 0x140
LEGACY_VERSIONS = (0, 1, 2)


def header_sum16(data: bytes) -> int:
    """16-bit sum of little-endian words (EFI FV header checksum)."""
    if len(data) % 2:
        data = data + b"\x00"
    return sum(struct.unpack_from("<H", data, i)[0] for i in range(0, len(data), 2))


def main(argv=None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("--fd-size", default="0x700000")
    ap.add_argument("--fd-base", default="0x50000000")
    args = ap.parse_args(argv)

    fd_size = int(args.fd_size, 0)
    data = open(args.image, "rb").read()
    errors = []

    def check(cond, msg):
        if not cond:
            errors.append(msg)

    check(len(data) >= 64, "image is smaller than a boot header")
    check(data[:8] == BOOT_MAGIC, "missing ANDROID! magic")
    if data[:8] != BOOT_MAGIC:
        for e in errors:
            print(f"FAIL: {e}", file=sys.stderr)
        return 1

    kernel_size, = struct.unpack_from("<I", data, 8)
    # header_version is at offset 40 in both the legacy and modern layouts.
    header_version, = struct.unpack_from("<I", data, 40)

    if header_version in LEGACY_VERSIONS:
        page_size, = struct.unpack_from("<I", data, 36)
        kernel_addr, = struct.unpack_from("<I", data, 12)
        tags_addr, = struct.unpack_from("<I", data, 32)
        check(2048 <= page_size <= 65536 and not page_size & (page_size - 1),
              f"legacy page_size {page_size} is not a power of two in 2048..65536")
        check(kernel_addr != 0, "legacy kernel_addr is zero")
        check(tags_addr != 0, "legacy tags_addr is zero")
        payload_offset = page_size
    elif header_version in (3, 4):
        payload_offset = MODERN_HEADER_PAGE
    else:
        print(f"FAIL: unsupported header_version {header_version}", file=sys.stderr)
        return 1

    check(payload_offset + kernel_size <= len(data),
          "kernel_size runs past the end of the file")
    payload = data[payload_offset:payload_offset + kernel_size]
    trailing = data[payload_offset + kernel_size:]
    check(len(trailing) < max(payload_offset, 4096) and trailing == b"\x00" * len(trailing),
          "unexpected non-padding data after the kernel payload")

    check(len(payload) > SHIM_PAYLOAD_OFFSET + 32, "payload too small for BootShim")
    if len(payload) > SHIM_PAYLOAD_OFFSET + 32:
        check(payload[0x38:0x3C] == ARM64_MAGIC,
              "BootShim does not expose the arm64 Image magic at offset 0x38")
        branch, = struct.unpack_from("<I", payload, 0)
        check((branch & 0xFC000000) == 0x14000000,
              "payload does not start with a branch")
        image_size, = struct.unpack_from("<Q", payload, 0x10)
        check(image_size == fd_size + 0x1000,
              f"Image image_size {image_size:#x} != FD size + 4 KiB "
              f"({fd_size + 0x1000:#x})")
        check(kernel_size == SHIM_PAYLOAD_OFFSET + fd_size,
              f"kernel_size {kernel_size:#x} != BootShim + FD "
              f"({SHIM_PAYLOAD_OFFSET + fd_size:#x})")

    fd = payload[SHIM_PAYLOAD_OFFSET:]
    check(len(fd) == fd_size, f"FD is {len(fd):#x} bytes, expected {fd_size:#x}")
    check(len(fd) > 0x30, "FD too small")
    check(fd[0x28:0x2C] == FV_SIG, "no _FVH signature at FD offset 0x28")
    if fd[0x28:0x2C] == FV_SIG:
        # The FV header starts at FD offset 0: ZeroVector aliases the reset
        # branch, so FvLength is at 0x20 and Signature at 0x28.
        fv_len, = struct.unpack_from("<Q", fd, 0x20)
        check(fv_len == fd_size, f"FV length {fv_len:#x} != {fd_size:#x}")
        hdr_len, = struct.unpack_from("<H", fd, 0x30)
        # EFI computes the FV checksum by summing 16-bit words; a valid header
        # sums to zero when the Checksum field is included.
        check(header_sum16(fd[0:hdr_len]) & 0xFFFF == 0,
              "FV header checksum is not zero-sum")

    if errors:
        for e in errors:
            print(f"FAIL: {e}", file=sys.stderr)
        return 1

    print(f"{args.image}: ok (v{header_version}, {len(data)} bytes, "
          f"FD {fd_size // 1024} KiB)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
