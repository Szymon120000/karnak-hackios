/* Host shim for <Library/BaseMemoryLib.h>. */
#ifndef HOST_BASE_MEMORY_LIB_H_
#define HOST_BASE_MEMORY_LIB_H_

#include <string.h>

static inline void
ZeroMem (
  void  *Buffer,
  size_t Length
  )
{
  memset (Buffer, 0, Length);
}

#endif
