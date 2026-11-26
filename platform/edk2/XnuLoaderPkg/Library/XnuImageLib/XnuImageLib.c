/** @file
  Mach-O 64 parser for XnuLoader.  See XnuImageLib.h for the contract.

  All multi-byte reads are byte-wise so the input buffer may live at any
  alignment (UEFI may hand us a file loaded at an arbitrary offset).

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/XnuImageLib.h>

#define MACH_HEADER_64_SIZE   32U
#define SEGMENT_COMMAND_64    72U    /* sizeof(struct segment_command_64) */
#define ENTRY_POINT_COMMAND   24U    /* sizeof(struct entry_point_command) */

/* <mach/arm/thread_status.h>: ARM_THREAD_STATE64 has 34 64-bit registers
   (x0-x28, fp, lr, sp, pc, cpsr) and its count is expressed in 32-bit words. */
#define ARM_THREAD_STATE64          6U
#define ARM_THREAD_STATE64_COUNT    68U
#define ARM_THREAD_STATE64_PC_INDEX 32U
#define UNIXTHREAD_COMMAND_HEADER   16U   /* cmd, cmdsize, flavor, count */

/* Load command numbers we can safely ignore: they carry metadata, not bytes
   to be copied.  Anything else that describes memory is rejected. */
#define LC_UUID               0x1BU
#define LC_BUILD_VERSION      0x32U
#define LC_VERSION_MIN_MACOSX 0x24U
#define LC_VERSION_MIN_IPHONEOS 0x25U
#define LC_VERSION_MIN_TVOS   0x2FU
#define LC_VERSION_MIN_WATCHOS 0x30U
#define LC_SOURCE_VERSION     0x2AU
#define LC_FUNCTION_STARTS    0x26U
#define LC_DATA_IN_CODE       0x29U
#define LC_CODE_SIGNATURE     0x1DU
#define LC_SEGMENT_SPLIT_INFO 0x1EU
#define LC_DYLIB_CODE_SIGN_DRS 0x2BU
#define LC_LINKER_OPTIMIZATION_HINT 0x2EU
#define LC_DYLD_INFO          0x22U
#define LC_DYLD_INFO_ONLY     0x80000022U
#define LC_DYLD_EXPORTS_TRIE  0x80000033U
#define LC_DYLD_CHAINED_FIXUPS 0x80000034U
#define LC_SYMTAB             0x02U
#define LC_DYSYMTAB           0x0BU
#define LC_NOTE               0x31U

STATIC
UINT32
ReadLe32 (
  IN CONST UINT8 *P
  )
{
  return (UINT32)P[0] | ((UINT32)P[1] << 8) |
         ((UINT32)P[2] << 16) | ((UINT32)P[3] << 24);
}

STATIC
UINT64
ReadLe64 (
  IN CONST UINT8 *P
  )
{
  return (UINT64)ReadLe32 (P) | ((UINT64)ReadLe32 (P + 4) << 32);
}

STATIC
UINT64
AddSat (
  IN UINT64 A,
  IN UINT64 B
  )
{
  UINT64 R = A + B;
  return (R < A) ? MAX_UINT64 : R;
}

STATIC
BOOLEAN
Overlaps (
  IN UINT64 ABase,
  IN UINT64 ASize,
  IN UINT64 BBase,
  IN UINT64 BSize
  )
{
  if (ASize == 0 || BSize == 0) {
    return FALSE;
  }

  return (ABase < AddSat (BBase, BSize)) && (BBase < AddSat (ABase, ASize));
}

STATIC
BOOLEAN
IgnorableCommand (
  IN UINT32 Cmd
  )
{
  switch (Cmd) {
    case LC_UUID:
    case LC_BUILD_VERSION:
    case LC_VERSION_MIN_MACOSX:
    case LC_VERSION_MIN_IPHONEOS:
    case LC_VERSION_MIN_TVOS:
    case LC_VERSION_MIN_WATCHOS:
    case LC_SOURCE_VERSION:
    case LC_FUNCTION_STARTS:
    case LC_DATA_IN_CODE:
    case LC_CODE_SIGNATURE:
    case LC_SEGMENT_SPLIT_INFO:
    case LC_DYLIB_CODE_SIGN_DRS:
    case LC_LINKER_OPTIMIZATION_HINT:
    case LC_DYLD_INFO:
    case LC_DYLD_INFO_ONLY:
    case LC_DYLD_EXPORTS_TRIE:
    case LC_DYLD_CHAINED_FIXUPS:
    case LC_SYMTAB:
    case LC_DYSYMTAB:
    case LC_NOTE:
      return TRUE;
    default:
      return FALSE;
  }
}

