#!/usr/bin/env python3
"""Assemble a GPT disk image that carries a Darwin (HFS+) root filesystem.

karnak boots from eMMC (MSDC0) with no RAM disk, so the root filesystem has to
live in a partition the loader and XNU can both describe unambiguously.  This
tool builds that container:

    [ protective MBR ]
    [ GPT header + entry array ]
    [ p1  KARNAK-BOOT   FAT32  16 MiB   (optional, kerncache/tools) ]
    [ p2  KARNAK-ROOT   HFS+   rest     (the Darwin rootfs)           ]
    [ backup entry array + backup GPT header ]

Only the container is generated here.  The HFS+ filesystem itself comes from
`--hfs` (an image produced on macOS or by hfsprogs); when it is omitted the
partition is left zeroed and the tool still produces a valid, inspectable disk.
That keeps the tool dependency-free (no sgdisk, no hdiutil, no root) and
runnable in CI where we only need to validate geometry.

Usage:
    mkrootfs.py --hfs base.hfs --output out/rootfs.img [--size 8G]
    mkrootfs.py --output out/rootfs.img --size 256M --no-boot   # smoke test
    mkrootfs.py --inspect out/rootfs.img

The GPT is written byte-for-byte according to UEFI 2.10 chapter 5, using CRC32
from zlib, so `sgdisk -v`, `fdisk -l` and `gdisk` accept the result.
"""

from __future__ import annotations

import argparse
import os
import struct
import sys
import zlib

SECTOR = 512

# Partition type GUIDs (mixed-endian encoding is handled by guid_bytes()).
HFS_TYPE = "48465300-0000-11AA-AA11-00306543ECAC"   # Apple HFS/HFS+
FAT_TYPE = "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7"   # EFI System / FAT
KARNAK_ROOT = "6B61726E-616B-524F-4F54-000000000002"  # "karnak ROOT"
KARNAK_BOOT = "6B61726E-616B-424F-4F54-000000000001"  # "karnak BOOT"

MIB = 1024 * 1024


def parse_size(text: str) -> int:
    suffix = text[-1:].upper()
    mult = {"K": 1024, "M": MIB, "G": 1024 * MIB, "T": 1024 * 1024 * MIB}.get(suffix)
    if mult is None:
        return int(text, 0)
    if not text[:-1].isdecimal():
        raise argparse.ArgumentTypeError(f"bad size: {text}")
    return int(text[:-1]) * mult


def guid_bytes(text: str) -> bytes:
    """Encode a GUID string as the 16 bytes GPT stores on disk."""
    parts = text.split("-")
    if [len(p) for p in parts] != [8, 4, 4, 4, 12]:
        raise ValueError(f"bad GUID: {text}")
    a, b, c, d, e = (int(p, 16) for p in parts)
    return struct.pack("<IHH", a, b, c) + struct.pack(">H", d) + e.to_bytes(6, "big")


def guid_str(raw: bytes) -> str:
    a, b, c = struct.unpack_from("<IHH", raw)
    d = struct.unpack_from(">H", raw, 8)[0]
    e = raw[10:16].hex().upper()
    return f"{a:08X}-{b:04X}-{c:04X}-{d:04X}-{e}"


def gpt_header(
    *,
    my_lba: int,
    alt_lba: int,
    first_usable: int,
    last_usable: int,
    disk_guid: bytes,
    entries_lba: int,
    entries_crc: int,
    entry_count: int = 128,
    entry_size: int = 128,
) -> bytes:
    hdr = struct.pack(
        "<8sIIIIQQQQ16sQIII",
        b"EFI PART",
        0x00010000,
        92,
        0,
        0,
        my_lba,
        alt_lba,
        first_usable,
        last_usable,
        disk_guid,
        entries_lba,
        entry_count,
        entry_size,
        entries_crc,
    )
    assert len(hdr) == 92, len(hdr)
    crc = zlib.crc32(hdr) & 0xFFFFFFFF
    # The CRC lives at offset 16, inside the 92-byte header, not after it.
    hdr = hdr[:16] + struct.pack("<I", crc) + hdr[20:]
    return hdr + b"\x00" * (SECTOR - 92)


