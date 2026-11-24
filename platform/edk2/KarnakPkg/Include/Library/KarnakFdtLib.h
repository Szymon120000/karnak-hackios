/** @file
  Read-only flattened device tree access for karnak platform libraries.

  The boot shim copies LK's live DTB to a stable address and publishes that
  address at PcdDeviceTreeStore.  This library is the only place that knows
  how to find and walk that blob; every consumer (memory map, DXE DTB
  loader, XnuLoader) uses it so there is a single parsing implementation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#ifndef KARNAK_FDT_LIB_H_
#define KARNAK_FDT_LIB_H_

#include <Uefi.h>

#define KARNAK_FDT_MAX_MEMORY_RANGES 4
#define KARNAK_FDT_LCM_NAME_LEN      32

typedef struct {
  UINT64 Base;
  UINT64 Size;
} KARNAK_FDT_RANGE;

typedef struct {
  BOOLEAN          Valid;
  UINT32           TotalSize;
  UINT32           MemoryRangeCount;
  KARNAK_FDT_RANGE Memory[KARNAK_FDT_MAX_MEMORY_RANGES];

  BOOLEAN          FramebufferValid;
  UINT64           FramebufferBase;
  UINT32           FramebufferSize;
  UINT32           FramebufferFps;
  CHAR8            LcmName[KARNAK_FDT_LCM_NAME_LEN];
} KARNAK_FDT_INFO;

/**
  Return the stable DTB pointer published by the boot shim, or NULL.
**/
CONST VOID *
EFIAPI
KarnakFdtGetBlob (
  VOID
  );

/**
  Return TRUE when a well-formed DTB is present.
**/
BOOLEAN
EFIAPI
KarnakFdtAvailable (
  VOID
  );

/**
  Parse /memory and /chosen:atag,videolfb into Info.  Returns TRUE when at
  least the FDT header validated; Info->MemoryRangeCount may still be zero.
**/
BOOLEAN
EFIAPI
KarnakFdtParse (
  OUT KARNAK_FDT_INFO *Info
  );

/**
  Convenience: total DRAM covered by the parsed /memory nodes.
**/
UINT64
EFIAPI
KarnakFdtTotalMemory (
  IN CONST KARNAK_FDT_INFO *Info
  );

#endif /* KARNAK_FDT_LIB_H_ */
