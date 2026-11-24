/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/**
  XNU ARM64 `boot_args` builder.

  Layout source: apple-oss-distributions/xnu, tag xnu-8796.141.3,
  `pexpert/pexpert/arm64/boot.h`.  The offsets below are asserted at compile
  time here and cross-checked against the vendored header by
  `xnu/tests/boot_args_test.c`.
**/
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/KarnakBootArgsLib.h>

/*
 * Offsets from the upstream structure.  If XNU changes this layout, the build
 * fails here instead of producing a kernel that faults on its first printk.
 */
STATIC_ASSERT (sizeof (KARNAK_BOOT_ARGS) == 736, "boot_args size must match xnu-8796");
STATIC_ASSERT (OFFSET_OF (KARNAK_BOOT_ARGS, Revision) == 0, "Revision offset");
STATIC_ASSERT (OFFSET_OF (KARNAK_BOOT_ARGS, Version) == 2, "Version offset");
STATIC_ASSERT (OFFSET_OF (KARNAK_BOOT_ARGS, VirtBase) == 8, "VirtBase offset");
STATIC_ASSERT (OFFSET_OF (KARNAK_BOOT_ARGS, PhysBase) == 16, "PhysBase offset");
STATIC_ASSERT (OFFSET_OF (KARNAK_BOOT_ARGS, MemSize) == 24, "MemSize offset");
STATIC_ASSERT (OFFSET_OF (KARNAK_BOOT_ARGS, TopOfKernelData) == 32, "TopOfKernelData offset");
STATIC_ASSERT (OFFSET_OF (KARNAK_BOOT_ARGS, Video) == 40, "Video offset");
STATIC_ASSERT (OFFSET_OF (KARNAK_BOOT_ARGS, MachineType) == 88, "MachineType offset");
STATIC_ASSERT (OFFSET_OF (KARNAK_BOOT_ARGS, DeviceTree) == 96, "deviceTreeP offset");
STATIC_ASSERT (OFFSET_OF (KARNAK_BOOT_ARGS, DeviceTreeLength) == 104, "deviceTreeLength offset");
STATIC_ASSERT (OFFSET_OF (KARNAK_BOOT_ARGS, CommandLine) == 108, "CommandLine offset");
STATIC_ASSERT (OFFSET_OF (KARNAK_BOOT_ARGS, BootFlags) == 720, "bootFlags offset");
STATIC_ASSERT (OFFSET_OF (KARNAK_BOOT_ARGS, MemSizeActual) == 728, "memSizeActual offset");

EFI_STATUS
EFIAPI
KarnakBootArgsInit (
  OUT KARNAK_BOOT_ARGS              *Args,
  IN  CONST KARNAK_BOOT_ARGS_CONFIG *Config
  )
{
  UINTN  CommandLineLength;

  if ((Args == NULL) || (Config == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  if ((Config->DeviceTree == NULL) || (Config->DeviceTreeLength < sizeof (UINT32)) ||
      (Config->MemSize == 0))
  {
    return EFI_INVALID_PARAMETER;
  }

  if ((Config->TopOfKernelData < Config->PhysBase) ||
      (Config->TopOfKernelData > Config->PhysBase + Config->MemSize))
  {
    return EFI_INVALID_PARAMETER;
  }

  ZeroMem (Args, sizeof (*Args));

  Args->Revision         = KARNAK_BOOT_ARGS_REVISION;
  Args->Version          = KARNAK_BOOT_ARGS_VERSION;
  Args->VirtBase         = 0;   /* unused on arm64; XNU rebases from PhysBase */
  Args->PhysBase         = Config->PhysBase;
  Args->MemSize          = Config->MemSize;
  Args->TopOfKernelData  = Config->TopOfKernelData;
  Args->Video            = Config->Video;
  Args->MachineType      = Config->MachineType;
  Args->DeviceTree       = Config->DeviceTree;
  Args->DeviceTreeLength = Config->DeviceTreeLength;
  Args->BootFlags        = Config->BootFlags;
  Args->MemSizeActual    = (Config->MemSizeActual != 0) ? Config->MemSizeActual : Config->MemSize;

  if (Config->CommandLine != NULL) {
    CommandLineLength = AsciiStrLen (Config->CommandLine);
    if (CommandLineLength >= KARNAK_BOOT_LINE_LENGTH) {
      return EFI_INVALID_PARAMETER;
    }

    CopyMem (Args->CommandLine, Config->CommandLine, CommandLineLength);
    /* ZeroMem above guarantees the NUL terminator. */
  }

  return EFI_SUCCESS;
}

BOOLEAN
EFIAPI
KarnakBootArgsValidate (
  IN CONST KARNAK_BOOT_ARGS *Args
  )
{
  UINT64  End;

  if (Args == NULL) {
    return FALSE;
  }

  if ((Args->Revision == 0) || (Args->Revision > KARNAK_BOOT_ARGS_REVISION)) {
    return FALSE;
  }

  if (Args->Version != KARNAK_BOOT_ARGS_VERSION) {
    return FALSE;
  }

  if ((Args->DeviceTree == NULL) || (Args->DeviceTreeLength < sizeof (UINT32))) {
    return FALSE;
  }

  if (Args->MemSize == 0) {
    return FALSE;
  }

  End = Args->PhysBase + Args->MemSize;
  if (End < Args->PhysBase) {
    return FALSE;   /* overflow */
  }

  if ((Args->TopOfKernelData < Args->PhysBase) || (Args->TopOfKernelData > End)) {
    return FALSE;
  }

  if ((Args->MemSizeActual != 0) && (Args->MemSizeActual > Args->MemSize)) {
    return FALSE;
  }

  /* The last byte must be a terminator, otherwise XNU's strlcpy sees garbage. */
  if (Args->CommandLine[KARNAK_BOOT_LINE_LENGTH - 1] != '\0') {
    return FALSE;
  }

  return TRUE;
}
