/* Minimal UEFI type shim so XnuImageLib can be compiled and fuzzed on the host. */
#ifndef HOST_UEFI_SHIM_H_
#define HOST_UEFI_SHIM_H_

#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef uint8_t  UINT8;
typedef uint32_t UINT32;
typedef uint64_t UINT64;
typedef size_t   UINTN;
typedef char     CHAR8;
typedef uint8_t  BOOLEAN;
typedef void     VOID;
typedef UINTN    EFI_STATUS;

#define TRUE  1
#define FALSE 0
#define NULL  ((void *)0)
#define MAX_UINT64 UINT64_MAX

#define IN
#define OUT
#define OPTIONAL
#define CONST const
#define EFIAPI
#define STATIC static

#define EFI_SUCCESS            0
#define EFI_INVALID_PARAMETER  2
#define EFI_UNSUPPORTED        3
#define EFI_VOLUME_CORRUPTED   8
#define EFI_ACCESS_DENIED      15

#endif
