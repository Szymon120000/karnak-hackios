/** @file
  karnak platform identification driver.

  Prints the runtime device-tree summary so a serial log proves the chain
  BootShim -> EDK2 -> DTB handoff is alive before any OS loader runs.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Uefi.h>
#include <Library/DebugLib.h>
#include <Library/KarnakFdtLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/UefiLib.h>

EFI_STATUS
EFIAPI
KarnakInfoDxeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable
  )
{
  KARNAK_FDT_INFO Info;
  UINT32          Index;

  if (!KarnakFdtAvailable ()) {
    DEBUG ((DEBUG_WARN,
            "Karnak: boot shim did not publish a device tree\n"));
    return EFI_SUCCESS;
  }

  if (!KarnakFdtParse (&Info) || !Info.Valid) {
    DEBUG ((DEBUG_ERROR, "Karnak: device tree is malformed\n"));
    return EFI_SUCCESS;
  }

  DEBUG ((DEBUG_INFO,
          "Karnak: DT %u bytes, %u DRAM range(s), %lu MiB total\n",
          Info.TotalSize, Info.MemoryRangeCount,
          KarnakFdtTotalMemory (&Info) / (1024 * 1024)));

  for (Index = 0; Index < Info.MemoryRangeCount; Index++) {
    DEBUG ((DEBUG_INFO, "Karnak:   DRAM [0x%lx, 0x%lx)\n",
            Info.Memory[Index].Base,
            Info.Memory[Index].Base + Info.Memory[Index].Size));
  }

  if (Info.FramebufferValid) {
    DEBUG ((DEBUG_INFO,
            "Karnak: framebuffer 0x%lx + 0x%x (%a, %u fps)\n",
            Info.FramebufferBase, Info.FramebufferSize,
            Info.LcmName, Info.FramebufferFps));
  } else {
    DEBUG ((DEBUG_INFO, "Karnak: no atag,videolfb; using static fallback\n"));
  }

  return EFI_SUCCESS;
}
