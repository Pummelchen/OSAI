#ifndef OSAI_ASSERT_H
#define OSAI_ASSERT_H

#include <osai/panic.h>

#define kassert(expr)                                      \
  do {                                                     \
    if (!(expr)) {                                         \
      panic("assertion failed: %s", #expr);                \
    }                                                      \
  } while (0)

#endif
