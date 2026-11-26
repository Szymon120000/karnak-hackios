#!/usr/bin/env python3
"""Host tests for tools/check_boot_image.py.

Builds synthetic but structurally valid boot images (modern v4 and legacy v0),
then corrupts one field at a time and asserts that the checker rejects it.  No
EDK2 checkout or cross toolchain required.

    python3 tools/test_check_boot_image.py
"""

from __future__ import annotations

import contextlib
import gzip
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


def build_dtb(size: int = 0x100) -> bytes:
    dtb = bytearray(size)
    dtb[0:4] = cbi.FDT_MAGIC
    struct.pack_into(">I", dtb, 4, size)            # totalsize (big-endian)
    return bytes(dtb)


def build_payload(fd_size: int = FD_SIZE) -> bytes:
    shim = bytearray(SHIM_SIZE)
    struct.pack_into("<I", shim, 0x00, 0x14000000 | 0x40)   # b _Start
    struct.pack_into("<Q", shim, 0x10, fd_size + 0x1000)    # image_size
    shim[0x38:0x3C] = cbi.ARM64_MAGIC
    return bytes(shim) + build_fd(fd_size)


def build_shim(image_size: int = FD_SIZE + 0x1000) -> bytes:
    shim = bytearray(SHIM_SIZE)
    struct.pack_into("<I", shim, 0x00, 0x14000000 | 0x40)   # b _Start
    struct.pack_into("<Q", shim, 0x10, image_size)          # image_size
    shim[0x38:0x3C] = cbi.ARM64_MAGIC
    return bytes(shim)


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
    dtb: bytes | None = None,
    ramdisk: bytes | None = None,
    image_size: int | None = None,
) -> bytes:
    tree = build_dtb() if dtb is None else dtb
    # LK decompresses only the first gzip member and then looks for the DTB in
    # the bytes that follow it, so the tree is appended outside the stream.
    kernel = gzip.compress(build_shim(
        FD_SIZE + 0x1000 if image_size is None else image_size)) + tree
    ramdisk = build_fd(fd_size) if ramdisk is None else ramdisk

    header = bytearray(page_size)
    header[0:8] = cbi.BOOT_MAGIC
    struct.pack_into("<I", header, 8, len(kernel))     # kernel_size
    struct.pack_into("<I", header, 12, kernel_addr)    # kernel_addr
    struct.pack_into("<I", header, 16, len(ramdisk))   # ramdisk_size
    struct.pack_into("<I", header, 20, 0x43480000)     # ramdisk_addr
    struct.pack_into("<I", header, 32, tags_addr)      # tags_addr
    struct.pack_into("<I", header, 36, page_size)      # page_size
    struct.pack_into("<I", header, 40, 0)              # header_version

    def pad(blob: bytes) -> bytes:
        return blob + b"\x00" * ((-len(blob)) % page_size)

    return bytes(header) + pad(kernel) + pad(ramdisk)


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
        self.assertEqual(self.run_check(build_legacy_image(image_size=FD_SIZE)), 1)

    def test_legacy_gzipped_with_dtb_passes(self):
        self.assertEqual(self.run_check(build_legacy_image()), 0)

    def test_legacy_missing_dtb(self):
        self.assertEqual(self.run_check(build_legacy_image(dtb=b"")), 1)

    def test_legacy_truncated_dtb(self):
        self.assertEqual(self.run_check(build_legacy_image(dtb=build_dtb()[:6])), 1)

    def test_legacy_missing_ramdisk(self):
        self.assertEqual(self.run_check(build_legacy_image(ramdisk=b"")), 1)

    def test_legacy_ramdisk_not_an_fd(self):
        ramdisk = bytearray(build_fd())
        ramdisk[0x28:0x2C] = b"XXXX"
        self.assertEqual(self.run_check(build_legacy_image(ramdisk=bytes(ramdisk))), 1)

    def test_legacy_bad_fv_checksum(self):
        ramdisk = bytearray(build_fd())
        off = 0x32
        struct.pack_into("<H", ramdisk, off, struct.unpack_from("<H", ramdisk, off)[0] ^ 0x5A5A)
        self.assertEqual(self.run_check(build_legacy_image(ramdisk=bytes(ramdisk))), 1)


if __name__ == "__main__":
    unittest.main(verbosity=2)
