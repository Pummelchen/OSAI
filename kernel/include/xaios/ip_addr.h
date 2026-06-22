#ifndef XAIOS_IP_ADDR_H
#define XAIOS_IP_ADDR_H

#include <xaios/types.h>

#define XAIOS_IP_FAMILY_V4 UINT8_C(4)
#define XAIOS_IP_FAMILY_V6 UINT8_C(6)

/*
 * Unified IP address type for dual-stack IPv4/IPv6.
 * IPv4 addresses are stored in bytes 0-3 (big-endian).
 * IPv6 addresses occupy all 16 bytes.
 */
typedef struct xaios_ip_addr {
  uint8_t family;    /* XAIOS_IP_FAMILY_V4 (4) or XAIOS_IP_FAMILY_V6 (6) */
  uint8_t addr[16];  /* IPv4 in bytes 0..3 (zero-padded), IPv6 full 16 */
} xaios_ip_addr_t;

static inline xaios_ip_addr_t xaios_ip_addr_from_ipv4(uint32_t ip) {
  xaios_ip_addr_t a;
  a.family = XAIOS_IP_FAMILY_V4;
  a.addr[0] = (uint8_t)(ip >> 24U);
  a.addr[1] = (uint8_t)(ip >> 16U);
  a.addr[2] = (uint8_t)(ip >> 8U);
  a.addr[3] = (uint8_t)(ip);
  for (uint32_t i = 4; i < 16; ++i) {
    a.addr[i] = 0;
  }
  return a;
}

static inline uint32_t xaios_ip_addr_to_ipv4(const xaios_ip_addr_t *a) {
  if (a->family != XAIOS_IP_FAMILY_V4) {
    return 0;
  }
  return ((uint32_t)a->addr[0] << 24U) | ((uint32_t)a->addr[1] << 16U) |
         ((uint32_t)a->addr[2] << 8U) | (uint32_t)a->addr[3];
}

static inline int xaios_ip_addr_equal(const xaios_ip_addr_t *a,
                                      const xaios_ip_addr_t *b) {
  if (a->family != b->family) {
    return 0;
  }
  uint32_t len = (a->family == XAIOS_IP_FAMILY_V4) ? 4U : 16U;
  for (uint32_t i = 0; i < len; ++i) {
    if (a->addr[i] != b->addr[i]) {
      return 0;
    }
  }
  return 1;
}

/* Fold 128-bit (or 32-bit) address into a 32-bit hash for flow distribution */
static inline uint32_t xaios_ip_addr_hash(const xaios_ip_addr_t *a) {
  uint32_t h = 0;
  uint32_t len = (a->family == XAIOS_IP_FAMILY_V4) ? 4U : 16U;
  for (uint32_t i = 0; i < len; i += 4U) {
    uint32_t word = ((uint32_t)a->addr[i] << 24U) |
                    ((uint32_t)a->addr[i + 1U] << 16U) |
                    ((uint32_t)a->addr[i + 2U] << 8U) |
                    (uint32_t)a->addr[i + 3U];
    h ^= word;
  }
  return h;
}

static inline void xaios_ip_addr_zero(xaios_ip_addr_t *a) {
  a->family = 0;
  for (uint32_t i = 0; i < 16; ++i) {
    a->addr[i] = 0;
  }
}

/* Copy 16 bytes from raw network-order address into xaios_ip_addr_t */
static inline void xaios_ip_addr_from_raw_ipv6(xaios_ip_addr_t *a,
                                                const uint8_t raw[16]) {
  a->family = XAIOS_IP_FAMILY_V6;
  for (uint32_t i = 0; i < 16; ++i) {
    a->addr[i] = raw[i];
  }
}

#endif
