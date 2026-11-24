/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/**
  Build and sanity-check the XNU ARM64 `boot_args` structure.

  This library only fills memory; it does not exit boot services or branch to
  the kernel.  Keeping it separate makes the layout testable on the host.
**/
#ifndef KARNAK_BOOT_ARGS_LIB_H_
#define KARNAK_BOOT_ARGS_LIB_H_

#include <Uefi.h>
#include <KarnakBootArgs.h>

/**
  Zero and populate `Args` from `Config`.

  Revision/Version are set to the values iBoot uses (2/2), the command line is
  copied with a guaranteed NUL terminator, and `MemSizeActual` falls back to
  `MemSize` when the caller passes 0.

  @param[out] Args    Structure to fill.  Must not be NULL.
  @param[in]  Config  Caller-owned inputs.  Must not be NULL.

  @retval EFI_SUCCESS            Args is populated.
  @retval EFI_INVALID_PARAMETER  Args or Config is NULL, or Config is unusable.
**/
EFI_STATUS
EFIAPI
KarnakBootArgsInit (
  OUT KARNAK_BOOT_ARGS         *Args,
  IN  CONST KARNAK_BOOT_ARGS_CONFIG *Config
  );

/**
  Check that a `boot_args` block is self-consistent enough to hand to XNU.

  This is a loader-side guard against uninitialised or truncated structures; it
  is not a substitute for the checks XNU performs.

  @param[in] Args  Structure to check.

  @retval TRUE   Fields are in range.
  @retval FALSE  Something is wrong; do not boot.
**/
BOOLEAN
EFIAPI
KarnakBootArgsValidate (
  IN CONST KARNAK_BOOT_ARGS *Args
  );

#endif /* KARNAK_BOOT_ARGS_LIB_H_ */
