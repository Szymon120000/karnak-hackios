/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/**
  ExitBootServices + MMU-off handoff helpers for karnak.

  The handoff itself is a small AArch64 routine with no stack or firmware
  dependencies.  The C half only prepares the call: it finds the boot-services
  memory map key and, for the bring-up test, describes the ranges that must be
  cleaned to the point of coherency before the caches are turned off.

  karnak boots UEFI at EL2 (MediaTek PrePi installs HCR_EL2.TGE).  XNU must run
  at EL1, so `KarnakXnuHandoff` performs an EL2 -> EL1 drop with the MMU and
  caches off when it detects EL2.  When it is already at EL1 it just disables
  them.
**/
#ifndef KARNAK_HANDOFF_LIB_H_
#define KARNAK_HANDOFF_LIB_H_

#include <Uefi.h>

/**
  Ranges cleaned to the point of coherency and invalidated from the I-cache
  before the MMU/caches are disabled.  The layout is part of the ABI with
  `KarnakXnuHandoff.S`; do not reorder.

  Empty ranges (start >= end) are skipped.
**/
typedef struct {
  UINT64  KernelStart;      /* offset  0 */
  UINT64  KernelEnd;        /* offset  8, exclusive */
  UINT64  BootArgsStart;    /* offset 16 */
  UINT64  BootArgsEnd;      /* offset 24, exclusive */
  UINT64  ImageStart;       /* offset 32: loader/payload code+data */
  UINT64  ImageEnd;         /* offset 40, exclusive */
  UINT64  DtbStart;         /* offset 48: device tree handed to XNU */
  UINT64  DtbEnd;           /* offset 56, exclusive */
} KARNAK_HANDOFF_RANGES;

/**
  Current exception level (1, 2 or 3), from CurrentEL.
**/
UINT32
EFIAPI
KarnakHandoffCurrentEl (
  VOID
  );

/**
  Leave boot services, retrying the GetMemoryMap/ExitBootServices race that
  firmware can introduce between the two calls.

  On success this returns only after the map key was accepted; the caller must
  immediately call `KarnakXnuHandoff`.  On failure nothing has been handed off
  and the return value describes the last error.

  @param[in] ImageHandle  Handle passed to the caller's entry point.
**/
EFI_STATUS
EFIAPI
KarnakHandoffExitBootServices (
  IN EFI_HANDLE ImageHandle
  );

/**
  Disable the MMU and caches, drop to EL1 if needed, and branch to `Entry`
  with x0 = `BootArgs`.  Does not return.

  @param[in] Entry     Physical (identity-mapped) entry point.
  @param[in] BootArgs  Physical address of the XNU boot_args block.
  @param[in] Ranges    Ranges to clean before the caches are disabled.
**/
VOID
EFIAPI
KarnakXnuHandoff (
  IN UINT64                       Entry,
  IN UINT64                       BootArgs,
  IN CONST KARNAK_HANDOFF_RANGES *Ranges
  );

/**
  Bring-up payload entered with the MMU and caches off.

  It paints white over the top 200 rows to prove execution reached it, then
  fills the rest of the panel with a result colour read from the boot_args and
  CurrentEL:

    green    boot_args valid, running at EL1
    yellow   boot_args valid, still at EL2 (the EL1 drop failed)
    magenta  boot_args valid, EL3 (unexpected)
    red      boot_args revision/version wrong

  It never returns and uses no stack.
**/
VOID
EFIAPI
KarnakHandoffTestPayload (
  VOID
  );

/** First byte past `KarnakHandoffTestPayload` in the loaded image. */
extern CHAR8 KarnakHandoffTestPayloadEnd[];

#endif /* KARNAK_HANDOFF_LIB_H_ */
