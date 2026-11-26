/** @file
  karnak bring-up probe.

  The only feedback channel on this tablet is the panel, and LK leaves the
  Amazon logo on it.  This driver paints evenly spaced full-height stripes
  through the GOP as soon as KarnakFbDxe publishes it, waits three seconds,
  then repaints them green.  It answers three questions in one boot:

    - did LK hand control to BootShim/EDK2 at all (stripes appear),
    - is the framebuffer stride the panel actually scans correct (the
      stripes stay vertical; a stride mismatch shears them into diagonals),
    - is the machine still running after the wait (the green repaint).

  It also registers for the EndOfDxe and ReadyToBoot event groups and paints
  the whole screen yellow and blue respectively.  That turns the final screen
  into a BDS progress report:

    green stripes only -> DXE dispatched the probe but BDS never started
    yellow            -> EndOfDxe fired, BDS is running but stuck before boot
    blue              -> ReadyToBoot fired, BDS finished

  Gated by PcdKarnakBringupMarkers so a normal build keeps a clean screen.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Protocol/GraphicsOutput.h>
#include <Guid/EventGroup.h>

STATIC EFI_EVENT mEndOfDxeEvent;
STATIC EFI_EVENT mReadyToBootEvent;

STATIC EFI_GRAPHICS_OUTPUT_BLT_PIXEL mBlack   = { 0x00, 0x00, 0x00, 0x00 };
STATIC EFI_GRAPHICS_OUTPUT_BLT_PIXEL mWhite   = { 0xFF, 0xFF, 0xFF, 0x00 };
STATIC EFI_GRAPHICS_OUTPUT_BLT_PIXEL mRed     = { 0x00, 0x00, 0xFF, 0x00 };
STATIC EFI_GRAPHICS_OUTPUT_BLT_PIXEL mGreen   = { 0x00, 0xFF, 0x00, 0x00 };
STATIC EFI_GRAPHICS_OUTPUT_BLT_PIXEL mYellow  = { 0x00, 0xFF, 0xFF, 0x00 };
STATIC EFI_GRAPHICS_OUTPUT_BLT_PIXEL mBlue    = { 0xFF, 0x00, 0x00, 0x00 };

STATIC
VOID
FillRect (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop,
  IN UINT32                         X,
  IN UINT32                         Y,
  IN UINT32                         Width,
  IN UINT32                         Height,
  IN EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Color
  )
{
  Gop->Blt (
        Gop,
        Color,
        EfiBltVideoFill,
        0, 0,
        X, Y,
        Width, Height,
        0
        );
}

STATIC
EFI_GRAPHICS_OUTPUT_PROTOCOL *
ProbeGop (
  VOID
  )
{
  EFI_STATUS                    Status;
  EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop;

  Gop    = NULL;
  Status = gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&Gop);
  if (EFI_ERROR (Status) || (Gop == NULL) || (Gop->Mode == NULL) ||
      (Gop->Mode->Info == NULL))
  {
    return NULL;
  }

  return Gop;
}

STATIC
VOID
PrintBanner (
  IN CONST CHAR16 *Text,
  IN UINTN         Attribute
  )
{
  if ((gST != NULL) && (gST->ConOut != NULL)) {
    gST->ConOut->SetAttribute (gST->ConOut, Attribute);
    gST->ConOut->OutputString (gST->ConOut, (CHAR16 *)Text);
  }
}

STATIC
VOID
EFIAPI
EndOfDxeNotify (
  IN EFI_EVENT Event,
  IN VOID     *Context
  )
{
  EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop;

  (VOID)Event;
  (VOID)Context;

  Gop = ProbeGop ();
  if (Gop != NULL) {
    FillRect (Gop, 0, 0, Gop->Mode->Info->HorizontalResolution,
              Gop->Mode->Info->VerticalResolution, &mYellow);
  }

  PrintBanner (L"KARNAK END OF DXE\r\n", EFI_BLACK | EFI_BACKGROUND_BROWN);
}

STATIC
VOID
DrawTestGlyph (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop
  )
{
  STATIC CONST UINT8  GlyphA[8] = { 0x18, 0x24, 0x42, 0x7E, 0x42, 0x42, 0x42, 0x00 };
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  *Buffer;
  UINTN                           Scale = 16;
  UINTN                           Size  = 8 * Scale;

  Buffer = AllocatePool (Size * Size * sizeof (*Buffer));
  if (Buffer == NULL) {
    return;
  }

  for (UINTN y = 0; y < Size; y++) {
    for (UINTN x = 0; x < Size; x++) {
      UINT8 Bit = (GlyphA[y / Scale] >> (7 - (x / Scale))) & 1;
      Buffer[y * Size + x] = Bit ? mWhite : mGreen;
    }
  }

  Gop->Blt (
        Gop,
        Buffer,
        EfiBltBufferToVideo,
        0, 0,
        500, 600,
        (UINT32)Size, (UINT32)Size,
        (UINTN)Size * sizeof (*Buffer)
        );
  FreePool (Buffer);
}

/*
 * Solid 64x64 squares through EfiBltBufferToVideo, independent of the
 * console: separates "BufferToVideo is broken" from "the console text
 * renderer is broken".
 */
