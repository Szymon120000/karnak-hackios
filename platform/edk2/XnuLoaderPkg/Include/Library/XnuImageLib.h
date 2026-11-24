/** @file
  Mach-O 64-bit image parser shared by XnuLoader and its host tests.

  This library does one thing: given a kernel image in memory, prove that it is
  an ARM64 Mach-O executable, list its loadable segments and compute the
  physical footprint.  It never writes to the image and never trusts a size or
  offset without checking it against the buffer.

  Placement policy (does a segment collide with firmware or the DTB copy
  window?) and the boot_args builder live in XnuLoader itself, because they
  need platform knowledge that this package must not hardcode.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#ifndef XNU_IMAGE_LIB_H_
#define XNU_IMAGE_LIB_H_

#include <Uefi.h>

#define XNU_IMAGE_MAX_SEGMENTS   32
#define XNU_IMAGE_MAX_CMDS       64
#define XNU_IMAGE_NAME_LEN       20

/* <mach/machine.h> */
#define XNU_CPU_TYPE_ARM64       0x0100000CU
#define XNU_CPU_SUBTYPE_ARM64_ALL 0U

/* <mach-o/loader.h> */
#define XNU_MH_MAGIC_64          0xFEEDFACFU
#define XNU_MH_EXECUTE           2U
#define XNU_LC_SEGMENT_64        0x19U
#define XNU_LC_MAIN              0x80000028U
#define XNU_LC_UNIXTHREAD        0x05U
#define XNU_LC_THREAD            0x04U

typedef struct {
  UINT64  VmAddr;
  UINT64  VmSize;
  UINT64  FileOff;
  UINT64  FileSize;
  UINT32  MaxProt;
  UINT32  InitProt;
  BOOLEAN Loadable;      /* VmSize != 0 */
  CHAR8   Name[XNU_IMAGE_NAME_LEN];
} XNU_SEGMENT;

typedef struct {
  UINT32       CpuType;
  UINT32       CpuSubtype;
  UINT32       FileType;
  UINT32       CmdCount;

  /* Where to branch after the copy.  0 means "not declared". */
  UINT64       EntryPoint;
  BOOLEAN      HasEntry;

  UINT32       SegmentCount;
  XNU_SEGMENT  Segment[XNU_IMAGE_MAX_SEGMENTS];

  /* Lowest/highest VM address covered by any loadable segment. */
  UINT64       ImageBase;
  UINT64       ImageEnd;

  /* Total bytes that must be copied out of the file. */
  UINT64       FileBytes;
  /* Total bytes that must exist at the destination (bss included). */
  UINT64       VmBytes;
} XNU_IMAGE_INFO;

/**
  Parse and validate a Mach-O 64 ARM64 kernel image.

  @param[in]  Buffer  Image bytes; may be unaligned.
  @param[in]  Size    Number of bytes in Buffer.
  @param[out] Info    Receives the validated description.  Unchanged on error.

  @retval EFI_SUCCESS            Info is valid and self-consistent.
  @retval EFI_INVALID_PARAMETER  NULL argument, bad magic/CPU/filetype.
  @retval EFI_UNSUPPORTED        Has load commands this parser cannot account
                                 for (e.g. LC_THREAD besides LC_UNIXTHREAD) or
                                 too many segments.
  @retval EFI_VOLUME_CORRUPTED   A size/offset/segment is out of bounds.
**/
EFI_STATUS
EFIAPI
XnuImageParse (
  IN  CONST VOID     *Buffer,
  IN  UINTN          Size,
  OUT XNU_IMAGE_INFO *Info
  );

/**
  Check a proposed physical placement against a reserved range.

  The image is linked at virtual addresses (typically high VA).  XnuLoader
  copies every loadable segment to physical memory, so segment I lands at
  `DestBase + (Segment[i].VmAddr - Info->ImageBase)`.  This routine checks
  those physical spans against [RangeBase, RangeBase + RangeSize), e.g. the
  firmware, the DTB copy window or the framebuffer.

  @param[in]  Info        Parsed image.
  @param[in]  DestBase    Physical address chosen for Info->ImageBase.
  @param[in]  RangeBase   Start of a reserved range.
  @param[in]  RangeSize   Length of the reserved range (0 means empty).
  @param[out] OverlapName Optional; receives the offending segment's name.

  @retval EFI_SUCCESS        No loadable segment overlaps the range.
  @retval EFI_ACCESS_DENIED  At least one segment intersects the range.
  @retval EFI_INVALID_PARAMETER  DestBase + image size overflows.
**/
EFI_STATUS
EFIAPI
XnuImageCheckPlacement (
  IN  CONST XNU_IMAGE_INFO *Info,
  IN  UINT64               DestBase,
  IN  UINT64               RangeBase,
  IN  UINT64               RangeSize,
  OUT CONST CHAR8          **OverlapName OPTIONAL
  );

#endif /* XNU_IMAGE_LIB_H_ */
