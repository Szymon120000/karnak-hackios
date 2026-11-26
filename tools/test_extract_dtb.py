#!/usr/bin/env python3
"""Host tests for tools/extract_dtb.py.

Builds synthetic v0 boot images (plain and amonet-injected), with raw and
gzip-compressed kernels, and checks that the appended DTB is recovered and
that malformed images are rejected.  If the pre-flash stock backup is present,
it is checked against the known DTB size as well.

    python3 tools/test_extract_dtb.py
"""

from __future__ import annotations

import contextlib
import gzip
import io
import struct
import tempfile
import unittest
from pathlib import Path

import extract_dtb as ed

PAGE = 2048
STOCK_BOOT = Path(__file__).resolve().parent.parent / "device-backup" / "boot-stock.img"
STOCK_DTB_SIZE = 123445


def build_dtb(size: int = 0x100) -> bytes:
    dtb = bytearray(size)
    dtb[0:4] = ed.FDT_MAGIC
    struct.pack_into(">I", dtb, 4, size)
    return bytes(dtb)


def build_image(*, tree: bytes | None = None, gzipped: bool = False,
                injected: bool = False, kernel_size: int | None = None) -> bytes:
    body = b"KERNEL-BODY" * 16
    tree = build_dtb() if tree is None else tree
    kernel = gzip.compress(body) + tree if gzipped else body + tree
    size = len(kernel) if kernel_size is None else kernel_size

    header = bytearray(PAGE)
    header[0:8] = ed.BOOT_MAGIC
    struct.pack_into("<I", header, 8, size)      # kernel_size
    struct.pack_into("<I", header, 36, PAGE)     # page_size

    if injected:
        # amonet: stub in the first page, real header at 0x400 (only the first
        # 0x400 bytes fit before the kernel page), kernel at 0x800.
        stub = bytearray(0x800)
        stub[0x400:0x800] = header[:0x400]
        return bytes(stub) + kernel
    return bytes(header) + kernel


class ExtractDtbTest(unittest.TestCase):
    def test_raw_kernel(self):
        self.assertEqual(ed.extract_dtb(build_image()), build_dtb())

    def test_gzipped_kernel(self):
        self.assertEqual(ed.extract_dtb(build_image(gzipped=True)), build_dtb())

    def test_injected_gzipped_kernel(self):
        data = build_image(gzipped=True, injected=True)
        self.assertEqual(ed.extract_dtb(data), build_dtb())

    def test_missing_dtb(self):
        with self.assertRaises(ValueError):
            ed.extract_dtb(build_image(tree=b""))

    def test_truncated_dtb_header(self):
        with self.assertRaises(ValueError):
            ed.extract_dtb(build_image(tree=build_dtb()[:6]))

    def test_truncated_kernel(self):
        with self.assertRaises(ValueError):
            ed.extract_dtb(build_image(kernel_size=0x10000))

    def test_corrupt_gzip(self):
        data = bytearray(build_image(gzipped=True))
        data[PAGE + 10] ^= 0xFF                      # gzip header byte
        with self.assertRaises(ValueError):
            ed.extract_dtb(bytes(data))

    def test_cli_writes_file(self):
        data = build_image(gzipped=True)
        with tempfile.TemporaryDirectory() as tmp:
            image = Path(tmp) / "boot.img"
            output = Path(tmp) / "karnak.dtb"
            image.write_bytes(data)
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(ed.main([str(image), str(output)]), 0)
            self.assertEqual(output.read_bytes(), build_dtb())

    @unittest.skipUnless(STOCK_BOOT.exists(), "stock boot backup not present")
    def test_stock_backup(self):
        dtb = ed.extract_dtb(STOCK_BOOT.read_bytes())
        self.assertEqual(len(dtb), STOCK_DTB_SIZE)
        self.assertEqual(dtb[:4], ed.FDT_MAGIC)


if __name__ == "__main__":
    unittest.main(verbosity=2)
