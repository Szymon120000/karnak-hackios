/** @file
  XnuLoader: last UEFI stage before XNU.

  Finds `\xnu\kernelcache` on the boot volume, validates it with XnuImageLib,
  copies its segments into allocator-provided memory, builds the XNU ARM64
  boot_args block, leaves boot services and branches to the entry point with
  the MMU and caches off (see KarnakHandoffLib).

  A kernel built for karnak does not exist yet; the path is exercised by
  KarnakProbeDxe's PcdKarnakHandoffSelfTest, which uses the same handoff
  routine with a payload that paints the result on the panel.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Uefi.h>
#include <Guid/FileInfo.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/LoadedImage.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <karnak/hardware_map.h>
#include <Library/KarnakBootArgsLib.h>
#include <Library/KarnakHandoffLib.h>
#include <Library/XnuImageLib.h>

#define XNU_MAX_IMAGE_BYTES    (96ULL * 1024 * 1024)
#define FDT_MAGIC              0xD00DFEEDU

STATIC
EFI_STATUS
ReadWholeFile (
  IN  EFI_FILE_PROTOCOL  *File,
  OUT VOID               **Buffer,
  OUT UINTN              *Size
  )
{
  EFI_STATUS  Status;
  UINTN       InfoSize;
  EFI_FILE_INFO *Info;
  UINTN       ReadSize;

  InfoSize = 0;
  Status   = File->GetInfo (File, &gEfiFileInfoGuid, &InfoSize, NULL);
  if (Status != EFI_BUFFER_TOO_SMALL) {
    return EFI_NOT_FOUND;
  }

  Info = AllocatePool (InfoSize);
  if (Info == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Status = File->GetInfo (File, &gEfiFileInfoGuid, &InfoSize, Info);
  if (EFI_ERROR (Status)) {
    FreePool (Info);
    return Status;
  }

  if ((Info->FileSize == 0) || (Info->FileSize > XNU_MAX_IMAGE_BYTES)) {
    FreePool (Info);
    return EFI_UNSUPPORTED;
  }

  *Buffer = AllocatePool ((UINTN)Info->FileSize);
  if (*Buffer == NULL) {
    FreePool (Info);
    return EFI_OUT_OF_RESOURCES;
  }

  ReadSize = (UINTN)Info->FileSize;
  Status   = File->Read (File, &ReadSize, *Buffer);
  FreePool (Info);

  if (EFI_ERROR (Status) || (ReadSize == 0)) {
    FreePool (*Buffer);
    *Buffer = NULL;
    return EFI_VOLUME_CORRUPTED;
  }

  *Size = ReadSize;
  return EFI_SUCCESS;
}

/**
  Build a boot_args preview from the DTB that KarnakBootShim saved.

  This is diagnostic only: it proves the shim pointer, the FDT header and the
  boot_args layout agree on real hardware.  It deliberately uses the DTS-floor
  DRAM values for now; the handoff will take them from the live memory map.
**/
STATIC
VOID
PreviewBootArgs (
  VOID
  )
{
  volatile UINT64           *PointerStore;
  UINT64                    DtbAddress;
  volatile UINT32           *Dtb;
  UINT32                    DtbSize;
  KARNAK_BOOT_ARGS          Args;
  KARNAK_BOOT_ARGS_CONFIG   Config;
  EFI_STATUS                Status;

  PointerStore = (volatile UINT64 *)(UINTN)KARN_DTB_PTR_STORE;
  DtbAddress   = *PointerStore;
  if (DtbAddress == 0) {
    Print (L"XnuLoader: no shim DTB pointer yet; boot_args preview skipped\n");
    return;
  }

  Dtb = (volatile UINT32 *)(UINTN)DtbAddress;
  if (SwapBytes32 (Dtb[0]) != FDT_MAGIC) {
    Print (L"XnuLoader: shim pointer 0x%lx is not an FDT (%08x)\n",
           DtbAddress, SwapBytes32 (Dtb[0]));
    return;
  }

  DtbSize = SwapBytes32 (Dtb[1]);
  if ((DtbSize < sizeof (UINT32) * 2) || (DtbSize > KARN_DTB_STORE_SIZE)) {
    Print (L"XnuLoader: DTB at 0x%lx has implausible size 0x%x\n",
           DtbAddress, DtbSize);
    return;
  }

  ZeroMem (&Config, sizeof (Config));
  Config.PhysBase         = KARN_DRAM_BASE;
  Config.MemSize          = KARN_DRAM_SIZE_DTS;
  /* Preview only: the real value is the end of the loaded kernel image. */
  Config.TopOfKernelData  = KARN_DRAM_END_DTS;
  Config.DeviceTree       = (VOID *)(UINTN)DtbAddress;
  Config.DeviceTreeLength = DtbSize;

  Status = KarnakBootArgsInit (&Args, &Config);
  if (EFI_ERROR (Status)) {
    Print (L"XnuLoader: boot_args preview failed (%r)\n", Status);
    return;
  }

  Print (L"XnuLoader: boot_args preview: %u bytes, rev %u, DT 0x%lx + 0x%x\n",
         (UINT32)sizeof (Args), Args.Revision, DtbAddress, DtbSize);
  if (KarnakBootArgsValidate (&Args)) {
    Print (L"XnuLoader: boot_args preview validates (DRAM floor 0x%lx)\n",
           Config.MemSize);
  } else {
    Print (L"XnuLoader: boot_args preview does not validate\n");
  }
}

