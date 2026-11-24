/** @file
  karnak memory map.

  The DRAM extent is taken from the live device tree (LK patches it to the
  real size), then the firmware regions and the MediaTek reserved-memory
  nodes are carved out.  If no DTB is available the build-time floor from
  the Amazon 3.18 DTS is used (0x40000000 + 0x20000000).

  Sources for the reserved ranges:
    arch/arm64/boot/dts/karnak.dts  reserved-memory nodes
    drivers/misc/mediatek/video/mt8163/videox/mtkfb.c fallback fb_base

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/KarnakFdtLib.h>
#include <Library/PlatformMemoryMapLib.h>

#define KARN_DRAM_BASE_DTS        0x40000000ULL
#define KARN_DRAM_SIZE_DTS        0x20000000ULL
#define KARN_ATF_BASE             0x43000000ULL
#define KARN_ATF_SIZE             0x00030000ULL
#define KARN_RAM_CONSOLE_BASE     0x44400000ULL
#define KARN_RAM_CONSOLE_SIZE     0x00200000ULL
#define KARN_MDUMP_BASE           0x4BD00000ULL
#define KARN_MDUMP_SIZE           0x00300000ULL
#define KARN_DTB_STORE_BASE       0x4FE00000ULL
#define KARN_DTB_STORE_SIZE       0x00100000ULL
#define KARN_DTB_PTR_STORE        0x4FF00000ULL
#define KARN_DTB_PTR_STORE_SIZE   0x00001000ULL
#define KARN_UEFI_BASE            0x50000000ULL
#define KARN_UEFI_SIZE            0x00700000ULL
#define KARN_UEFI_STACK_BASE      0x50800000ULL
#define KARN_UEFI_STACK_SIZE      0x00040000ULL
#define KARN_UEFI_VECTOR_BASE     0x50840000ULL
#define KARN_UEFI_VECTOR_SIZE     0x00010000ULL
#define KARN_UEFI_POOL_BASE       0x50850000ULL
#define KARN_UEFI_POOL_SIZE       0x02000000ULL
#define KARN_FB_FALLBACK_BASE     0x5EE00000ULL
#define KARN_FB_FALLBACK_SIZE     0x01000000ULL

#define KARN_MAX_HOLES            24

typedef struct {
  CHAR8                        Name[32];
  UINT64                       Base;
  UINT64                       Size;
  EFI_MEMORY_TYPE              MemoryType;
  ARM_MEMORY_REGION_ATTRIBUTES Attributes;
} KARN_HOLE;

STATIC ARM_MEMORY_REGION_DESCRIPTOR_EX gMap[MAX_ARM_MEMORY_REGION_DESCRIPTOR_COUNT];
STATIC KARN_HOLE                        mHoles[KARN_MAX_HOLES];
STATIC UINT32                           mHoleCount;
STATIC UINT64                           mDramBase;
STATIC UINT64                           mDramEnd;
STATIC BOOLEAN                          mReady;

STATIC UINT32 mMapCount;

STATIC
VOID
MapAdd (
  IN CONST CHAR8                 *Name,
  IN UINT64                       Address,
  IN UINT64                       Length,
  IN DeviceMemoryAddHob           HobOption,
  IN EFI_RESOURCE_TYPE            ResourceType,
  IN EFI_RESOURCE_ATTRIBUTE_TYPE  ResourceAttribute,
  IN EFI_MEMORY_TYPE              MemoryType,
  IN ARM_MEMORY_REGION_ATTRIBUTES ArmAttributes
  )
{
  ARM_MEMORY_REGION_DESCRIPTOR_EX *Entry;

  if (Length == 0 || mMapCount >= MAX_ARM_MEMORY_REGION_DESCRIPTOR_COUNT - 1) {
    return;
  }
  Entry = &gMap[mMapCount++];
  ZeroMem (Entry, sizeof (*Entry));
  AsciiStrCpyS (Entry->Name, sizeof (Entry->Name), Name);
  Entry->Address           = Address;
  Entry->Length            = Length;
  Entry->HobOption         = HobOption;
  Entry->ResourceType      = ResourceType;
  Entry->ResourceAttribute = ResourceAttribute;
  Entry->MemoryType        = MemoryType;
  Entry->ArmAttributes     = ArmAttributes;
}

STATIC
VOID
AddHole (
  IN CONST CHAR8                 *Name,
  IN UINT64                       Base,
  IN UINT64                       Size,
  IN EFI_MEMORY_TYPE              MemoryType,
  IN ARM_MEMORY_REGION_ATTRIBUTES Attributes
  )
{
  UINT64 End;

  if (Size == 0 || mHoleCount >= KARN_MAX_HOLES) {
    return;
  }
  if (Base < mDramBase) {
    Size -= (mDramBase - Base);
    Base  = mDramBase;
  }
  End = Base + Size;
  if (End > mDramEnd) {
    if (Base >= mDramEnd) {
      return;
    }
    End = mDramEnd;
  }
  if (End <= Base) {
    return;
  }

  ZeroMem (&mHoles[mHoleCount], sizeof (mHoles[mHoleCount]));
  AsciiStrCpyS (mHoles[mHoleCount].Name, sizeof (mHoles[mHoleCount].Name), Name);
  mHoles[mHoleCount].Base       = Base;
  mHoles[mHoleCount].Size       = End - Base;
  mHoles[mHoleCount].MemoryType = MemoryType;
  mHoles[mHoleCount].Attributes = Attributes;
  mHoleCount++;
}

STATIC
VOID
SortHoles (
  VOID
  )
{
  UINT32 I;
  UINT32 J;

  for (I = 1; I < mHoleCount; I++) {
    KARN_HOLE Key = mHoles[I];
    J = I;
    while (J > 0 && mHoles[J - 1].Base > Key.Base) {
      mHoles[J] = mHoles[J - 1];
      J--;
    }
    mHoles[J] = Key;
  }
}

STATIC
VOID
BuildMap (
  VOID
  )
{
  KARNAK_FDT_INFO Info;
  UINT32          I;
  UINT64          Cursor;
  UINT64          FbBase;
  UINT64          FbSize;
  BOOLEAN         HaveDtb;

  if (mReady) {
    return;
  }

  mDramBase = KARN_DRAM_BASE_DTS;
  mDramEnd  = KARN_DRAM_BASE_DTS + KARN_DRAM_SIZE_DTS;

  HaveDtb = KarnakFdtParse (&Info);
  if (HaveDtb && Info.MemoryRangeCount > 0) {
    mDramBase = Info.Memory[0].Base;
    mDramEnd  = Info.Memory[0].Base + Info.Memory[0].Size;
    for (I = 1; I < Info.MemoryRangeCount; I++) {
      UINT64 End = Info.Memory[I].Base + Info.Memory[I].Size;
      if (End > mDramEnd) {
        mDramEnd = End;
      }
    }
  }

  FbBase = KARN_FB_FALLBACK_BASE;
  FbSize = KARN_FB_FALLBACK_SIZE;
  if (HaveDtb && Info.FramebufferValid) {
    FbBase = Info.FramebufferBase;
    FbSize = Info.FramebufferSize;
  }

  mMapCount  = 0;
  mHoleCount = 0;

  /* Device space below DRAM. */
  MapAdd ("Peripherals", 0, mDramBase, AddMem, MEM_RES, UNCACHEABLE,
          RtCode, NS_DEVICE);

  /* Firmware-owned ranges. */
  AddHole ("DTB store", KARN_DTB_STORE_BASE, KARN_DTB_STORE_SIZE,
           EfiReservedMemoryType, WRITE_BACK);
  AddHole ("DTB pointer", KARN_DTB_PTR_STORE, KARN_DTB_PTR_STORE_SIZE,
           EfiReservedMemoryType, WRITE_BACK);
  AddHole ("UEFI FD", KARN_UEFI_BASE, KARN_UEFI_SIZE,
           EfiBootServicesCode, WRITE_BACK);
  AddHole ("UEFI Stack", KARN_UEFI_STACK_BASE, KARN_UEFI_STACK_SIZE,
           EfiBootServicesData, WRITE_BACK);
  AddHole ("CPU Vectors", KARN_UEFI_VECTOR_BASE, KARN_UEFI_VECTOR_SIZE,
           EfiBootServicesCode, WRITE_BACK);
  AddHole ("DXE Heap", KARN_UEFI_POOL_BASE, KARN_UEFI_POOL_SIZE,
           EfiConventionalMemory, WRITE_BACK);

  /* MediaTek firmware reservations from the device tree. */
  AddHole ("ATF", KARN_ATF_BASE, KARN_ATF_SIZE,
           EfiReservedMemoryType, WRITE_BACK);
  AddHole ("RAM Console", KARN_RAM_CONSOLE_BASE, KARN_RAM_CONSOLE_SIZE,
           EfiReservedMemoryType, WRITE_BACK);
  AddHole ("MDump", KARN_MDUMP_BASE, KARN_MDUMP_SIZE,
           EfiReservedMemoryType, WRITE_BACK);
  AddHole ("Framebuffer", FbBase, FbSize,
           EfiReservedMemoryType, WRITE_THROUGH_XN);

  SortHoles ();

  Cursor = mDramBase;
  for (I = 0; I < mHoleCount; I++) {
    if (mHoles[I].Base > Cursor) {
      MapAdd ("DRAM", Cursor, mHoles[I].Base - Cursor, AddMem, SYS_MEM,
              SYS_MEM_CAP, Conv, WRITE_BACK_XN);
    }
    MapAdd (mHoles[I].Name, mHoles[I].Base, mHoles[I].Size, AddMem, SYS_MEM,
            SYS_MEM_CAP, mHoles[I].MemoryType, mHoles[I].Attributes);
    if (mHoles[I].Base + mHoles[I].Size > Cursor) {
      Cursor = mHoles[I].Base + mHoles[I].Size;
    }
  }
  if (Cursor < mDramEnd) {
    MapAdd ("DRAM", Cursor, mDramEnd - Cursor, AddMem, SYS_MEM,
            SYS_MEM_CAP, Conv, WRITE_BACK_XN);
  }

  ZeroMem (&gMap[mMapCount], sizeof (gMap[mMapCount]));
  mReady = TRUE;
}

ARM_MEMORY_REGION_DESCRIPTOR_EX *
GetPlatformMemoryMap (
  VOID
  )
{
  BuildMap ();
  return gMap;
}
