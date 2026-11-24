/* Host shim for the parts of <Library/BaseLib.h> used by the loader tests. */
#ifndef HOST_BASE_LIB_H_
#define HOST_BASE_LIB_H_

#include <stddef.h>

static inline size_t
AsciiStrLen (
  const char  *String
  )
{
  const char *p = String;
  while (*p != '\0') {
    p++;
  }
  return (size_t)(p - String);
}

#endif
