#!/usr/bin/env python3
"""Host tests for tools/check_boot_image.py.

Builds synthetic but structurally valid boot images (modern v4 and legacy v0),
then corrupts one field at a time and asserts that the checker rejects it.  No
EDK2 checkout or cross toolchain required.

    python3 tools/test_check_boot_image.py
"""

from __future__ import annotations

import contextlib
import io
import struct
import tempfile
import unittest
from pathlib import Path

import check_boot_image as cbi

FD_SIZE = 0x1000
SHIM_SIZE = 0x140
MODERN_HEADER = 4096
LEGACY_PAGE = 2048


def fv_checksum(header: bytearray) -> int:
    if len(header) % 2:
        header += b"\x00"
    total = sum(struct.unpack_from("<H", header, i)[0] for i in range(0, len(header), 2))
    return (-total) & 0xFFFF


def build_fd(fd_size: int = FD_SIZE) -> bytes:
    fd = bytearray(fd_size)
    hdr_len = 0x48
    struct.pack_into("<Q", fd, 0x20, fd_size)       # FvLength
    fd[0x28:0x2C] = cbi.FV_SIG                      # Signature
    struct.pack_into("<H", fd, 0x30, hdr_len)       # HeaderLength
    struct.pack_into("<H", fd, 0x32, 0)             # Checksum placeholder
    struct.pack_into("<H", fd, 0x32, fv_checksum(bytearray(fd[:hdr_len])))
    return bytes(fd)


def build_payload(fd_size: int = FD_SIZE) -> bytes:
    shim = bytearray(SHIM_SIZE)
    struct.pack_into("<I", shim, 0x00, 0x14000000 | 0x40)   # b _Start
    struct.pack_into("<Q", shim, 0x10, fd_size + 0x1000)    # image_size
    shim[0x38:0x3C] = cbi.ARM64_MAGIC
    return bytes(shim) + build_fd(fd_size)


def build_modern_image(*, fd_size: int = FD_SIZE) -> bytes:
    payload = build_payload(fd_size)
    header = bytearray(MODERN_HEADER)
    header[0:8] = cbi.BOOT_MAGIC
    struct.pack_into("<I", header, 8, len(payload))   # kernel_size
    struct.pack_into("<I", header, 40, 4)             # header_version
    return bytes(header) + payload


def build_legacy_image(
    *,
    fd_size: int = FD_SIZE,
    page_size: int = LEGACY_PAGE,
    kernel_addr: int = 0x40080000,
    tags_addr: int = 0x4FE00000,
) -> bytes:
    payload = build_payload(fd_size)
    header = bytearray(page_size)
    header[0:8] = cbi.BOOT_MAGIC
    struct.pack_into("<I", header, 8, len(payload))   # kernel_size
    struct.pack_into("<I", header, 12, kernel_addr)   # kernel_addr
    struct.pack_into("<I", header, 32, tags_addr)     # tags_addr
    struct.pack_into("<I", header, 36, page_size)     # page_size
    struct.pack_into("<I", header, 40, 0)             # header_version
    padded = payload + b"\x00" * ((-len(payload)) % page_size)
    return bytes(header) + padded


class CheckBootImageTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.path = Path(self.tmp.name) / "boot.img"

    def run_check(self, data: bytes, fd_size: int = FD_SIZE) -> int:
        self.path.write_bytes(data)
        with contextlib.redirect_stderr(io.StringIO()), \
                contextlib.redirect_stdout(io.StringIO()):
            return cbi.main([str(self.path), "--fd-size", hex(fd_size)])

    # --- modern v4 -------------------------------------------------------
    def test_valid_modern_image_passes(self):
        self.assertEqual(self.run_check(build_modern_image()), 0)

    def test_missing_android_magic(self):
        data = bytearray(build_modern_image())
        data[0:8] = b"NOTBOOT!"
        self.assertEqual(self.run_check(bytes(data)), 1)

    def test_unsupported_header_version(self):
        data = bytearray(build_modern_image())
        struct.pack_into("<I", data, 40, 9)
        self.assertEqual(self.run_check(bytes(data)), 1)

    def test_nonzero_data_after_payload(self):
        self.assertEqual(self.run_check(build_modern_image() + b"\x01"), 1)

    def test_missing_arm64_magic(self):
        data = bytearray(build_modern_image())
        off = MODERN_HEADER + 0x38
        data[off:off + 4] = b"XXXX"
        self.assertEqual(self.run_check(bytes(data)), 1)

    def test_wrong_image_size(self):
        data = bytearray(build_modern_image())
        struct.pack_into("<Q", data, MODERN_HEADER + 0x10, FD_SIZE)
        self.assertEqual(self.run_check(bytes(data)), 1)

    def test_wrong_fd_size(self):
        self.assertEqual(self.run_check(build_modern_image(), fd_size=FD_SIZE * 2), 1)

    def test_missing_fv_signature(self):
        data = bytearray(build_modern_image())
        off = MODERN_HEADER + SHIM_SIZE + 0x28
        data[off:off + 4] = b"XXXX"
        self.assertEqual(self.run_check(bytes(data)), 1)

    def test_corrupt_fv_checksum(self):
        data = bytearray(build_modern_image())
        off = MODERN_HEADER + SHIM_SIZE + 0x32
        struct.pack_into("<H", data, off, struct.unpack_from("<H", data, off)[0] ^ 0x5A5A)
        self.assertEqual(self.run_check(bytes(data)), 1)

    # --- legacy v0 (karnak LK) ------------------------------------------
    def test_valid_legacy_image_passes(self):
        self.assertEqual(self.run_check(build_legacy_image()), 0)

    def test_legacy_zero_kernel_addr(self):
        self.assertEqual(self.run_check(build_legacy_image(kernel_addr=0)), 1)

    def test_legacy_zero_tags_addr(self):
        self.assertEqual(self.run_check(build_legacy_image(tags_addr=0)), 1)

    def test_legacy_bad_page_size(self):
        self.assertEqual(self.run_check(build_legacy_image(page_size=2000)), 1)

    def test_legacy_payload_mismatch(self):
        data = bytearray(build_legacy_image())
        struct.pack_into("<Q", data, LEGACY_PAGE + 0x10, FD_SIZE)  # wrong image_size
        self.assertEqual(self.run_check(bytes(data)), 1)


if __name__ == "__main__":
    unittest.main(verbosity=2)
