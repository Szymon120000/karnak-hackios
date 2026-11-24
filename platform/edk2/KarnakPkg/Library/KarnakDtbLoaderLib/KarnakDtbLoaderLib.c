/** @file
  Publish LK's device tree to DtPlatformDxe.

  The default EmbeddedPkg loader looks for a DTB file in an FV.  karnak's
  DTB is runtime-patched by LK (DRAM size, framebuffer), so we return the
  stable copy made by KarnakBootShim instead.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Uefi.h>
#include <Library/DebugLib.h>
#include <Library/DtPlatformDtbLoaderLib.h>
#include <Library/KarnakFdtLib.h>

EFI_STATUS
EFIAPI
DtPlatformLoadDtb (
  OUT VOID   **Dtb,
  OUT UINTN   *DtbSize
  )
{
  KARNAK_FDT_INFO Info;
  CONST VOID     *Blob;

  if (Dtb == NULL || DtbSize == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Blob = KarnakFdtGetBlob ();
  if (Blob == NULL || !KarnakFdtAvailable () || !KarnakFdtParse (&Info)) {
    DEBUG ((DEBUG_ERROR, "Karnak: no usable device tree\n"));
    return EFI_NOT_FOUND;
  }

  *Dtb     = (VOID *)Blob;
  *DtbSize = Info.TotalSize;
  DEBUG ((DEBUG_INFO, "Karnak: DT %u bytes at 0x%lx\n",
          Info.TotalSize, (UINT64)(UINTN)Blob));
  return EFI_SUCCESS;
}
