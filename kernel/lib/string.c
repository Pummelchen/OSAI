/*
 * Freestanding C runtime string/memory functions for XAIOS kernel.
 *
 * These replace libstd functions (memcpy, memset, strlen, strncmp)
 * which are not available in the freestanding AArch64 build environment.
 * The compiler may implicitly generate calls to memcpy/memset for
 * struct copies and zero-initialization.
 */

#include <xaios/types.h>

void *memcpy(void *dst, const void *src, uint64_t n) {
  uint8_t *d = (uint8_t *)dst;
  const uint8_t *s = (const uint8_t *)src;
  for (uint64_t i = 0; i < n; ++i) {
    d[i] = s[i];
  }
  return dst;
}

void *memset(void *dst, int value, uint64_t n) {
  uint8_t *d = (uint8_t *)dst;
  uint8_t v = (uint8_t)value;
  for (uint64_t i = 0; i < n; ++i) {
    d[i] = v;
  }
  return dst;
}

void *memmove(void *dst, const void *src, uint64_t n) {
  uint8_t *d = (uint8_t *)dst;
  const uint8_t *s = (const uint8_t *)src;
  if (d < s) {
    for (uint64_t i = 0; i < n; ++i) {
      d[i] = s[i];
    }
  } else if (d > s) {
    for (uint64_t i = n; i > 0; --i) {
      d[i - 1] = s[i - 1];
    }
  }
  return dst;
}

uint64_t strlen(const char *s) {
  uint64_t len = 0;
  while (s[len] != '\0') {
    ++len;
  }
  return len;
}

int strncmp(const char *a, const char *b, uint64_t n) {
  for (uint64_t i = 0; i < n; ++i) {
    if (a[i] != b[i]) {
      return (int)(uint8_t)a[i] - (int)(uint8_t)b[i];
    }
    if (a[i] == '\0') {
      return 0;
    }
  }
  return 0;
}
