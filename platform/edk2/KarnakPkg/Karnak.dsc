## @file
#  Amazon Fire HD 8 (2018) "karnak" - MediaTek MT8163 UEFI platform.
#
#  Build-time truth comes from the Amazon 3.18 device tree (see
#  tools/extract_karnak_hw.py -> include/karnak/hardware_map.h).  Runtime
#  DRAM size and the LK framebuffer are published by KarnakBootShim and
#  consumed by PlatformMemoryMapLib / KarnakDtbLoaderLib.
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
##

[Defines]
  PLATFORM_NAME                  = karnak
  PLATFORM_GUID                  = C1D8A54B-9B02-4E1F-8B7E-6D5C4A3B2F10
  PLATFORM_VERSION               = 0.1
  DSC_SPECIFICATION              = 0x00010019
  OUTPUT_DIRECTORY               = Build/$(PLATFORM_NAME)
  SUPPORTED_ARCHITECTURES        = AARCH64
  BUILD_TARGETS                  = DEBUG|RELEASE
  SKUID_IDENTIFIER               = DEFAULT
  FLASH_DEFINITION               = KarnakPkg/Karnak.fdf
  DEVICE_DXE_FV_COMPONENTS       = KarnakPkg/Karnak.fdf.inc

  # MT8163: quad Cortex-A53, GIC-400 (GICv2), 13 MHz architected timer.
  SOC_PLATFORM                   = mt8163
  USE_PHYSICAL_TIMER             = TRUE
  HAS_BROKEN_TIMER               = FALSE

!include MediaTekPkg/MediaTekCommonDsc.inc

[PcdsFixedAtBuild.common]
  # ---- DRAM ------------------------------------------------------------
  # The build-time DTS declares 512 MiB at 0x40000000.  PcdSystemMemorySize
  # stays at that floor; PlatformMemoryMapLib extends to the live DTB value.
  gArmTokenSpaceGuid.PcdSystemMemoryBase|0x40000000
  gArmTokenSpaceGuid.PcdSystemMemorySize|0x20000000

  # ---- Firmware layout (see include/karnak/hardware_map.h) ------------
  gArmTokenSpaceGuid.PcdCpuVectorBaseAddress|0x50840000
  gEmbeddedTokenSpaceGuid.PcdPrePiStackBase|0x50800000
  gEmbeddedTokenSpaceGuid.PcdPrePiStackSize|0x00040000
  gMediaTekTokenSpaceGuid.PcdUefiMemPoolBase|0x50850000
  gMediaTekTokenSpaceGuid.PcdUefiMemPoolSize|0x02000000

  # ---- GIC-400 (DTS interrupt-controller@10221000) --------------------
  gArmTokenSpaceGuid.PcdGicDistributorBase|0x10221000
  gArmTokenSpaceGuid.PcdGicInterruptInterfaceBase|0x10222000

  # ---- Architected timer: 13 MHz, PPIs 13/14/11/10 -> INTIDs 29/30/27/26
  gArmTokenSpaceGuid.PcdArmArchTimerFreqInHz|13000000
  gArmTokenSpaceGuid.PcdArmArchTimerSecIntrNum|29
  gArmTokenSpaceGuid.PcdArmArchTimerIntrNum|30
  gArmTokenSpaceGuid.PcdArmArchTimerVirtIntrNum|27
  gArmTokenSpaceGuid.PcdArmArchTimerHypIntrNum|26

  gArmPlatformTokenSpaceGuid.PcdCoreCount|4
  gArmPlatformTokenSpaceGuid.PcdClusterCount|1

  # ---- Simple framebuffer (LK 32bpp BGRA) -----------------------------
  # TODO(VERIFY_BOOTARGS): real base/vram come from /chosen:atag,videolfb.
  # KarnakDtbLoaderLib republishes them via PCDs at DXE time; the fixed
  # value below is the mtkfb.c fallback for a 16 MiB top reservation.
  gMediaTekTokenSpaceGuid.PcdMipiFrameBufferAddress|0x5EE00000
  gMediaTekTokenSpaceGuid.PcdMipiFrameBufferWidth|800
  gMediaTekTokenSpaceGuid.PcdMipiFrameBufferHeight|1280
  gMediaTekTokenSpaceGuid.PcdMipiFrameBufferPixelBpp|32
  gMediaTekTokenSpaceGuid.PcdMipiFrameBufferVisibleWidth|800
  gMediaTekTokenSpaceGuid.PcdMipiFrameBufferVisibleHeight|1280
  gMediaTekTokenSpaceGuid.PcdMipiFrameBufferLayers|2
  gMediaTekTokenSpaceGuid.PcdWatchdogBaseAddress|0x10007000
  gMediaTekTokenSpaceGuid.PcdWatchdogWorkingTimer|1

  gEfiMdeModulePkgTokenSpaceGuid.PcdAcpiDefaultOemRevision|0x00000850

[LibraryClasses.common]
  # karnak overrides: generated-from-DTS UART console and FDT access.
  SerialPortLib|KarnakPkg/Library/KarnakSerialPortLib/KarnakSerialPortLib.inf
  KarnakFdtLib|KarnakPkg/Library/KarnakFdtLib/KarnakFdtLib.inf

  PlatformPeiLib|KarnakPkg/Library/PlatformPeiLib/PlatformPeiLib.inf
  PlatformPrePiLib|KarnakPkg/Library/PlatformPrePiLib/PlatformPrePiLib.inf
  PlatformMemoryMapLib|KarnakPkg/Library/PlatformMemoryMapLib/PlatformMemoryMapLib.inf
  XnuImageLib|XnuLoaderPkg/Library/XnuImageLib/XnuImageLib.inf

[LibraryClasses.common.DXE_DRIVER]
  # More specific than the MediaTekCommonDsc.inc mapping, so this overrides it
  # without redefining the common library class.
  DtPlatformDtbLoaderLib|KarnakPkg/Library/KarnakDtbLoaderLib/KarnakDtbLoaderLib.inf

  # MT8163 has an MT6323 PMIC and a different GPIO controller; those drivers
  # are intentionally not placed in the FV.  MediaTekCommonDsc.inc still lists
  # them in [Components.common], so bind the library classes to the generic
  # MediaTek implementations to keep the DSC complete.
  # TODO(VERIFY_DTS): port MtkGpioImplLib/PmicWrapperImplLib to MT8163 + MT6323.
  KeypadDeviceImplLib|MT6789Pkg/Library/KeypadDeviceImplLib/KeypadDeviceImplLib.inf
  MtkGpioImplLib|MT6789Pkg/Library/MtkGpioImplLib/MtkGpioImplLib.inf
  PmicWrapperImplLib|MT6789Pkg/Library/PmicWrapperImplLib/PmicWrapperImplLib.inf

[Components.common]
  # Minimal platform identification driver (prints DRAM + DTB source).
  KarnakPkg/Drivers/KarnakInfoDxe/KarnakInfoDxe.inf

  # Built and linked, but not placed in the FV: it refuses to boot until the
  # boot_args handoff exists.  Keeping it in [Components] makes CI compile it.
  XnuLoaderPkg/Application/XnuLoader/XnuLoader.inf