EFI_STATUS
EFIAPI
XnuImageParse (
  IN  CONST VOID     *Buffer,
  IN  UINTN          Size,
  OUT XNU_IMAGE_INFO *Info
  )
{
  CONST UINT8   *Base;
  UINT32        Magic;
  UINT32        NCmds;
  UINT32        SizeOfCmds;
  UINT32        I;
  UINT64        CmdOff;
  UINT64        End;
  UINT64        FileBytes;
  UINT64        VmBytes;
  UINT64        ImageBase;
  UINT64        ImageEnd;
  XNU_IMAGE_INFO Parsed;

  if ((Buffer == NULL) || (Info == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  if (Size < MACH_HEADER_64_SIZE) {
    return EFI_VOLUME_CORRUPTED;
  }

  Base  = (CONST UINT8 *)Buffer;
  Magic = ReadLe32 (Base);
  if (Magic != XNU_MH_MAGIC_64) {
    return EFI_INVALID_PARAMETER;
  }

  ZeroMem (&Parsed, sizeof (Parsed));
  Parsed.CpuType    = ReadLe32 (Base + 4);
  Parsed.CpuSubtype = ReadLe32 (Base + 8);
  Parsed.FileType   = ReadLe32 (Base + 12);
  NCmds             = ReadLe32 (Base + 16);
  SizeOfCmds        = ReadLe32 (Base + 20);

  if (Parsed.CpuType != XNU_CPU_TYPE_ARM64) {
    return EFI_INVALID_PARAMETER;
  }

  if (Parsed.FileType != XNU_MH_EXECUTE) {
    return EFI_INVALID_PARAMETER;
  }

  if (NCmds > XNU_IMAGE_MAX_CMDS) {
    return EFI_UNSUPPORTED;
  }

  if ((UINT64)MACH_HEADER_64_SIZE + SizeOfCmds > Size) {
    return EFI_VOLUME_CORRUPTED;
  }

  Parsed.CmdCount = NCmds;
  CmdOff          = MACH_HEADER_64_SIZE;
  End             = (UINT64)MACH_HEADER_64_SIZE + SizeOfCmds;
  FileBytes       = 0;
  VmBytes         = 0;
  ImageBase       = MAX_UINT64;
  ImageEnd        = 0;

  for (I = 0; I < NCmds; I++) {
    UINT32 Cmd;
    UINT32 CmdSize;

    if (CmdOff + 8 > End) {
      return EFI_VOLUME_CORRUPTED;
    }

    Cmd     = ReadLe32 (Base + CmdOff);
    CmdSize = ReadLe32 (Base + CmdOff + 4);
    if ((CmdSize < 8) || ((CmdSize & 7) != 0) || (CmdOff + CmdSize > End)) {
      return EFI_VOLUME_CORRUPTED;
    }

    if (Cmd == XNU_LC_SEGMENT_64) {
      XNU_SEGMENT Seg;
      UINT32      NameLen;

      if (CmdSize < SEGMENT_COMMAND_64) {
        return EFI_VOLUME_CORRUPTED;
      }

      if (Parsed.SegmentCount >= XNU_IMAGE_MAX_SEGMENTS) {
        return EFI_UNSUPPORTED;
      }

      ZeroMem (&Seg, sizeof (Seg));
      Seg.Name[0]  = 0;
      NameLen      = 0;
      while ((NameLen + 1 < XNU_IMAGE_NAME_LEN) &&
             (Base[CmdOff + 8 + NameLen] != 0))
      {
        Seg.Name[NameLen] = (CHAR8)Base[CmdOff + 8 + NameLen];
        NameLen++;
      }

      Seg.VmAddr   = ReadLe64 (Base + CmdOff + 24);
      Seg.VmSize   = ReadLe64 (Base + CmdOff + 32);
      Seg.FileOff  = ReadLe64 (Base + CmdOff + 40);
      Seg.FileSize = ReadLe64 (Base + CmdOff + 48);
      Seg.MaxProt  = ReadLe32 (Base + CmdOff + 56);
      Seg.InitProt = ReadLe32 (Base + CmdOff + 60);
      Seg.Loadable = (BOOLEAN)(Seg.VmSize != 0);

      if (Seg.FileSize > 0) {
        if ((Seg.FileOff > Size) || (Seg.FileSize > (UINT64)Size - Seg.FileOff)) {
          return EFI_VOLUME_CORRUPTED;
        }
        FileBytes = AddSat (FileBytes, Seg.FileSize);
      }
      if (Seg.FileSize > Seg.VmSize) {
        return EFI_VOLUME_CORRUPTED;
      }

      if (Seg.Loadable) {
        VmBytes   = AddSat (VmBytes, Seg.VmSize);
        if (Seg.VmAddr < ImageBase) {
          ImageBase = Seg.VmAddr;
        }
        if (AddSat (Seg.VmAddr, Seg.VmSize) > ImageEnd) {
          ImageEnd = AddSat (Seg.VmAddr, Seg.VmSize);
        }
      }

      Parsed.Segment[Parsed.SegmentCount++] = Seg;
    } else if (Cmd == XNU_LC_MAIN) {
      if (CmdSize < ENTRY_POINT_COMMAND) {
        return EFI_VOLUME_CORRUPTED;
      }
      /* LC_MAIN gives a file offset (entryoff) relative to __TEXT. */
      if (Parsed.SegmentCount == 0) {
        return EFI_VOLUME_CORRUPTED;
      }
      Parsed.EntryPoint = Parsed.Segment[0].VmAddr + ReadLe64 (Base + CmdOff + 8);
      Parsed.HasEntry   = TRUE;
    } else if (Cmd == XNU_LC_UNIXTHREAD) {
      /*
       * XNU kernelcaches start through LC_UNIXTHREAD rather than LC_MAIN.
       * Accept only the ARM64 thread state we know how to read, and only
       * when LC_MAIN has not already given us an entry point.
       */
      UINT32 Flavor;
      UINT32 Count;
      UINT64 PcOffset;

      if (CmdSize < UNIXTHREAD_COMMAND_HEADER) {
        return EFI_VOLUME_CORRUPTED;
      }

      Flavor = ReadLe32 (Base + CmdOff + 8);
      Count  = ReadLe32 (Base + CmdOff + 12);
      if ((Flavor != ARM_THREAD_STATE64) ||
          (Count < ARM_THREAD_STATE64_COUNT))
      {
        return EFI_UNSUPPORTED;
      }

      /* 34 registers, pc is the 33rd. */
      PcOffset = (UINT64)UNIXTHREAD_COMMAND_HEADER +
                 (UINT64)ARM_THREAD_STATE64_PC_INDEX * 8;
      if (PcOffset + 8 > CmdSize) {
        return EFI_VOLUME_CORRUPTED;
      }

      if (!Parsed.HasEntry) {
        Parsed.EntryPoint = ReadLe64 (Base + CmdOff + PcOffset);
        Parsed.HasEntry   = TRUE;
      }
    } else if (Cmd == XNU_LC_THREAD) {
      return EFI_UNSUPPORTED;
    } else if (!IgnorableCommand (Cmd)) {
      /* Unknown command: it may describe loadable bytes, so refuse rather
         than silently dropping part of the image. */
      return EFI_UNSUPPORTED;
    }

    CmdOff += CmdSize;
  }

  if (Parsed.SegmentCount == 0) {
    return EFI_VOLUME_CORRUPTED;
  }

  if (ImageBase == MAX_UINT64) {
    return EFI_VOLUME_CORRUPTED;
  }

  /* LC_MAIN is optional for a kernel (XNU uses LC_UNIXTHREAD), so let the
     caller supply the entry from its manifest when HasEntry is FALSE. */

  Parsed.ImageBase = ImageBase;
  Parsed.ImageEnd  = ImageEnd;
  Parsed.FileBytes = FileBytes;
  Parsed.VmBytes   = VmBytes;

  /* Force 16 KiB page granularity: XNU links on 16K pages, and rounding up now
     keeps the placement checks honest. */
  Parsed.ImageBase &= ~((UINT64)0x3FFF);
  Parsed.ImageEnd   = (AddSat (Parsed.ImageEnd, 0x3FFF)) & ~((UINT64)0x3FFF);

  *Info = Parsed;
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
XnuImageCheckPlacement (
  IN  CONST XNU_IMAGE_INFO *Info,
  IN  UINT64               DestBase,
  IN  UINT64               RangeBase,
  IN  UINT64               RangeSize,
  OUT CONST CHAR8          **OverlapName OPTIONAL
  )
{
  UINT32 I;
  UINT64 End;

  if (Info == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  End = AddSat (DestBase, Info->ImageEnd - Info->ImageBase);
  if ((End < DestBase) || (End == MAX_UINT64)) {
    return EFI_INVALID_PARAMETER;
  }

  for (I = 0; I < Info->SegmentCount; I++) {
    CONST XNU_SEGMENT *Seg = &Info->Segment[I];
    UINT64             Phys;

    if (!Seg->Loadable) {
      continue;
    }

    Phys = DestBase + (Seg->VmAddr - Info->ImageBase);
    if (Overlaps (Phys, Seg->VmSize, RangeBase, RangeSize)) {
      if (OverlapName != NULL) {
        *OverlapName = Seg->Name;
      }
      return EFI_ACCESS_DENIED;
    }
  }

  return EFI_SUCCESS;
}
