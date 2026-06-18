#ifndef XAIOS_SHA256_H
#define XAIOS_SHA256_H

#include <xaios/types.h>

typedef struct xaios_sha256_ctx {
  uint32_t state[8];
  uint64_t bit_count;
  uint8_t buffer[64];
  uint32_t buffer_len;
} xaios_sha256_ctx_t;

void xaios_sha256_init(xaios_sha256_ctx_t *ctx);
void xaios_sha256_update(xaios_sha256_ctx_t *ctx, const void *data, uint64_t len);
void xaios_sha256_final(xaios_sha256_ctx_t *ctx, uint8_t hash[32]);
void xaios_sha256(const void *data, uint64_t len, uint8_t hash[32]);
void sha256_self_test(void);

#endif