STATIC CONST struct {
  UINT64      Base;
  UINT64      Size;
  CONST CHAR8 *Name;
} mReserved[] = {
  { KARN_UEFI_BASE,        KARN_UEFI_SIZE,             "firmware" },
  { KARN_DTB_STORE_BASE,   KARN_DTB_STORE_SIZE,        "DTB copy" },
  { KARN_DTB_PTR_STORE,    KARN_DTB_PTR_STORE_SIZE,    "DTB pointer" },
  { KARN_ATF_BASE,         KARN_ATF_SIZE,              "ATF" },
  { KARN_FB_BASE_FALLBACK, KARN_FB_RESERVE_BYTES,      "framebuffer" },
};

STATIC
EFI_STATUS
ReadShimDtb (
  OUT UINT64 *DtbAddress,
  OUT UINT32 *DtbSize
  )
{
  volatile UINT64 *PointerStore;
  volatile UINT32 *Dtb;
  UINT64           Address;
  UINT32           Size;

  PointerStore = (volatile UINT64 *)(UINTN)KARN_DTB_PTR_STORE;
  Address      = *PointerStore;
  if (Address == 0) {
    return EFI_NOT_FOUND;
  }

  Dtb = (volatile UINT32 *)(UINTN)Address;
  if (SwapBytes32 (Dtb[0]) != FDT_MAGIC) {
    return EFI_UNSUPPORTED;
  }

  Size = SwapBytes32 (Dtb[1]);
  if ((Size < sizeof (UINT32) * 2) || (Size > KARN_DTB_STORE_SIZE)) {
    return EFI_VOLUME_CORRUPTED;
  }

  *DtbAddress = Address;
  *DtbSize    = Size;
  return EFI_SUCCESS;
}

