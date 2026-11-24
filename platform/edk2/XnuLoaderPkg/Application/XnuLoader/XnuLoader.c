/** @file
  XnuLoader: last UEFI stage before XNU.

  Current scope: find `\xnu\kernelcache` on the boot volume, validate it with
  XnuImageLib, check that its physical placement would not collide with the
  firmware or the DTB copy window, and then refuse to boot.  The boot_args
  builder and the ExitBootServices handoff are intentionally missing: see
  ../README.md for why and what they need.

  Running it is still useful: it proves the file/driver path and prints the
  segment map that the real handoff will use.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Uefi.h>
#include <Guid/FileInfo.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/LoadedImage.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/XnuImageLib.h>

#define XNU_KERNEL_PATH        L"\\xnu\\kernelcache"
#define XNU_MAX_IMAGE_BYTES    (96ULL * 1024 * 1024)

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

  Status = Root->Open (Root, &File, XNU_KERNEL_PATH, EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR (Status)) {
    Print (L"XnuLoader: %s not found (%r)\n", XNU_KERNEL_PATH, Status);
    return Status;
  }

  Status = ReadWholeFile (File, &Image, &ImageSize);
  File->Close (File);
  Root->Close (Root);
  if (EFI_ERROR (Status)) {
    Print (L"XnuLoader: cannot read %s (%r)\n", XNU_KERNEL_PATH, Status);
    return Status;
  }

  Status = XnuImageParse (Image, ImageSize, &Info);
  if (EFI_ERROR (Status)) {
    Print (L"XnuLoader: %s is not a usable ARM64 Mach-O (%r)\n",
           XNU_KERNEL_PATH, Status);
    FreePool (Image);
    return Status;
  }

  Print (L"XnuLoader: %s, %u bytes, %u segments\n",
         XNU_KERNEL_PATH, ImageSize, Info.SegmentCount);
  Print (L"  VM 0x%lx..0x%lx, file %lu bytes, VM %lu bytes\n",
         Info.ImageBase, Info.ImageEnd, Info.FileBytes, Info.VmBytes);
  for (I = 0; I < Info.SegmentCount; I++) {
    Print (L"  %a 0x%lx + 0x%lx (file 0x%lx + 0x%lx) prot %u/%u\n",
           Info.Segment[I].Name,
           Info.Segment[I].VmAddr, Info.Segment[I].VmSize,
           Info.Segment[I].FileOff, Info.Segment[I].FileSize,
           Info.Segment[I].MaxProt, Info.Segment[I].InitProt);
  }

  /* No destination is chosen yet.  When the handoff lands it calls
     XnuImageCheckPlacement(Info, DestBase, reserved...) for every firmware
     reservation and refuses to boot on EFI_ACCESS_DENIED. */
  FreePool (Image);
  Print (L"XnuLoader: boot_args handoff not implemented; refusing to boot.\n");
  return EFI_UNSUPPORTED;
}
