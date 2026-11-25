/** @file
  karnak bring-up probe.

  The only feedback channel on this tablet is the panel, and LK leaves the
  Amazon logo on it.  This driver paints a fixed colour pattern through the
  GOP as soon as SimpleFbDxe publishes it, waits ten seconds, then repaints
  the screen.  It answers three questions in one boot:

    - did LK hand control to BootShim/EDK2 at all (pattern appears),
    - is the framebuffer base and GOP mode correct (pattern is visible and
      upright),
    - is the machine still running after the wait (the second colour).

  Gated by PcdKarnakBringupMarkers so a normal build keeps a clean screen.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/PcdLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Protocol/GraphicsOutput.h>

STATIC
VOID
FillBand (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop,
  IN UINT32                         Y,
  IN UINT32                         Height,
  IN EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Color
  )
{
  Gop->Blt (
        Gop,
        Color,
        EfiBltVideoFill,
        0, 0,
        0, Y,
        Gop->Mode->Info->HorizontalResolution, Height,
        0
        );
}

EFI_STATUS
EFIAPI
KarnakProbeDxeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable
  )
{
  EFI_STATUS                    Status;
  EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL Red     = { 0x00, 0x00, 0xFF, 0x00 };
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL Green   = { 0x00, 0xFF, 0x00, 0x00 };
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL Blue    = { 0xFF, 0x00, 0x00, 0x00 };
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL White   = { 0xFF, 0xFF, 0xFF, 0x00 };
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL Magenta = { 0xFF, 0x00, 0xFF, 0x00 };
  UINT32                        Width;
  UINT32                        Height;
  UINT32                        Band;

  (VOID)ImageHandle;
  (VOID)SystemTable;

  if (!FixedPcdGetBool (PcdKarnakBringupMarkers)) {
    return EFI_SUCCESS;
  }

  Gop    = NULL;
  Status = gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&Gop);
  if (EFI_ERROR (Status) || (Gop == NULL) || (Gop->Mode == NULL) ||
      (Gop->Mode->Info == NULL))
  {
    DEBUG ((DEBUG_ERROR, "KarnakProbe: no usable GOP (%r)\n", Status));
    return EFI_SUCCESS;
  }

  Width  = Gop->Mode->Info->HorizontalResolution;
  Height = Gop->Mode->Info->VerticalResolution;
  Band   = Height / 8;

  FillBand (Gop, 0 * Band, Band, &Red);
  FillBand (Gop, 1 * Band, Band, &Green);
  FillBand (Gop, 2 * Band, Band, &Blue);
  FillBand (Gop, 3 * Band, Band, &White);

  DEBUG ((
    DEBUG_INFO,
    "KarnakProbe: GOP %ux%u stride %u fb=0x%lx, painted\n",
    Width,
    Height,
    Gop->Mode->Info->PixelsPerScanLine,
    (UINT64)Gop->Mode->FrameBufferBase
    ));

  // A busy-wait would hide a broken timer; Stall is exactly what we want to
  // exercise here.
  gBS->Stall (10 * 1000 * 1000);

  FillBand (Gop, 0, Height, &Magenta);

  DEBUG ((DEBUG_INFO, "KarnakProbe: still alive ten seconds later\n"));
  return EFI_SUCCESS;
}
