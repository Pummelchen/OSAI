#ifndef XAIOS_ASSERT_H
#define XAIOS_ASSERT_H

#include <xaios/panic.h>

#define kassert(expr)                                      \
  do {                                                     \
    if (!(expr)) {                                         \
      panic("assertion failed: %s", #expr);                \
    }                                                      \
  } while (0)

#endif