STATIC
VOID
DrawColorSquare (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop,
  IN UINT32                         X,
  IN UINT32                         Y,
  IN EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Color
  )
{
  UINTN                           Size = 64;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  *Buffer;

  Buffer = AllocatePool (Size * Size * sizeof (*Buffer));
  if (Buffer == NULL) {
    return;
  }

  for (UINTN i = 0; i < Size * Size; i++) {
    Buffer[i] = *Color;
  }

  Gop->Blt (Gop, Buffer, EfiBltBufferToVideo, 0, 0, X, Y,
            (UINT32)Size, (UINT32)Size, (UINTN)Size * sizeof (*Buffer));
  FreePool (Buffer);
}

STATIC
VOID
EFIAPI
ReadyToBootNotify (
  IN EFI_EVENT Event,
  IN VOID     *Context
  )
{
  EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop;

  (VOID)Event;
  (VOID)Context;

  Gop = ProbeGop ();
  if (Gop != NULL) {
    FillRect (Gop, 0, 0, Gop->Mode->Info->HorizontalResolution,
              Gop->Mode->Info->VerticalResolution, &mBlack);
    /* Left-to-right: red, green, white.  These come from a plain
     * BltBufferToVideo buffer, with no console or font involved. */
    DrawColorSquare (Gop, 100, 400, &mRed);
    DrawColorSquare (Gop, 200, 400, &mGreen);
    DrawColorSquare (Gop, 300, 400, &mWhite);
    DrawTestGlyph (Gop);
  }

  PrintBanner (L"KARNAK READY TO BOOT V5\r\n", EFI_WHITE | EFI_BACKGROUND_BLUE);
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
  UINT32                        Width;
  UINT32                        Height;
  UINT32                        Stripe;

  (VOID)ImageHandle;
  (VOID)SystemTable;

  if (!FixedPcdGetBool (PcdKarnakBringupMarkers)) {
    return EFI_SUCCESS;
  }

  Gop = ProbeGop ();
  if (Gop == NULL) {
    DEBUG ((DEBUG_ERROR, "KarnakProbe: no usable GOP\n"));
    return EFI_SUCCESS;
  }

  Width  = Gop->Mode->Info->HorizontalResolution;
  Height = Gop->Mode->Info->VerticalResolution;

  /*
   * Stride probe.  A solid fill is invariant to the panel's real line
   * length, but a vertical line is not: if the hardware scans with a
   * different PixelsPerScanLine the line shears into diagonals.  Draw
   * evenly spaced full-height stripes; straight means the stride is right.
   */
  FillRect (Gop, 0, 0, Width, Height, &mBlack);
  for (Stripe = 0; Stripe < 8; Stripe++) {
    FillRect (Gop, Stripe * (Width / 8), 0, 4, Height, &mWhite);
  }

  DEBUG ((
    DEBUG_INFO,
    "KarnakProbe: GOP %ux%u stride %u fb=0x%lx, stripes painted\n",
    Width,
    Height,
    Gop->Mode->Info->PixelsPerScanLine,
    (UINT64)Gop->Mode->FrameBufferBase
    ));

  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  EndOfDxeNotify,
                  NULL,
                  &gEfiEndOfDxeEventGroupGuid,
                  &mEndOfDxeEvent
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "KarnakProbe: EndOfDxe event failed: %r\n", Status));
  }

  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  ReadyToBootNotify,
                  NULL,
                  &gEfiEventReadyToBootGuid,
                  &mReadyToBootEvent
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "KarnakProbe: ReadyToBoot event failed: %r\n", Status));
  }

  // Stall exercises the timer path that BDS needs; keep it short so a
  // bring-up cycle is not dominated by this driver.
  gBS->Stall (3 * 1000 * 1000);

  for (Stripe = 0; Stripe < 8; Stripe++) {
    FillRect (Gop, Stripe * (Width / 8), 0, 4, Height, &mGreen);
  }

  PrintBanner (L"KARNAK PROBE V5 TEXT 0123456789\r\n", EFI_WHITE | EFI_BACKGROUND_BLUE);
  DEBUG ((DEBUG_INFO, "KarnakProbe: still alive after the stall\n"));
  return EFI_SUCCESS;
}
