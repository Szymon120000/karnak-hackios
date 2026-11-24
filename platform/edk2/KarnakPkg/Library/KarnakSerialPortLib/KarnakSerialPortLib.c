/** @file
  16550-compatible serial port for the MT8163 UART0 (karnak).

  UART0 is at 0x11002000 (DTS uart0@11002000), LSR at +0x14 and THRE is
  bit 0x20.  LK has already muxed the pins and programmed 921600 8N1 from
  the 26 MHz clock, so initialisation here is a no-op by design; redoing
  the MTK high-speed divisor from firmware would only risk breaking LK's
  setup.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Base.h>
#include <Library/BaseLib.h>
#include <Library/PcdLib.h>
#include <Library/SerialPortLib.h>

#define KARN_UART_THR  0x00
#define KARN_UART_LSR  0x14
#define KARN_UART_LSR_THRE 0x20

STATIC
VOID
KarnakUartPutc (
  IN CHAR8 C
  )
{
  UINT32 Base;
  UINTN  Spin;

  Base = FixedPcdGet32 (PcdKarnakUartBase);
  if (C == '\n') {
    KarnakUartPutc ('\r');
  }

  Spin = 1000000;
  while (((*(volatile UINT32 *)(UINTN)(Base + KARN_UART_LSR)) &
          KARN_UART_LSR_THRE) == 0) {
    if (--Spin == 0) {
      return;
    }
  }
  *(volatile UINT32 *)(UINTN)(Base + KARN_UART_THR) = (UINT8)C;
}

RETURN_STATUS
EFIAPI
SerialPortInitialize (
  VOID
  )
{
  return RETURN_SUCCESS;
}

UINTN
EFIAPI
SerialPortWrite (
  IN UINT8     *Buffer,
  IN UINTN      NumberOfBytes
  )
{
  UINTN Index;

  if (Buffer == NULL) {
    return 0;
  }
  for (Index = 0; Index < NumberOfBytes; Index++) {
    KarnakUartPutc ((CHAR8)Buffer[Index]);
  }
  return NumberOfBytes;
}

UINTN
EFIAPI
SerialPortRead (
  OUT UINT8     *Buffer,
  IN  UINTN      NumberOfBytes
  )
{
  return 0;
}

BOOLEAN
EFIAPI
SerialPortPoll (
  VOID
  )
{
  return FALSE;
}

RETURN_STATUS
EFIAPI
SerialPortSetControl (
  IN UINT32 Control
  )
{
  return RETURN_SUCCESS;
}

RETURN_STATUS
EFIAPI
SerialPortGetControl (
  OUT UINT32 *Control
  )
{
  if (Control != NULL) {
    *Control = 0;
  }
  return RETURN_SUCCESS;
}

RETURN_STATUS
EFIAPI
SerialPortSetAttributes (
  IN OUT UINT64             *BaudRate,
  IN OUT UINT32             *ReceiveFifoDepth,
  IN OUT UINT32             *Timeout,
  IN OUT EFI_PARITY_TYPE    *Parity,
  IN OUT UINT8              *DataBits,
  IN OUT EFI_STOP_BITS_TYPE *StopBits
  )
{
  return RETURN_SUCCESS;
}
