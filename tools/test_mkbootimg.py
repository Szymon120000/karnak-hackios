#!/usr/bin/env python3
"""Host tests for platform/edk2/KarnakPkg/Tools/mkbootimg.py.

    python3 tools/test_mkbootimg.py
"""

from __future__ import annotations

import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MKBOOTIMG = ROOT / "platform/edk2/KarnakPkg/Tools/mkbootimg.py"


def run(args: list[str]) -> subprocess.CompletedProcess:
    return subprocess.run(
        [sys.executable, str(MKBOOTIMG), *args],
        capture_output=True, text=True)


class MkBootImgTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.dir = Path(self.tmp.name)
        self.kernel = self.dir / "kernel"
        self.kernel.write_bytes(bytes(range(256)) * 20)   # 5120 bytes
        self.out = self.dir / "boot.img"

    def write_legacy(self, *extra: str) -> subprocess.CompletedProcess:
        return run([
            "--header_version", "0",
            "--kernel", str(self.kernel),
            "--kernel_addr", "0x40080000",
            "--tags_addr", "0x4FE00000",
            "--pagesize", "2048",
            "--output", str(self.out),
            *extra,
        ])

    def test_legacy_v0_layout(self):
        proc = self.write_legacy("--cmdline", "console=ttyS0")
        self.assertEqual(proc.returncode, 0, proc.stderr)
        data = self.out.read_bytes()
        u32 = lambda o: struct.unpack_from("<I", data, o)[0]

        self.assertEqual(data[:8], b"ANDROID!")
        self.assertEqual(u32(8), self.kernel.stat().st_size)   # kernel_size
        self.assertEqual(u32(12), 0x40080000)                  # kernel_addr
        self.assertEqual(u32(32), 0x4FE00000)                  # tags_addr
        self.assertEqual(u32(36), 2048)                        # page_size
        self.assertEqual(u32(40), 0)                           # header_version
        self.assertEqual(data[64:64 + 14], b"console=ttyS0\x00")
        # Header padded to page_size, then the kernel.
        self.assertEqual(data[2048:2048 + self.kernel.stat().st_size],
                         self.kernel.read_bytes())

    def test_legacy_requires_addresses(self):
        proc = run([
            "--header_version", "0",
            "--kernel", str(self.kernel),
            "--output", str(self.out),
        ])
        self.assertNotEqual(proc.returncode, 0)
        self.assertIn("--kernel_addr", proc.stderr)
        self.assertFalse(self.out.exists())

    def test_legacy_rejects_zero_address(self):
        proc = run([
            "--header_version", "0",
            "--kernel", str(self.kernel),
            "--kernel_addr", "0",
            "--tags_addr", "0x4FE00000",
            "--output", str(self.out),
        ])
        self.assertNotEqual(proc.returncode, 0)

    def test_modern_v4_layout(self):
        proc = run([
            "--header_version", "4",
            "--kernel", str(self.kernel),
            "--output", str(self.out),
        ])
        self.assertEqual(proc.returncode, 0, proc.stderr)
        data = self.out.read_bytes()
        self.assertEqual(data[:8], b"ANDROID!")
        self.assertEqual(struct.unpack_from("<I", data, 8)[0],
                         self.kernel.stat().st_size)
        self.assertEqual(struct.unpack_from("<I", data, 40)[0], 4)
        self.assertEqual(data[4096:4096 + self.kernel.stat().st_size],
                         self.kernel.read_bytes())

    def test_rejects_unknown_header(self):
        proc = run([
            "--header_version", "2",
            "--kernel", str(self.kernel),
            "--output", str(self.out),
        ])
        self.assertNotEqual(proc.returncode, 0)
        self.assertIn("not implemented", proc.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
