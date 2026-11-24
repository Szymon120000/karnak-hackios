/** @file
  karnak PrePi hook.  LK already brings up DRAM, the PMIC rails and the
  display; nothing else is required before the DXE core starts.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Library/PlatformPrePiLib.h>

VOID
PlatformInitialize (
  VOID
  )
{
  /* Intentionally empty.  Keep UART/watchdog handling in their own drivers. */
}
