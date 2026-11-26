/** @file
  Minimal watchdog timer architectural protocol for karnak.

  DxeCore calls CoreAllEfiServicesAvailable() before handing control to BDS
  and asserts if any architectural protocol is missing.  MdeModulePkg's
  WatchdogTimerDxe is deliberately not part of this firmware (its expiry
  handler calls ResetSystem, which turns a slow bring-up boot into a silent
  warm reset), but the protocol itself must exist or a DEBUG build dead-loops
  in the assert before BDS ever runs.

  This stub satisfies the protocol without implementing a timer: LK's MTK
  hardware watchdog is already stopped by BootShim, and nothing here should
  reset the machine.  SetTimerPeriod is accepted and ignored.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Protocol/WatchdogTimer.h>

STATIC UINT64  mTimerPeriod;

STATIC
EFI_STATUS
EFIAPI
KarnakRegisterHandler (
  IN EFI_WATCHDOG_TIMER_ARCH_PROTOCOL  *This,
  IN EFI_WATCHDOG_TIMER_NOTIFY          NotifyFunction
  )
{
  /* No timer is ever armed, so the handler can never fire. */
  (VOID)This;
  (VOID)NotifyFunction;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
KarnakSetTimerPeriod (
  IN EFI_WATCHDOG_TIMER_ARCH_PROTOCOL  *This,
  IN UINT64                             TimerPeriod
  )
{
  (VOID)This;
  mTimerPeriod = TimerPeriod;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
KarnakGetTimerPeriod (
  IN  EFI_WATCHDOG_TIMER_ARCH_PROTOCOL  *This,
  OUT UINT64                            *TimerPeriod
  )
{
  (VOID)This;

  if (TimerPeriod == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  *TimerPeriod = mTimerPeriod;
  return EFI_SUCCESS;
}

STATIC EFI_WATCHDOG_TIMER_ARCH_PROTOCOL  mWatchdogTimer = {
  KarnakRegisterHandler,
  KarnakSetTimerPeriod,
  KarnakGetTimerPeriod
};

EFI_STATUS
EFIAPI
KarnakWatchdogDxeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable
  )
{
  EFI_STATUS  Status;

  (VOID)SystemTable;

  DEBUG ((DEBUG_INFO, "KarnakWatchdog: installing no-op arch protocol\n"));

  Status = gBS->InstallMultipleProtocolInterfaces (
                  &ImageHandle,
                  &gEfiWatchdogTimerArchProtocolGuid,
                  &mWatchdogTimer,
                  NULL
                  );
  ASSERT_EFI_ERROR (Status);

  return Status;
}
