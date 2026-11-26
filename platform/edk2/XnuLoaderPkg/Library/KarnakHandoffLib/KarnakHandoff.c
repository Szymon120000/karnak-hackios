/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/**
  C half of the karnak ExitBootServices / MMU-off handoff.
**/
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/KarnakHandoffLib.h>

STATIC_ASSERT (OFFSET_OF (KARNAK_HANDOFF_RANGES, KernelStart) == 0, "handoff ABI");
STATIC_ASSERT (OFFSET_OF (KARNAK_HANDOFF_RANGES, KernelEnd) == 8, "handoff ABI");
STATIC_ASSERT (OFFSET_OF (KARNAK_HANDOFF_RANGES, BootArgsStart) == 16, "handoff ABI");
STATIC_ASSERT (OFFSET_OF (KARNAK_HANDOFF_RANGES, BootArgsEnd) == 24, "handoff ABI");
STATIC_ASSERT (OFFSET_OF (KARNAK_HANDOFF_RANGES, ImageStart) == 32, "handoff ABI");
STATIC_ASSERT (OFFSET_OF (KARNAK_HANDOFF_RANGES, ImageEnd) == 40, "handoff ABI");
STATIC_ASSERT (OFFSET_OF (KARNAK_HANDOFF_RANGES, DtbStart) == 48, "handoff ABI");
STATIC_ASSERT (OFFSET_OF (KARNAK_HANDOFF_RANGES, DtbEnd) == 56, "handoff ABI");

UINT32
EFIAPI
KarnakHandoffCurrentEl (
  VOID
  )
{
  UINT64  CurrentEl;

  __asm__ volatile ("mrs %0, CurrentEL" : "=r" (CurrentEl));
  return (UINT32)(CurrentEl >> 2);
}

EFI_STATUS
EFIAPI
KarnakHandoffExitBootServices (
  IN EFI_HANDLE ImageHandle
  )
{
  EFI_STATUS  Status;
  VOID        *Map;
  UINTN       MapSize;
  UINTN       MapKey;
  UINTN       DescriptorSize;
  UINT32      DescriptorVersion;
  UINTN       Attempt;
  BOOLEAN     HaveMap;

  Map      = NULL;
  MapSize  = 0;
  HaveMap  = FALSE;

  for (Attempt = 0; Attempt < 8; Attempt++) {
    if (!HaveMap) {
      MapSize = 0;
      Status  = gBS->GetMemoryMap (
                      &MapSize, NULL, &MapKey, &DescriptorSize,
                      &DescriptorVersion
                      );
      if (Status != EFI_BUFFER_TOO_SMALL) {
        return Status;
      }

      /* Leave room for descriptors that appear while we allocate. */
      MapSize += 8 * DescriptorSize;
      Map     = AllocatePool (MapSize);
      if (Map == NULL) {
        return EFI_OUT_OF_RESOURCES;
      }

      HaveMap = TRUE;
    }

    Status = gBS->GetMemoryMap (
                    &MapSize, Map, &MapKey, &DescriptorSize,
                    &DescriptorVersion
                    );
    if (Status == EFI_BUFFER_TOO_SMALL) {
      /* A descriptor was added since the buffer was sized; retry. */
      FreePool (Map);
      Map     = NULL;
      HaveMap = FALSE;
      continue;
    }

    if (EFI_ERROR (Status)) {
      FreePool (Map);
      return Status;
    }

    Status = gBS->ExitBootServices (ImageHandle, MapKey);
    if (!EFI_ERROR (Status)) {
      return EFI_SUCCESS;
    }

    if (Status != EFI_INVALID_PARAMETER) {
      FreePool (Map);
      return Status;
    }

    /* The map changed after GetMemoryMap; take a fresh one and try again. */
  }

  FreePool (Map);
  return EFI_ABORTED;
}
