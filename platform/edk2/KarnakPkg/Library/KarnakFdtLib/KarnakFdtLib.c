/** @file
  Minimal read-only FDT walker for karnak.

  Only the root memory nodes and /chosen:atag,videolfb are interpreted.  All
  accesses are bounds checked against totalsize, and every integer is read
  byte-wise so an unaligned blob cannot fault.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/PcdLib.h>
#include <Library/KarnakFdtLib.h>

#define FDT_MAGIC        0xD00DFEEDU
#define FDT_BEGIN_NODE   1U
#define FDT_END_NODE     2U
#define FDT_PROP         3U
#define FDT_NOP          4U
#define FDT_END          9U

STATIC
UINT32
FdtBe32 (
  IN CONST UINT8 *P
  )
{
  return ((UINT32)P[0] << 24) | ((UINT32)P[1] << 16) |
         ((UINT32)P[2] << 8) | (UINT32)P[3];
}

STATIC
UINT64
FdtBe64 (
  IN CONST UINT8 *P
  )
{
  return ((UINT64)FdtBe32 (P) << 32) | FdtBe32 (P + 4);
}

STATIC
BOOLEAN
FdtNameIs (
  IN CONST CHAR8 *Name,
  IN CONST CHAR8 *Match
  )
{
  return AsciiStrCmp (Name, Match) == 0;
}

STATIC
BOOLEAN
FdtNameHasPrefix (
  IN CONST CHAR8 *Name,
  IN CONST CHAR8 *Prefix
  )
{
  while (*Prefix != '\0') {
    if (*Name++ != *Prefix++) {
      return FALSE;
    }
  }
  return TRUE;
}

CONST VOID *
EFIAPI
KarnakFdtGetBlob (
  VOID
  )
{
  UINT64 Address;

  Address = FixedPcdGet64 (PcdDeviceTreeStore);
  if (Address == 0) {
    return NULL;
  }
  return (CONST VOID *)(UINTN)*(CONST UINT64 *)(UINTN)Address;
}

BOOLEAN
EFIAPI
KarnakFdtAvailable (
  VOID
  )
{
  CONST UINT8 *Blob;
  UINT32       Total;

  Blob = (CONST UINT8 *)KarnakFdtGetBlob ();
  if (Blob == NULL) {
    return FALSE;
  }
  Total = FdtBe32 (Blob + 4);
  if (FdtBe32 (Blob) != FDT_MAGIC || Total < 40 ||
      Total > FixedPcdGet32 (PcdDtbStoreSize)) {
    return FALSE;
  }
  return TRUE;
}

STATIC
VOID
FdtParseMemoryReg (
  IN OUT KARNAK_FDT_INFO *Info,
  IN UINT32               AddressCells,
  IN UINT32               SizeCells,
  IN CONST UINT8         *Data,
  IN UINT32               Len
  )
{
  UINT32 Pos;
  UINT32 Stride;

  if (AddressCells != 1 && AddressCells != 2) {
    return;
  }
  if (SizeCells != 1 && SizeCells != 2) {
    return;
  }

  Stride = (AddressCells + SizeCells) * 4;
  if (Stride == 0 || (Len % Stride) != 0) {
    return;
  }

  for (Pos = 0; Pos + Stride <= Len; Pos += Stride) {
    UINT64 Base;
    UINT64 Size;

    if (AddressCells == 2) {
      Base = FdtBe64 (Data + Pos);
    } else {
      Base = FdtBe32 (Data + Pos);
    }
    if (SizeCells == 2) {
      Size = FdtBe64 (Data + Pos + AddressCells * 4);
    } else {
      Size = FdtBe32 (Data + Pos + AddressCells * 4);
    }

    if (Size == 0 || Base == 0) {
      continue;
    }
    if (Info->MemoryRangeCount >= KARNAK_FDT_MAX_MEMORY_RANGES) {
      break;
    }
    Info->Memory[Info->MemoryRangeCount].Base = Base;
    Info->Memory[Info->MemoryRangeCount].Size = Size;
    Info->MemoryRangeCount++;
  }
}

STATIC
VOID
FdtParseChosenProp (
  IN OUT KARNAK_FDT_INFO *Info,
  IN CONST CHAR8         *Name,
  IN CONST UINT8         *Data,
  IN UINT32               Len
  )
{
  UINT32 I;

  if (!FdtNameIs (Name, "atag,videolfb") || Len < 20) {
    return;
  }

  /* struct tag_videolfb {
   *   u64 fb_base; u32 islcmfound; u32 fps; u32 vram; char lcmname[];
   * } -- drivers/misc/mediatek/video/mt8163/videox/mtkfb.c
   */
  Info->FramebufferBase = FdtBe64 (Data);
  Info->FramebufferFps  = FdtBe32 (Data + 12);
  Info->FramebufferSize = FdtBe32 (Data + 16);

  if (Info->FramebufferBase == 0 || Info->FramebufferSize == 0) {
    return;
  }

  for (I = 0; I + 20 < Len && I + 1 < KARNAK_FDT_LCM_NAME_LEN; I++) {
    Info->LcmName[I] = (CHAR8)Data[20 + I];
    if (Info->LcmName[I] == '\0') {
      break;
    }
  }
  Info->LcmName[KARNAK_FDT_LCM_NAME_LEN - 1] = '\0';
  Info->FramebufferValid = TRUE;
}

