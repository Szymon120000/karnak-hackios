#!/usr/bin/env python3
"""Host tests for rootfs/mkrootfs.py.  No external tools required."""

import io
import os
import struct
import sys
import tempfile
import unittest
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mkrootfs as mk  # noqa: E402


class GptTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.hfs = os.path.join(self.tmp.name, "base.hfs")
        payload = bytes(range(256)) * 4096          # 1 MiB, recognisable
        with open(self.hfs, "wb") as f:
            f.write(payload)
        self.payload = payload

    def tearDown(self):
        self.tmp.cleanup()

    def build(self, *extra, size="64M"):
        out = os.path.join(self.tmp.name, "disk.img")
        rc = mk.main(["--hfs", self.hfs, "-o", out, "-s", size, *extra])
        self.assertEqual(rc, 0)
        return out

    def read(self, path, lba, count=1):
        with open(path, "rb") as f:
            f.seek(lba * mk.SECTOR)
            return f.read(count * mk.SECTOR)

    def test_protective_mbr(self):
        img = self.build()
        mbr = self.read(img, 0)
        self.assertEqual(mbr[510:512], b"\x55\xaa")
        ptype = mbr[446 + 4]
        self.assertEqual(ptype, 0xEE)
        start, count = struct.unpack_from("<II", mbr, 446 + 8)
        self.assertEqual(start, 1)
        self.assertGreater(count, 1)

    def test_header_crc_and_backup(self):
        img = self.build()
        size = os.path.getsize(img)
        total = size // mk.SECTOR
        hdr = self.read(img, 1)
        self.assertEqual(hdr[:8], b"EFI PART")
        want = struct.unpack_from("<I", hdr, 16)[0]
        self.assertEqual(want, zlib.crc32(hdr[:16] + b"\0\0\0\0" + hdr[20:92]) & 0xFFFFFFFF)
        self.assertEqual(struct.unpack_from("<Q", hdr, 32)[0], total - 1)  # alternate LBA
        backup = self.read(img, total - 1)
        self.assertEqual(backup[:8], b"EFI PART")
        self.assertEqual(struct.unpack_from("<Q", backup, 24)[0], total - 1)  # my LBA

    def test_partitions_and_payload(self):
        img = self.build(size="64M")
        hdr = self.read(img, 1)
        entries_lba = struct.unpack_from("<Q", hdr, 72)[0]
        raw = self.read(img, entries_lba, 4)
        p1, p2 = raw[0:128], raw[128:256]
        self.assertEqual(mk.guid_str(p1[:16]), mk.FAT_TYPE)
        self.assertEqual(mk.guid_str(p2[:16]), mk.HFS_TYPE)
        self.assertEqual(p1[56:128].decode("utf-16-le").rstrip("\0"), "KARNAK-BOOT")
        self.assertEqual(p2[56:128].decode("utf-16-le").rstrip("\0"), "KARNAK-ROOT")

        start = struct.unpack_from("<Q", p2, 32)[0]
        # HFS+ payload must land exactly at the partition start.
        with open(img, "rb") as f:
            f.seek(start * mk.SECTOR)
            self.assertEqual(f.read(len(self.payload)), self.payload)
        # ...and the rest of the partition must stay zero.
        with open(img, "rb") as f:
            f.seek(start * mk.SECTOR + len(self.payload))
            self.assertEqual(f.read(4096), b"\0" * 4096)

    def test_entry_crc_matches(self):
        img = self.build()
        hdr = self.read(img, 1)
        entries_lba = struct.unpack_from("<Q", hdr, 72)[0]
        count, esize = struct.unpack_from("<II", hdr, 80)
        raw = self.read(img, entries_lba, count * esize // mk.SECTOR)
        self.assertEqual(
            zlib.crc32(raw) & 0xFFFFFFFF, struct.unpack_from("<I", hdr, 88)[0]
        )

    def test_rejects_oversized_payload(self):
        with self.assertRaises(SystemExit):
            mk.main(["--hfs", self.hfs, "-o", os.path.join(self.tmp.name, "x.img"), "-s", "4M"])

    def test_no_boot(self):
        img = self.build("--no-boot", size="64M")
        hdr = self.read(img, 1)
        raw = self.read(img, struct.unpack_from("<Q", hdr, 72)[0], 1)
        self.assertEqual(mk.guid_str(raw[:16]), mk.HFS_TYPE)

    def test_guid_roundtrip(self):
        for g in (mk.HFS_TYPE, mk.FAT_TYPE, mk.KARNAK_ROOT):
            self.assertEqual(mk.guid_str(mk.guid_bytes(g)), g)


if __name__ == "__main__":
    unittest.main(verbosity=2)
