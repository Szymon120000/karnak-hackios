/*
 * Host tests for the XNU ARM64 boot_args builder.
 *
 * Compares every field offset in KarnakBootArgs.h against the vendored
 * xnu-8796 upstream header, then exercises the builder.
 *
 *   cc -std=gnu11 -Wall -Wextra -Werror \
 *      -I xnu/tests/hostshim \
 *      -I platform/edk2/XnuLoaderPkg/Include \
 *      -I xnu/include/pexpert \
 *      xnu/tests/boot_args_test.c \
 *      platform/edk2/XnuLoaderPkg/Library/KarnakBootArgsLib/KarnakBootArgsLib.c
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* Minimal kernel types that the vendored pexpert headers assume. */
typedef unsigned long vm_offset_t;
typedef int           boolean_t;

#include "pexpert/arm64/boot.h"

#include <Uefi.h>
#include <Library/KarnakBootArgsLib.h>

static int failures;

#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) {                                                      \
      printf ("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);           \
      failures++;                                                       \
    }                                                                   \
  } while (0)

#define CHECK_OFFSET(field)                                             \
  CHECK (offsetof (KARNAK_BOOT_ARGS, field) == offsetof (boot_args, field))

/* The upstream field names differ in case; map them for the offset checks. */
#define offsetof_up(t, f) offsetof (t, f)

static void
TestLayoutMatchesUpstream (void)
{
  CHECK (sizeof (KARNAK_BOOT_VIDEO) == sizeof (Boot_Video));
  CHECK (sizeof (KARNAK_BOOT_ARGS) == sizeof (boot_args));
  CHECK (KARNAK_BOOT_LINE_LENGTH == BOOT_LINE_LENGTH);
  CHECK (KARNAK_BOOT_ARGS_REVISION == kBootArgsRevision2);
  CHECK (KARNAK_BOOT_ARGS_VERSION == kBootArgsVersion2);

  CHECK_OFFSET (Revision);
  CHECK_OFFSET (Version);
  CHECK (offsetof (KARNAK_BOOT_ARGS, VirtBase) == offsetof (boot_args, virtBase));
  CHECK (offsetof (KARNAK_BOOT_ARGS, PhysBase) == offsetof (boot_args, physBase));
  CHECK (offsetof (KARNAK_BOOT_ARGS, MemSize) == offsetof (boot_args, memSize));
  CHECK (offsetof (KARNAK_BOOT_ARGS, TopOfKernelData) ==
         offsetof (boot_args, topOfKernelData));
  CHECK (offsetof (KARNAK_BOOT_ARGS, Video) == offsetof (boot_args, Video));
  CHECK (offsetof (KARNAK_BOOT_ARGS, MachineType) == offsetof (boot_args, machineType));
  CHECK (offsetof (KARNAK_BOOT_ARGS, DeviceTree) ==
         offsetof (boot_args, deviceTreeP));
  CHECK (offsetof (KARNAK_BOOT_ARGS, DeviceTreeLength) ==
         offsetof (boot_args, deviceTreeLength));
  CHECK_OFFSET (CommandLine);
  CHECK (offsetof (KARNAK_BOOT_ARGS, BootFlags) == offsetof (boot_args, bootFlags));
  CHECK (offsetof (KARNAK_BOOT_ARGS, MemSizeActual) ==
         offsetof (boot_args, memSizeActual));

  CHECK (offsetof (KARNAK_BOOT_VIDEO, BaseAddress) == offsetof (Boot_Video, v_baseAddr));
  CHECK (offsetof (KARNAK_BOOT_VIDEO, RowBytes) == offsetof (Boot_Video, v_rowBytes));
  CHECK (offsetof (KARNAK_BOOT_VIDEO, Width) == offsetof (Boot_Video, v_width));
  CHECK (offsetof (KARNAK_BOOT_VIDEO, Height) == offsetof (Boot_Video, v_height));
  CHECK (offsetof (KARNAK_BOOT_VIDEO, Depth) == offsetof (Boot_Video, v_depth));
}

static int dtb[4] = { 0x00dfeed, 0x100, 0, 0 };

static void
TestInitFillsFields (void)
{
  KARNAK_BOOT_ARGS        args;
  KARNAK_BOOT_ARGS_CONFIG cfg;
  EFI_STATUS              status;

  memset (&args, 0xA5, sizeof (args));
  memset (&cfg, 0, sizeof (cfg));
  cfg.PhysBase         = 0x40000000ULL;
  cfg.MemSize          = 0x20000000ULL;
  cfg.TopOfKernelData  = 0x50000000ULL;
  cfg.DeviceTree       = dtb;
  cfg.DeviceTreeLength = sizeof (dtb);
  cfg.CommandLine      = "-v keepsyms=1 debug=0x8";

  status = KarnakBootArgsInit (&args, &cfg);
  CHECK (status == EFI_SUCCESS);
  CHECK (args.Revision == 2);
  CHECK (args.Version == 2);
  CHECK (args.PhysBase == cfg.PhysBase);
  CHECK (args.MemSize == cfg.MemSize);
  CHECK (args.MemSizeActual == cfg.MemSize);   /* fallback from 0 */
  CHECK (args.TopOfKernelData == cfg.TopOfKernelData);
  CHECK (args.DeviceTree == dtb);
  CHECK (args.DeviceTreeLength == sizeof (dtb));
  CHECK (strcmp (args.CommandLine, cfg.CommandLine) == 0);
  CHECK (args.CommandLine[KARNAK_BOOT_LINE_LENGTH - 1] == '\0');
  CHECK (KarnakBootArgsValidate (&args) == TRUE);
}

static void
TestInitRejectsBadInput (void)
{
  KARNAK_BOOT_ARGS        args;
  KARNAK_BOOT_ARGS_CONFIG cfg;
  char                    too_long[KARNAK_BOOT_LINE_LENGTH + 1];

  memset (&cfg, 0, sizeof (cfg));
  cfg.PhysBase         = 0x40000000ULL;
  cfg.MemSize          = 0x20000000ULL;
  cfg.TopOfKernelData  = 0x50000000ULL;
  cfg.DeviceTree       = dtb;
  cfg.DeviceTreeLength = sizeof (dtb);

  CHECK (KarnakBootArgsInit (NULL, &cfg) == EFI_INVALID_PARAMETER);
  CHECK (KarnakBootArgsInit (&args, NULL) == EFI_INVALID_PARAMETER);

  cfg.DeviceTree = NULL;
  CHECK (KarnakBootArgsInit (&args, &cfg) == EFI_INVALID_PARAMETER);
  cfg.DeviceTree = dtb;

  cfg.TopOfKernelData = 0x30000000ULL;   /* below physBase */
  CHECK (KarnakBootArgsInit (&args, &cfg) == EFI_INVALID_PARAMETER);
  cfg.TopOfKernelData = 0x70000000ULL;   /* above physBase + memSize */
  CHECK (KarnakBootArgsInit (&args, &cfg) == EFI_INVALID_PARAMETER);
  cfg.TopOfKernelData = 0x50000000ULL;

  memset (too_long, 'x', sizeof (too_long));
  too_long[sizeof (too_long) - 1] = '\0';
  cfg.CommandLine = too_long;
  CHECK (KarnakBootArgsInit (&args, &cfg) == EFI_INVALID_PARAMETER);
}

static void
TestValidateRejectsCorruption (void)
{
  KARNAK_BOOT_ARGS        args;
  KARNAK_BOOT_ARGS_CONFIG cfg;

  memset (&cfg, 0, sizeof (cfg));
  cfg.PhysBase         = 0x40000000ULL;
  cfg.MemSize          = 0x20000000ULL;
  cfg.TopOfKernelData  = 0x50000000ULL;
  cfg.DeviceTree       = dtb;
  cfg.DeviceTreeLength = sizeof (dtb);

  CHECK (KarnakBootArgsInit (&args, &cfg) == EFI_SUCCESS);
  CHECK (KarnakBootArgsValidate (NULL) == FALSE);

  args.Version = 1;
  CHECK (KarnakBootArgsValidate (&args) == FALSE);
  args.Version = KARNAK_BOOT_ARGS_VERSION;

  args.Revision = 0;
  CHECK (KarnakBootArgsValidate (&args) == FALSE);
  args.Revision = KARNAK_BOOT_ARGS_REVISION;

  args.DeviceTreeLength = 0;
  CHECK (KarnakBootArgsValidate (&args) == FALSE);
  args.DeviceTreeLength = sizeof (dtb);

  args.MemSize = 0;
  CHECK (KarnakBootArgsValidate (&args) == FALSE);
  args.MemSize = cfg.MemSize;

  args.TopOfKernelData = 0x1000;
  CHECK (KarnakBootArgsValidate (&args) == FALSE);
  args.TopOfKernelData = cfg.TopOfKernelData;

  args.MemSizeActual = args.MemSize + 1;
  CHECK (KarnakBootArgsValidate (&args) == FALSE);
  args.MemSizeActual = args.MemSize;

  args.CommandLine[KARNAK_BOOT_LINE_LENGTH - 1] = 'x';
  CHECK (KarnakBootArgsValidate (&args) == FALSE);

  CHECK (KarnakBootArgsValidate (&args) == FALSE);   /* still corrupt */
}

int
main (void)
{
  TestLayoutMatchesUpstream ();
  TestInitFillsFields ();
  TestInitRejectsBadInput ();
  TestValidateRejectsCorruption ();

  if (failures != 0) {
    printf ("%d boot_args test(s) failed\n", failures);
    return 1;
  }

  printf ("boot_args tests passed (sizeof=%zu)\n", sizeof (KARNAK_BOOT_ARGS));
  return 0;
}
