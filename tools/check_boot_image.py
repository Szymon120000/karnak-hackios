#!/usr/bin/env python3
"""Structural check of out/uefi-karnak.img (BootShim + EDK2 FD).

Verifies the pieces that are easy to get subtly wrong and that no compiler will
catch:

  * Android boot image header v4, with kernel_size matching the payload;
  * the payload starts with the KarnakBootShim branch and ARM64 Image magic;
  * the Image header's image_size covers the whole FD;
  * the FD starts with an EDK2 firmware volume whose length matches the
    configured FD size, and whose FV header CRC is correct.

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
BOOT_HEADER_V4_SIZE = 4096           # fixed for header_version 4
SHIM_HEADER_SIZE = 0x40
SHIM_PAYLOAD_OFFSET = 0x140


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

    check(len(data) > BOOT_HEADER_V4_SIZE, "image is smaller than a v4 boot header")
    check(data[:8] == BOOT_MAGIC, "missing ANDROID! magic")
    kernel_size, = struct.unpack_from("<I", data, 8)
    check(40 <= len(data), "header truncated")
    header_version, = struct.unpack_from("<I", data, 40)
    check(header_version == 4, f"header_version is {header_version}, expected 4")

    payload = data[BOOT_HEADER_V4_SIZE:BOOT_HEADER_V4_SIZE + kernel_size]
    check(len(payload) == kernel_size, "kernel_size runs past the end of the file")
    check(data[BOOT_HEADER_V4_SIZE + kernel_size:] == b"\x00" * (len(data) - BOOT_HEADER_V4_SIZE - kernel_size),
          "unexpected data after the kernel payload")

    check(len(payload) > SHIM_PAYLOAD_OFFSET + 32, "payload too small for BootShim")
    check(payload[0x38:0x3C] == ARM64_MAGIC,
          "BootShim does not expose the arm64 Image magic at offset 0x38")
    branch, = struct.unpack_from("<I", payload, 0)
    check((branch & 0xFC000000) == 0x14000000, "payload does not start with a branch")
    image_size, = struct.unpack_from("<Q", payload, 0x10)
    check(image_size == fd_size + 0x1000,
          f"Image image_size {image_size:#x} != FD size + 4 KiB ({fd_size + 0x1000:#x})")
    check(kernel_size == SHIM_PAYLOAD_OFFSET + fd_size,
          f"kernel_size {kernel_size:#x} != BootShim + FD ({SHIM_PAYLOAD_OFFSET + fd_size:#x})")

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

    print(f"{args.image}: ok ({len(data)} bytes, FD {fd_size // 1024} KiB)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