/**
  Copy the parsed kernel to a buffer the allocator gave us, build boot_args and
  hand control to it.  Returns only on failure; on success the machine is
  running XNU with no boot services.

  The destination comes from the UEFI allocator, so it cannot overlap
  firmware, the DTB or the framebuffer; the explicit reservation checks are a
  second line of defence for those known ranges.
**/
STATIC
EFI_STATUS
BootKernel (
  IN EFI_HANDLE                ImageHandle,
  IN EFI_LOADED_IMAGE_PROTOCOL *LoadedImage,
  IN CONST XNU_IMAGE_INFO      *Info,
  IN CONST VOID                *Image
  )
{
  EFI_STATUS                    Status;
  EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop;
  EFI_PHYSICAL_ADDRESS          DestPage;
  EFI_PHYSICAL_ADDRESS          ArgsPage;
  UINT64                        Span;
  UINT64                        Entry;
  UINT64                        DtbAddress;
  UINT32                        DtbSize;
  UINTN                         Pages;
  UINTN                         Index;
  CONST CHAR8                  *OverlapName;
  KARNAK_BOOT_ARGS_CONFIG       Config;
  KARNAK_HANDOFF_RANGES         Ranges;

  if (!Info->HasEntry) {
    Print (L"XnuLoader: kernel has no entry point\n");
    return EFI_UNSUPPORTED;
  }

  if ((Info->EntryPoint < Info->ImageBase) ||
      (Info->EntryPoint >= Info->ImageEnd))
  {
    Print (L"XnuLoader: entry 0x%lx is outside the image\n", Info->EntryPoint);
    return EFI_UNSUPPORTED;
  }

  Span = Info->ImageEnd - Info->ImageBase;
  if ((Span == 0) || (Span > MAX_UINT32)) {
    Print (L"XnuLoader: implausible image span 0x%lx\n", Span);
    return EFI_UNSUPPORTED;
  }

  Status = ReadShimDtb (&DtbAddress, &DtbSize);
  if (EFI_ERROR (Status)) {
    Print (L"XnuLoader: no usable shim DTB (%r)\n", Status);
    return Status;
  }

  Pages  = EFI_SIZE_TO_PAGES ((UINTN)Span);
  DestPage = 0;
  Status = gBS->AllocatePages (AllocateAnyPages, EfiLoaderData, Pages, &DestPage);
  if (EFI_ERROR (Status)) {
    Print (L"XnuLoader: cannot allocate 0x%lx bytes for the kernel (%r)\n",
           Span, Status);
    return Status;
  }

  for (Index = 0; Index < ARRAY_SIZE (mReserved); Index++) {
    if (XnuImageCheckPlacement (
          Info, DestPage,
          mReserved[Index].Base, mReserved[Index].Size,
          &OverlapName
          ) != EFI_SUCCESS)
    {
      Print (L"XnuLoader: segment %a overlaps %a at 0x%lx\n",
             OverlapName, mReserved[Index].Name, DestPage);
      gBS->FreePages (DestPage, Pages);
      return EFI_ACCESS_DENIED;
    }
  }

  ZeroMem ((VOID *)(UINTN)DestPage, Pages * EFI_PAGE_SIZE);
  for (Index = 0; Index < Info->SegmentCount; Index++) {
    CONST XNU_SEGMENT *Segment = &Info->Segment[Index];
    UINT64             Destination;

    if (!Segment->Loadable) {
      continue;
    }

    Destination = (UINT64)DestPage + (Segment->VmAddr - Info->ImageBase);
    if (Segment->FileSize != 0) {
      CopyMem (
        (VOID *)(UINTN)Destination,
        (CONST UINT8 *)Image + Segment->FileOff,
        (UINTN)Segment->FileSize
        );
    }
  }

  Entry = (UINT64)DestPage + (Info->EntryPoint - Info->ImageBase);

  ArgsPage = 0;
  Status   = gBS->AllocatePages (AllocateAnyPages, EfiLoaderData, 1, &ArgsPage);
  if (EFI_ERROR (Status)) {
    Print (L"XnuLoader: cannot allocate boot_args (%r)\n", Status);
    gBS->FreePages (DestPage, Pages);
    return Status;
  }

  ZeroMem ((VOID *)(UINTN)ArgsPage, EFI_PAGE_SIZE);

  Gop    = NULL;
  Status = gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&Gop);
  if (EFI_ERROR (Status)) {
    Gop = NULL;
  }

  ZeroMem (&Config, sizeof (Config));
  Config.PhysBase        = KARN_DRAM_BASE;
  /*
   * TODO(VERIFY_MEMSIZE): the live DRAM size should come from the memory map
   * or the shim DTB, not the 512 MiB DTS floor.
   */
  Config.MemSize         = KARN_DRAM_SIZE_DTS;
  Config.TopOfKernelData = (UINT64)DestPage + Span;
  Config.DeviceTree      = (VOID *)(UINTN)DtbAddress;
  Config.DeviceTreeLength = DtbSize;
  Config.CommandLine     = "karnak xnu";
  if (Gop != NULL) {
    Config.Video.BaseAddress = Gop->Mode->FrameBufferBase;
    Config.Video.RowBytes    = (UINT64)Gop->Mode->Info->PixelsPerScanLine * 4;
    Config.Video.Width       = Gop->Mode->Info->HorizontalResolution;
    Config.Video.Height      = Gop->Mode->Info->VerticalResolution;
    Config.Video.Depth       = 32;
  } else {
    /*
     * No GOP at handoff time.  The panel geometry is fixed by the LK VRAM
     * reservation (the same constants BootShim and KarnakFbDxe use), so
     * pass them instead of leaving the kernel with a zeroed Video structure.
     */
    Print (L"XnuLoader: no GOP; using the LK framebuffer constants\n");
    Config.Video.BaseAddress = KARN_FB_BASE_FALLBACK;
    Config.Video.RowBytes    = KARN_FB_STRIDE;
    Config.Video.Width       = KARN_FB_WIDTH;
    Config.Video.Height      = KARN_FB_HEIGHT;
    Config.Video.Depth       = 32;
  }

  Status = KarnakBootArgsInit ((KARNAK_BOOT_ARGS *)(UINTN)ArgsPage, &Config);
  if (EFI_ERROR (Status)) {
    Print (L"XnuLoader: boot_args init failed (%r)\n", Status);
    gBS->FreePages (ArgsPage, 1);
    gBS->FreePages (DestPage, Pages);
    return Status;
  }

  Ranges.KernelStart   = DestPage;
  Ranges.KernelEnd     = (UINT64)DestPage + Span;
  Ranges.BootArgsStart = ArgsPage;
  Ranges.BootArgsEnd   = (UINT64)ArgsPage + sizeof (KARNAK_BOOT_ARGS);
  Ranges.ImageStart    = (UINT64)(UINTN)LoadedImage->ImageBase;
  Ranges.ImageEnd      = Ranges.ImageStart + LoadedImage->ImageSize;
  Ranges.DtbStart      = DtbAddress;
  Ranges.DtbEnd        = DtbAddress + DtbSize;

  Print (L"XnuLoader: kernel 0x%lx..0x%lx, entry 0x%lx, args 0x%lx\n",
         Ranges.KernelStart, Ranges.KernelEnd, Entry, (UINT64)ArgsPage);
  Print (L"XnuLoader: video base 0x%lx %lux%lu depth %lu stride 0x%lx\n",
         (UINT64)Config.Video.BaseAddress,
         (UINT64)Config.Video.Width, (UINT64)Config.Video.Height,
         (UINT64)Config.Video.Depth, (UINT64)Config.Video.RowBytes);
  /*
   * Paint a magenta band low on the panel directly through the GOP base.
   * If this shows up the physical address the kernel is given really is the
   * visible front buffer, which separates "kernel never mapped it" from
   * "wrong address".  Rows 900..1100 are black in the loader text screen.
   */
  if (Config.Video.BaseAddress != 0) {
    volatile UINT32 *Fb = (volatile UINT32 *)(UINTN)Config.Video.BaseAddress;
    UINTN            X;
    UINTN            Y;

    for (Y = 900; Y < 1100; Y++) {
      for (X = 0; X < Config.Video.Width; X++) {
        Fb[Y * (Config.Video.RowBytes / 4) + X] = 0x00FF00FF;
      }
    }
  }
  Print (L"XnuLoader: EL%u; exiting boot services and handing off\n",
         KarnakHandoffCurrentEl ());

  Status = KarnakHandoffExitBootServices (ImageHandle);
  if (EFI_ERROR (Status)) {
    Print (L"XnuLoader: ExitBootServices failed (%r)\n", Status);
    gBS->FreePages (ArgsPage, 1);
    gBS->FreePages (DestPage, Pages);
    return Status;
  }

  KarnakXnuHandoff (Entry, (UINT64)ArgsPage, &Ranges);
  CpuDeadLoop ();
  return EFI_ABORTED;
}

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS                  Status;
  EFI_LOADED_IMAGE_PROTOCOL   *LoadedImage;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs;
  EFI_FILE_PROTOCOL           *Root;
  EFI_FILE_PROTOCOL           *File;
  VOID                        *Image;
  UINTN                       ImageSize;
  XNU_IMAGE_INFO              Info;
  UINT32                      I;
  UINTN                       PathIndex;
  CONST CHAR16               *KernelPath;
  STATIC CONST CHAR16 *mKernelPaths[] = {
    /* The real layout: a kernel under /xnu on the boot partition. */
    L"\\xnu\\kernelcache",
    /* FvSimpleFileSystem has no directories; the self-test kernel is a
       root-level FV file. */
    L"\\kernelcache",
  };

  Status = gBS->HandleProtocol (
                  ImageHandle,
                  &gEfiLoadedImageProtocolGuid,
                  (VOID **)&LoadedImage
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->HandleProtocol (
                  LoadedImage->DeviceHandle,
                  &gEfiSimpleFileSystemProtocolGuid,
                  (VOID **)&Fs
                  );
  if (EFI_ERROR (Status)) {
    Print (L"XnuLoader: boot volume has no SimpleFileSystem\n");
    return Status;
  }

  Status = Fs->OpenVolume (Fs, &Root);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  KernelPath = NULL;
  Status     = EFI_NOT_FOUND;
  for (PathIndex = 0; PathIndex < ARRAY_SIZE (mKernelPaths); PathIndex++) {
    Status = Root->Open (
                   Root, &File, (CHAR16 *)mKernelPaths[PathIndex],
                   EFI_FILE_MODE_READ, 0
                   );
    if (!EFI_ERROR (Status)) {
      KernelPath = mKernelPaths[PathIndex];
      break;
    }
  }

  if (KernelPath == NULL) {
    Print (L"XnuLoader: no \\xnu\\kernelcache or \\kernelcache on this volume (%r)\n",
           Status);
    Root->Close (Root);
    return Status;
  }

  Status = ReadWholeFile (File, &Image, &ImageSize);
  File->Close (File);
  Root->Close (Root);
  if (EFI_ERROR (Status)) {
    Print (L"XnuLoader: cannot read %s (%r)\n", KernelPath, Status);
    return Status;
  }

  Print (L"XnuLoader: read %s: %lu bytes, first 16:",
         KernelPath, (UINT64)ImageSize);
  if (ImageSize >= 16) {
    UINT32  *Words;
    Words = (UINT32 *)Image;
    Print (L" %08x %08x %08x %08x\n", Words[0], Words[1], Words[2], Words[3]);
  } else {
    Print (L" (too short)\n");
  }

  Status = XnuImageParse (Image, ImageSize, &Info);
  if (EFI_ERROR (Status)) {
    Print (L"XnuLoader: %s is not a usable ARM64 Mach-O (%r)\n",
           KernelPath, Status);
    FreePool (Image);
    return Status;
  }

  Print (L"XnuLoader: %s, %u bytes, %u segments\n",
         KernelPath, ImageSize, Info.SegmentCount);
  Print (L"  VM 0x%lx..0x%lx, file %lu bytes, VM %lu bytes\n",
         Info.ImageBase, Info.ImageEnd, Info.FileBytes, Info.VmBytes);
  for (I = 0; I < Info.SegmentCount; I++) {
    Print (L"  %a 0x%lx + 0x%lx (file 0x%lx + 0x%lx) prot %u/%u\n",
           Info.Segment[I].Name,
           Info.Segment[I].VmAddr, Info.Segment[I].VmSize,
           Info.Segment[I].FileOff, Info.Segment[I].FileSize,
           Info.Segment[I].MaxProt, Info.Segment[I].InitProt);
  }

  PreviewBootArgs ();

  Status = BootKernel (ImageHandle, LoadedImage, &Info, Image);
  FreePool (Image);
  return Status;
}
