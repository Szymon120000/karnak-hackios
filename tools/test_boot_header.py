#!/usr/bin/env python3
"""Host tests for tools/boot_header.py.

Builds a v0 and a v4 image with the bundled mkbootimg writer, then checks the
values the parser reports (and that it refuses to invent addresses for v4).

    python3 tools/test_boot_header.py
"""

from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MKBOOTIMG = ROOT / "platform/edk2/KarnakPkg/Tools/mkbootimg.py"
BOOT_HEADER = ROOT / "tools/boot_header.py"


class BootHeaderTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.dir = Path(self.tmp.name)
        self.kernel = self.dir / "kernel"
        self.kernel.write_bytes(bytes(range(256)) * 16)
        self.image = self.dir / "boot.img"

    def make(self, *args: str) -> None:
        proc = subprocess.run(
            [sys.executable, str(MKBOOTIMG), "--kernel", str(self.kernel),
             "--output", str(self.image), *args],
            capture_output=True, text=True)
        self.assertEqual(proc.returncode, 0, proc.stderr)

    def parse(self, *args: str) -> subprocess.CompletedProcess:
        return subprocess.run(
            [sys.executable, str(BOOT_HEADER), str(self.image), *args],
            capture_output=True, text=True)

    def test_legacy_addresses(self):
        self.make("--header_version", "0", "--kernel_addr", "0x40080000",
                  "--tags_addr", "0x4fe00000", "--pagesize", "2048")
        proc = self.parse("--shell")
        self.assertEqual(proc.returncode, 0, proc.stderr)
        out = proc.stdout
        self.assertIn("header_version   0", out)
        self.assertIn("page_size        2048", out)
        self.assertIn("LK_KERNEL_ADDR=0x40080000", out)
        self.assertIn("LK_TAGS_ADDR=0x4fe00000", out)
        self.assertIn("LK_PAGE_SIZE=2048", out)

    def test_modern_refuses_addresses(self):
        self.make("--header_version", "4")
        proc = self.parse("--shell")
        self.assertEqual(proc.returncode, 2)
        self.assertIn("no kernel or", proc.stdout)
        self.assertNotIn("LK_KERNEL_ADDR", proc.stdout)

    def test_not_a_boot_image(self):
        self.image.write_bytes(b"not an image at all")
        proc = self.parse()
        self.assertEqual(proc.returncode, 1)


if __name__ == "__main__":
    unittest.main(verbosity=2)