def entry(type_guid: bytes, part_guid: bytes, first: int, last: int, name: str) -> bytes:
    raw = struct.pack("<16s16sQQQ", type_guid, part_guid, first, last, 0)
    raw += name.encode("utf-16-le")[:72].ljust(72, b"\x00")
    assert len(raw) == 128
    return raw


def default_layout(disk_bytes: int, with_boot: bool):
    """Return [(name, type_guid, name_guid, mib)] in disk order.

    Overhead is the 1 MiB alignment before the first partition plus the backup
    GPT at the end of the disk (33 sectors, rounded up to 1 MiB).
    """
    boot_mib = 16 if with_boot else 0
    root_mib = disk_bytes // MIB - 2 - boot_mib
    if root_mib < 32:
        raise SystemExit("disk too small: need at least ~64 MiB for a rootfs")
    parts = []
    if with_boot:
        parts.append(("KARNAK-BOOT", FAT_TYPE, KARNAK_BOOT, boot_mib))
    parts.append(("KARNAK-ROOT", HFS_TYPE, KARNAK_ROOT, root_mib))
    return parts


def build(args) -> int:
    disk_bytes = parse_size(args.size)
    if disk_bytes % SECTOR:
        raise SystemExit("size must be a multiple of 512 bytes")
    total_lba = disk_bytes // SECTOR

    if args.hfs:
        hfs_size = os.path.getsize(args.hfs)
        min_root = (hfs_size + 7 * MIB - 1) // MIB * MIB
    else:
        hfs_size = 0
        min_root = 0

    boot_mib = 0 if args.no_boot else 16
    # 1 MiB alignment + 1 MiB backup GPT + boot partition + rootfs payload.
    floor = (2 + boot_mib) * MIB + min_root
    if disk_bytes < floor:
        raise SystemExit(f"--size {args.size} too small; need at least {floor // MIB} MiB")

    layout = default_layout(disk_bytes, not args.no_boot)
    root_mib = layout[-1][3]
    if hfs_size > root_mib * MIB:
        raise SystemExit(
            f"{args.hfs} is {hfs_size // MIB} MiB but the root partition is {root_mib} MiB"
        )

    # Reserve 1 MiB of alignment before the first partition (UEFI recommends
    # 1 MiB for flash-friendly media).  Entry array is 128 * 128 = 16 KiB.
    entries_sectors = 128 * 128 // SECTOR
    first_usable = 1 + 1 + entries_sectors            # protective MBR + hdr + array
    first_data_lba = (1 * MIB) // SECTOR              # 1 MiB aligned
    last_usable = total_lba - 2 - entries_sectors     # backup array + backup hdr
    if first_usable < 2 + entries_sectors:
        first_usable = 2 + entries_sectors
    first_data_lba = max(first_data_lba, first_usable)

    disk_guid = guid_bytes("6B61726E-0000-4000-8000-6B61726E0000")
    entries = []
    cursor = first_data_lba
    for i, (name, tguid, nguid, mib) in enumerate(layout):
        last = total_lba - 2 - entries_sectors if i == len(layout) - 1 else cursor + mib * 2048 - 1
        if last > last_usable:
            last = last_usable
        entries.append(
            entry(guid_bytes(tguid), guid_bytes(nguid), cursor, last, name)
        )
        cursor = last + 1

    used_last = max(int.from_bytes(e[40:48], "little") for e in entries)
    if used_last > last_usable:
        raise SystemExit("internal error: partition exceeds usable area")

    array = b"".join(entries) + b"\x00" * (128 * 128 - 128 * len(entries))
    array_crc = zlib.crc32(array) & 0xFFFFFFFF

    primary = gpt_header(
        my_lba=1,
        alt_lba=total_lba - 1,
        first_usable=first_data_lba,
        last_usable=last_usable,
        disk_guid=disk_guid,
        entries_lba=2,
        entries_crc=array_crc,
    )
    backup = gpt_header(
        my_lba=total_lba - 1,
        alt_lba=1,
        first_usable=first_data_lba,
        last_usable=last_usable,
        disk_guid=disk_guid,
        entries_lba=total_lba - 1 - entries_sectors,
        entries_crc=array_crc,
    )

    mbr = (
        b"\x00" * 446
        + struct.pack("<B3sB3sII", 0x00, b"\x00\x02\x00", 0xEE, b"\xff\xff\xff", 1, min(total_lba - 1, 0xFFFFFFFF))
        + b"\x00" * 48            # three unused MBR slots
        + b"\x55\xAA"
    )

    out_dir = os.path.dirname(os.path.abspath(args.output))
    os.makedirs(out_dir, exist_ok=True)
    with open(args.output, "wb") as img:
        img.truncate(disk_bytes)
        img.write(mbr)
        img.seek(SECTOR)
        img.write(primary)
        img.seek(2 * SECTOR)
        img.write(array)
        if args.hfs:
            root_start = int.from_bytes(entries[-1][32:40], "little") * SECTOR
            img.seek(root_start)
            with open(args.hfs, "rb") as src:
                while chunk := src.read(4 * MIB):
                    img.write(chunk)
        img.seek((total_lba - 1 - entries_sectors) * SECTOR)
        img.write(array)
        img.seek((total_lba - 1) * SECTOR)
        img.write(backup)

    print(f"wrote {args.output}: {disk_bytes // MIB} MiB, {len(entries)} partition(s)")
    for name, e in zip((p[0] for p in layout), entries):
        start, end = struct.unpack_from("<QQ", e, 32)
        print(f"  {name:<14} LBA {start:>10}..{end:<10} {(end - start + 1) // 2048:>8} MiB")
    if not args.hfs:
        print("note: no --hfs given; KARNAK-ROOT is zero-filled")
    return 0


