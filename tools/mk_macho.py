#!/usr/bin/env python3
"""Wrap a flat ARM64 payload in a minimal Mach-O 64 executable.

The karnak XNU loader parses, validates, copies and enters a Mach-O image.
Until a real kernel exists, `SyntheticKernel.S` provides a payload that paints
the panel; this tool turns it into the same shape a kernelcache has:

    Mach-O header (32 bytes)
    LC_SEGMENT_64 "__TEXT" (72 bytes, fileoff 0, covers the whole file)
    LC_UNIXTHREAD ARM_THREAD_STATE64 (288 bytes, pc = payload entry)

The entry point is the first byte after the load commands, and since the
loader copies the segment from file offset 0 the headers travel with the code.
No symbol table, code signature or dynamic information is emitted.

Usage:
    mk_macho.py --payload SyntheticKernel.bin --output kernelcache
    mk_macho.py --check kernelcache
"""

from __future__ import annotations

import argparse
import struct
import sys

MH_MAGIC_64 = 0xFEEDFACF
CPU_TYPE_ARM64 = 0x0100000C
MH_EXECUTE = 2

LC_SEGMENT_64 = 0x19
LC_UNIXTHREAD = 0x05

ARM_THREAD_STATE64 = 6
ARM_THREAD_STATE64_COUNT = 68
UNIXTHREAD_BYTES = 16 + 34 * 8
HEADER_BYTES = 32
SEGMENT_COMMAND_BYTES = 72
TEXT_OFFSET = HEADER_BYTES + SEGMENT_COMMAND_BYTES + UNIXTHREAD_BYTES

DEFAULT_BASE = 0xFFFFFFF007004000
PAGE = 0x4000


def build(payload: bytes, base: int = DEFAULT_BASE) -> bytes:
    text_off = TEXT_OFFSET
    if text_off % 8:
        raise ValueError("load commands are not 8-byte aligned")

    entry = base + text_off
    file_size = text_off + len(payload)
    vm_size = (file_size + PAGE - 1) & ~(PAGE - 1)

    out = bytearray()
    out += struct.pack(
        "<IiiIIIII",
        MH_MAGIC_64,
        CPU_TYPE_ARM64,
        0,          # cpusubtype: ARM64_ALL
        MH_EXECUTE,
        2,          # ncmds
        SEGMENT_COMMAND_BYTES + UNIXTHREAD_BYTES,
        0,          # flags
        0,          # reserved
    )

    out += struct.pack(
        "<II16sQQQQIIII",
        LC_SEGMENT_64,
        SEGMENT_COMMAND_BYTES,
        b"__TEXT\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00",
        base,
        vm_size,
        0,          # fileoff
        file_size,  # filesize
        7,          # maxprot: rwx
        5,          # initprot: r-x
        0,          # nsects
        0,          # flags
    )

    thread = bytearray(UNIXTHREAD_BYTES)
    struct.pack_into("<IIII", thread, 0, LC_UNIXTHREAD, UNIXTHREAD_BYTES,
                     ARM_THREAD_STATE64, ARM_THREAD_STATE64_COUNT)
    struct.pack_into("<Q", thread, 16 + 32 * 8, entry)
    out += thread
    assert len(out) == text_off

    out += payload
    return bytes(out)


def check(data: bytes) -> list[str]:
    errors: list[str] = []
    if len(data) < 32 + 72 + UNIXTHREAD_BYTES:
        return ["image is shorter than its load commands"]
    (magic, cputype, _sub, filetype, ncmds, sizeofcmds, _flags,
     _res) = struct.unpack_from("<IiiIIIII", data, 0)
    if magic != MH_MAGIC_64:
        errors.append(f"magic {magic:#x} != {MH_MAGIC_64:#x}")
    if cputype != CPU_TYPE_ARM64:
        errors.append(f"cputype {cputype:#x} is not ARM64")
    if filetype != MH_EXECUTE:
        errors.append(f"filetype {filetype} is not MH_EXECUTE")
    if ncmds != 2:
        errors.append(f"ncmds {ncmds} != 2")
    if 32 + sizeofcmds > len(data):
        errors.append("sizeofcmds runs past the file")

    off = 32
    vmaddr = None
    cmd, cmd_size = struct.unpack_from("<II", data, off)
    if cmd != LC_SEGMENT_64 or cmd_size != SEGMENT_COMMAND_BYTES:
        errors.append("first load command is not LC_SEGMENT_64")
    else:
        (vmaddr, vmsize, fileoff, filesize) = struct.unpack_from("<QQQQ", data, off + 24)
        if filesize + fileoff != len(data):
            errors.append("segment filesize does not cover the file")
        if vmsize < filesize or vmsize % PAGE:
            errors.append("segment vmsize is not page-rounded")

    off += 72
    cmd, cmd_size, flavor, count = struct.unpack_from("<IIII", data, off)
    if cmd != LC_UNIXTHREAD or cmd_size != UNIXTHREAD_BYTES:
        errors.append("second load command is not LC_UNIXTHREAD")
    elif flavor != ARM_THREAD_STATE64 or count != ARM_THREAD_STATE64_COUNT:
        errors.append("thread state is not ARM64")
    else:
        pc = struct.unpack_from("<Q", data, off + 16 + 32 * 8)[0]
        if vmaddr is not None and pc != vmaddr + TEXT_OFFSET:
            errors.append("thread pc does not point at the payload")
    return errors


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--payload")
    ap.add_argument("--output")
    ap.add_argument("--base", type=lambda v: int(v, 0), default=DEFAULT_BASE)
    ap.add_argument("--check", metavar="MACHO",
                    help="validate an existing image instead of building one")
    args = ap.parse_args()

    if args.check:
        with open(args.check, "rb") as f:
            errors = check(f.read())
        if errors:
            for error in errors:
                print(f"{args.check}: {error}", file=sys.stderr)
            return 1
        print(f"{args.check}: ok")
        return 0

    if not args.payload or not args.output:
        ap.error("--payload and --output are required unless --check is used")
    with open(args.payload, "rb") as f:
        payload = f.read()
    if not payload:
        print("error: payload is empty", file=sys.stderr)
        return 1
    data = build(payload, args.base)
    with open(args.output, "wb") as f:
        f.write(data)
    print(f"{args.output}: {len(data)} bytes, entry {args.base + TEXT_OFFSET:#x}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
