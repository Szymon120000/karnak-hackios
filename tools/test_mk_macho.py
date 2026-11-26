#!/usr/bin/env python3
"""Host tests for tools/mk_macho.py."""

from __future__ import annotations

import os
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, "mk_macho.py")

sys.path.insert(0, HERE)
import mk_macho  # noqa: E402

failures = 0


def check(cond: bool, what: str) -> None:
    global failures
    if not cond:
        print(f"FAIL: {what}")
        failures += 1


def test_layout() -> None:
    payload = bytes(range(256)) * 4
    data = mk_macho.build(payload)
    check(data[:4] == struct.pack("<I", mk_macho.MH_MAGIC_64), "magic")
    check(len(data) == mk_macho.TEXT_OFFSET + len(payload), "file size")
    check(data.endswith(payload), "payload is last")

    cmdsize = struct.unpack_from("<I", data, 20)[0]
    check(cmdsize == 72 + mk_macho.UNIXTHREAD_BYTES, "sizeofcmds")

    vmaddr, vmsize, fileoff, filesize = struct.unpack_from("<QQQQ", data, 32 + 24)
    check(vmaddr == mk_macho.DEFAULT_BASE, "vmaddr")
    check(fileoff == 0, "fileoff")
    check(filesize == len(data), "filesize covers the file")
    check(vmsize % mk_macho.PAGE == 0 and vmsize >= filesize, "vmsize rounded")

    off = 32 + 72
    cmd, size, flavor, count = struct.unpack_from("<IIII", data, off)
    check(cmd == mk_macho.LC_UNIXTHREAD, "unixthread cmd")
    check(size == mk_macho.UNIXTHREAD_BYTES, "unixthread size")
    check((flavor, count) == (6, 68), "arm64 thread state")
    pc = struct.unpack_from("<Q", data, off + 16 + 32 * 8)[0]
    check(pc == mk_macho.DEFAULT_BASE + mk_macho.TEXT_OFFSET, "entry pc")

    check(mk_macho.check(data) == [], "self-check passes")


def test_bad_images() -> None:
    good = mk_macho.build(b"x" * 128)
    check(mk_macho.check(good[:16]) != [], "short image rejected")

    bad = bytearray(good)
    struct.pack_into("<I", bad, 0, 0xCAFEBABE)
    check(mk_macho.check(bytes(bad)) != [], "bad magic rejected")

    bad = bytearray(good)
    struct.pack_into("<I", bad, 12, 6)  # MH_DYLIB
    check(mk_macho.check(bytes(bad)) != [], "bad filetype rejected")


def test_cli() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        payload = os.path.join(tmp, "payload.bin")
        image = os.path.join(tmp, "kernelcache")
        with open(payload, "wb") as f:
            f.write(b"\x1f\x20\x03\xd5" * 16)

        subprocess.run([sys.executable, TOOL, "--payload", payload,
                        "--output", image], check=True, capture_output=True)
        subprocess.run([sys.executable, TOOL, "--check", image],
                       check=True, capture_output=True)

        with open(image, "rb") as f:
            data = bytearray(f.read())
        struct.pack_into("<I", data, 0, 0)
        broken = os.path.join(tmp, "broken")
        with open(broken, "wb") as f:
            f.write(data)
        result = subprocess.run([sys.executable, TOOL, "--check", broken],
                                capture_output=True)
        check(result.returncode == 1, "check exits 1 on corruption")


def main() -> int:
    test_layout()
    test_bad_images()
    test_cli()
    if failures:
        print(f"{failures} test(s) failed")
        return 1
    print("mk_macho: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
