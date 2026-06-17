#ifndef OSAI_SHA256_H
#define OSAI_SHA256_H

#include <osai/types.h>

typedef struct osai_sha256_ctx {
  uint32_t state[8];
  uint64_t bit_count;
  uint8_t buffer[64];
  uint32_t buffer_len;
} osai_sha256_ctx_t;

void osai_sha256_init(osai_sha256_ctx_t *ctx);
void osai_sha256_update(osai_sha256_ctx_t *ctx, const void *data, uint64_t len);
void osai_sha256_final(osai_sha256_ctx_t *ctx, uint8_t hash[32]);
void osai_sha256(const void *data, uint64_t len, uint8_t hash[32]);
void sha256_self_test(void);

#endif