BOOLEAN
EFIAPI
KarnakFdtParse (
  OUT KARNAK_FDT_INFO *Info
  )
{
  CONST UINT8 *Blob;
  CONST UINT8 *Strings;
  UINT32       Total;
  UINT32       Off;
  UINT32       StructEnd;
  UINT32       StringsSize;
  UINT32       Depth;
  UINT32       AddressCells;
  UINT32       SizeCells;
  CHAR8        NodeName[128];
  BOOLEAN      NodeIsMemory;
  BOOLEAN      NodeIsChosen;

  if (Info == NULL) {
    return FALSE;
  }
  ZeroMem (Info, sizeof (*Info));

  Blob = (CONST UINT8 *)KarnakFdtGetBlob ();
  if (Blob == NULL) {
    return FALSE;
  }

  Total       = FdtBe32 (Blob + 4);
  Off         = FdtBe32 (Blob + 8);
  Strings     = Blob + FdtBe32 (Blob + 12);
  StructEnd   = Off + FdtBe32 (Blob + 36);
  StringsSize = FdtBe32 (Blob + 32);

  if (FdtBe32 (Blob) != FDT_MAGIC || Total < 40 ||
      Total > FixedPcdGet32 (PcdDtbStoreSize) ||
      StructEnd > Total || Off >= Total) {
    return FALSE;
  }

  Info->Valid     = TRUE;
  Info->TotalSize = Total;

  Depth        = 0;
  AddressCells = 2;
  SizeCells    = 2;
  NodeIsMemory = FALSE;
  NodeIsChosen = FALSE;
  NodeName[0]  = '\0';

  while (Off + 4 <= StructEnd) {
    UINT32 Tag;

    Tag = FdtBe32 (Blob + Off);
    Off += 4;

    if (Tag == FDT_BEGIN_NODE) {
      UINT32 N;

      for (N = 0; Off + N < StructEnd && Blob[Off + N] != 0; N++) {
        if (N + 1 < sizeof (NodeName)) {
          NodeName[N] = (CHAR8)Blob[Off + N];
        }
      }
      NodeName[MIN (N, sizeof (NodeName) - 1)] = '\0';
      Off += (N + 4) & ~3U;
      Depth++;

      if (Depth == 2) {
        NodeIsMemory = FdtNameIs (NodeName, "memory") ||
                       FdtNameHasPrefix (NodeName, "memory@");
        NodeIsChosen = FdtNameIs (NodeName, "chosen") ||
                       FdtNameHasPrefix (NodeName, "chosen@");
      } else {
        NodeIsMemory = FALSE;
        NodeIsChosen = FALSE;
      }
    } else if (Tag == FDT_END_NODE) {
      if (Depth != 0) {
        Depth--;
      }
      if (Depth < 2) {
        NodeIsMemory = FALSE;
        NodeIsChosen = FALSE;
      }
    } else if (Tag == FDT_PROP) {
      UINT32       Len;
      UINT32       NameOff;
      CONST UINT8 *Data;
      CONST CHAR8 *PropName;

      if (Off + 8 > StructEnd) {
        break;
      }
      Len     = FdtBe32 (Blob + Off);
      NameOff = FdtBe32 (Blob + Off + 4);
      Off    += 8;
      if (Off + Len > StructEnd || NameOff >= StringsSize) {
        break;
      }
      Data     = Blob + Off;
      PropName = (CONST CHAR8 *)(Strings + NameOff);
      Off    += (Len + 3) & ~3U;

      if (Depth == 1) {
        if (FdtNameIs (PropName, "#address-cells") && Len == 4) {
          AddressCells = FdtBe32 (Data);
        } else if (FdtNameIs (PropName, "#size-cells") && Len == 4) {
          SizeCells = FdtBe32 (Data);
        }
      } else if (Depth == 2 && NodeIsMemory) {
        if (FdtNameIs (PropName, "reg")) {
          FdtParseMemoryReg (Info, AddressCells, SizeCells, Data, Len);
        }
      } else if (Depth == 2 && NodeIsChosen) {
        FdtParseChosenProp (Info, PropName, Data, Len);
      }
    } else if (Tag == FDT_NOP) {
      continue;
    } else if (Tag == FDT_END) {
      break;
    } else {
      break;
    }
  }

  return TRUE;
}

UINT64
EFIAPI
KarnakFdtTotalMemory (
  IN CONST KARNAK_FDT_INFO *Info
  )
{
  UINT32 I;
  UINT64 Total;

  Total = 0;
  if (Info == NULL) {
    return 0;
  }
  for (I = 0; I < Info->MemoryRangeCount; I++) {
    Total += Info->Memory[I].Size;
  }
  return Total;
}
