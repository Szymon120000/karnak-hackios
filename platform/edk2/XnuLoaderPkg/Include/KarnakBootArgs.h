/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/**
  XNU ARM64 `boot_args` layout and helpers (XNU 8796 era).

  The authoritative definition is Apple's
  `pexpert/pexpert/arm64/boot.h` from xnu-8796.141.3, vendored under
  `xnu/include/pexpert/pexpert/arm64/boot.h`.  Firmware code cannot include
  that header directly (it pulls in kernel-only types and libc headers), so
  this file mirrors the layout with UEFI types and the host test
  `xnu/tests/boot_args_test.c` asserts every field offset against the original.

  Do not reorder fields.  Changing the layout silently corrupts the handoff.
**/
#ifndef KARNAK_BOOT_ARGS_H_
#define KARNAK_BOOT_ARGS_H_

#define KARNAK_BOOT_LINE_LENGTH   608
#define KARNAK_BOOT_ARGS_REVISION 2
#define KARNAK_BOOT_ARGS_VERSION  2

#define KARNAK_BOOT_VIDEO_DEPTH_MASK        0xFFU
#define KARNAK_BOOT_VIDEO_DEPTH_SHIFT       0U
#define KARNAK_BOOT_VIDEO_ROTATE_SHIFT      8U
#define KARNAK_BOOT_VIDEO_SCALE_SHIFT       16U
#define KARNAK_BOOT_VIDEO_BOOT_ROTATE_SHIFT 24U

typedef struct {
  UINT64    BaseAddress;
  UINT64    Display;
  UINT64    RowBytes;
  UINT64    Width;
  UINT64    Height;
  UINT64    Depth;
} KARNAK_BOOT_VIDEO;

typedef struct {
  UINT16              Revision;
  UINT16              Version;
  UINT64              VirtBase;
  UINT64              PhysBase;
  UINT64              MemSize;
  UINT64              TopOfKernelData;
  KARNAK_BOOT_VIDEO   Video;
  UINT32              MachineType;
  VOID                *DeviceTree;
  UINT32              DeviceTreeLength;
  CHAR8               CommandLine[KARNAK_BOOT_LINE_LENGTH];
  UINT64              BootFlags;
  UINT64              MemSizeActual;
} KARNAK_BOOT_ARGS;

/**
  Inputs that the loader owns.  Anything not listed here is derived.
**/
typedef struct {
  UINT64              PhysBase;
  UINT64              MemSize;
  UINT64              TopOfKernelData;
  UINT64              MemSizeActual;   /* 0 = unknown, same as MemSize */
  UINT64              BootFlags;
  UINT32              MachineType;
  VOID                *DeviceTree;
  UINT32              DeviceTreeLength;
  CONST CHAR8         *CommandLine;    /* optional, ASCII, NUL-terminated */
  KARNAK_BOOT_VIDEO   Video;
} KARNAK_BOOT_ARGS_CONFIG;

#endif /* KARNAK_BOOT_ARGS_H_ */