def inspect(path: str) -> int:
    size = os.path.getsize(path)
    with open(path, "rb") as f:
        hdr = f.read(2 * SECTOR)[SECTOR:]
        if hdr[:8] != b"EFI PART":
            raise SystemExit(f"{path}: no GPT header at LBA 1")
        my_lba, alt_lba, first, last = struct.unpack_from("<QQQQ", hdr, 24)
        entries_lba = struct.unpack_from("<Q", hdr, 72)[0]
        count, esize = struct.unpack_from("<II", hdr, 80)
        crc = struct.unpack_from("<I", hdr, 16)[0]
        check = zlib.crc32(hdr[:16] + b"\x00\x00\x00\x00" + hdr[20:92]) & 0xFFFFFFFF
        f.seek(entries_lba * SECTOR)
        raw = f.read(count * esize)
        array_crc = zlib.crc32(raw) & 0xFFFFFFFF
        print(f"{path}: {size // MIB} MiB")
        print(f"  header crc {'ok' if crc == check else 'BAD'}; entries crc "
              f"{'ok' if array_crc == struct.unpack_from('<I', hdr, 88)[0] else 'BAD'}")
        print(f"  usable LBA {first}..{last}, backup header at {alt_lba}")
        for i in range(count):
            e = raw[i * esize:(i + 1) * esize]
            if e[:16] == b"\x00" * 16:
                continue
            start, end = struct.unpack_from("<QQ", e, 32)
            name = e[56:128].decode("utf-16-le").rstrip("\x00")
            print(f"  p{i + 1}: {name:<14} {guid_str(e[:16])} "
                  f"LBA {start}..{end} ({(end - start + 1) // 2048} MiB)")
    return 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--hfs", help="HFS+ filesystem image to embed")
    ap.add_argument("-o", "--output", default="out/rootfs.img")
    ap.add_argument("-s", "--size", default="8G", help="disk size (default 8G)")
    ap.add_argument("--no-boot", action="store_true", help="omit the FAT boot partition")
    ap.add_argument("--inspect", metavar="IMAGE", help="print an existing image's GPT")
    args = ap.parse_args(argv)
    if args.inspect:
        return inspect(args.inspect)
    return build(args)


if __name__ == "__main__":
    sys.exit(main())
