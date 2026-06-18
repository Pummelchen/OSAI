#ifndef XAIOS_CPUSET_H
#define XAIOS_CPUSET_H

#include <xaios/types.h>

/*
 * CPU set type — bitmap supporting up to 131,072 CPUs.
 * Used for affinity masks, online CPU tracking, and IPI routing.
 * At max scale: 2,048 words = 16 KB per cpuset.
 */

#define XAIOS_CPUSET_MAX 131072U
#define XAIOS_CPUSET_WORDS (XAIOS_CPUSET_MAX / 64U)

typedef struct xaios_cpuset {
  uint64_t bits[XAIOS_CPUSET_WORDS];
} xaios_cpuset_t;

static inline void xaios_cpuset_zero(xaios_cpuset_t *set) {
  for (uint32_t i = 0; i < XAIOS_CPUSET_WORDS; ++i) {
    set->bits[i] = 0;
  }
}

static inline void xaios_cpuset_set(xaios_cpuset_t *set, uint32_t cpu) {
  if (cpu < XAIOS_CPUSET_MAX) {
    set->bits[cpu / 64U] |= UINT64_C(1) << (cpu % 64U);
  }
}

static inline void xaios_cpuset_clear(xaios_cpuset_t *set, uint32_t cpu) {
  if (cpu < XAIOS_CPUSET_MAX) {
    set->bits[cpu / 64U] &= ~(UINT64_C(1) << (cpu % 64U));
  }
}

static inline int xaios_cpuset_test(const xaios_cpuset_t *set, uint32_t cpu) {
  if (cpu >= XAIOS_CPUSET_MAX) {
    return 0;
  }
  return (set->bits[cpu / 64U] & (UINT64_C(1) << (cpu % 64U))) != 0;
}

static inline uint32_t xaios_cpuset_count(const xaios_cpuset_t *set) {
  uint32_t count = 0;
  for (uint32_t i = 0; i < XAIOS_CPUSET_WORDS; ++i) {
    uint64_t w = set->bits[i];
    while (w != 0) {
      count += (uint32_t)(w & 1U);
      w >>= 1U;
    }
  }
  return count;
}

/* Find the first set bit, returns XAIOS_CPUSET_MAX if none. */
static inline uint32_t xaios_cpuset_first(const xaios_cpuset_t *set) {
  for (uint32_t i = 0; i < XAIOS_CPUSET_WORDS; ++i) {
    uint64_t w = set->bits[i];
    if (w != 0) {
      uint32_t bit = 0;
      while ((w & (UINT64_C(1) << bit)) == 0) {
        ++bit;
      }
      return i * 64U + bit;
    }
  }
  return XAIOS_CPUSET_MAX;
}

static inline void xaios_cpuset_or(xaios_cpuset_t *dst,
                                    const xaios_cpuset_t *src) {
  for (uint32_t i = 0; i < XAIOS_CPUSET_WORDS; ++i) {
    dst->bits[i] |= src->bits[i];
  }
}

static inline void xaios_cpuset_and(xaios_cpuset_t *dst,
                                     const xaios_cpuset_t *src) {
  for (uint32_t i = 0; i < XAIOS_CPUSET_WORDS; ++i) {
    dst->bits[i] &= src->bits[i];
  }
}

#endif
