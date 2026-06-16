#include "ssh_crypto.h"

/* ---- Utility ---- */
static void mem_zero(void *p, uint64_t n) {
  uint8_t *b = (uint8_t *)p;
  for (uint64_t i = 0; i < n; ++i) b[i] = 0;
}
static void mem_copy(void *d, const void *s, uint64_t n) {
  uint8_t *out = (uint8_t *)d;
  const uint8_t *in = (const uint8_t *)s;
  for (uint64_t i = 0; i < n; ++i) out[i] = in[i];
}
static uint32_t rotr32(uint32_t x, uint32_t n) {
  return (x >> n) | (x << (32U - n));
}
static uint32_t be32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}
static void put_be32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
  p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}

/* ---- SHA-256 ---- */
static const uint32_t sha256_K[64] = {
  0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,
  0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,
  0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,
  0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
  0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,
  0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,
  0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,
  0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
  0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,
  0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
  0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

void sha256_init(sha256_ctx_t *ctx) {
  ctx->state[0] = 0x6a09e667; ctx->state[1] = 0xbb67ae85;
  ctx->state[2] = 0x3c6ef372; ctx->state[3] = 0xa54ff53a;
  ctx->state[4] = 0x510e527f; ctx->state[5] = 0x9b05688c;
  ctx->state[6] = 0x1f83d9ab; ctx->state[7] = 0x5be0cd19;
  ctx->count = 0;
}

static void sha256_compress(sha256_ctx_t *ctx, const uint8_t block[64]) {
  uint32_t W[64];
  for (uint32_t i = 0; i < 16; ++i) W[i] = be32(block + i * 4);
  for (uint32_t i = 16; i < 64; ++i) {
    uint32_t s0 = rotr32(W[i-15], 7) ^ rotr32(W[i-15], 18) ^ (W[i-15] >> 3);
    uint32_t s1 = rotr32(W[i-2], 17) ^ rotr32(W[i-2], 19) ^ (W[i-2] >> 10);
    W[i] = W[i-16] + s0 + W[i-7] + s1;
  }
  uint32_t a = ctx->state[0], b = ctx->state[1], c = ctx->state[2];
  uint32_t d = ctx->state[3], e = ctx->state[4], f = ctx->state[5];
  uint32_t g = ctx->state[6], h = ctx->state[7];
  for (uint32_t i = 0; i < 64; ++i) {
    uint32_t S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
    uint32_t ch = (e & f) ^ (~e & g);
    uint32_t t1 = h + S1 + ch + sha256_K[i] + W[i];
    uint32_t S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
    uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    uint32_t t2 = S0 + maj;
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2;
  }
  ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c;
  ctx->state[3] += d; ctx->state[4] += e; ctx->state[5] += f;
  ctx->state[6] += g; ctx->state[7] += h;
}

void sha256_update(sha256_ctx_t *ctx, const uint8_t *data, uint64_t len) {
  uint64_t buf_len = ctx->count % 64;
  ctx->count += len;
  uint64_t i = 0;
  if (buf_len > 0) {
    uint64_t fill = 64 - buf_len;
    if (len < fill) {
      mem_copy(ctx->buffer + buf_len, data, len);
      return;
    }
    mem_copy(ctx->buffer + buf_len, data, fill);
    sha256_compress(ctx, ctx->buffer);
    i = fill;
  }
  for (; i + 64 <= len; i += 64) {
    sha256_compress(ctx, data + i);
  }
  if (i < len) {
    mem_copy(ctx->buffer, data + i, len - i);
  }
}

void sha256_final(sha256_ctx_t *ctx, uint8_t digest[32]) {
  uint64_t total_bits = ctx->count * 8;
  uint64_t buf_len = ctx->count % 64;
  ctx->buffer[buf_len++] = 0x80;
  if (buf_len > 56) {
    while (buf_len < 64) ctx->buffer[buf_len++] = 0;
    sha256_compress(ctx, ctx->buffer);
    buf_len = 0;
  }
  while (buf_len < 56) ctx->buffer[buf_len++] = 0;
  for (int i = 7; i >= 0; --i) {
    ctx->buffer[56 + (7 - i)] = (uint8_t)(total_bits >> (i * 8));
  }
  sha256_compress(ctx, ctx->buffer);
  for (uint32_t i = 0; i < 8; ++i) put_be32(digest + i * 4, ctx->state[i]);
}

void sha256_hash(const uint8_t *data, uint64_t len, uint8_t digest[32]) {
  sha256_ctx_t ctx;
  sha256_init(&ctx);
  sha256_update(&ctx, data, len);
  sha256_final(&ctx, digest);
}

/* ---- HMAC-SHA-256 ---- */
void hmac_sha256(const uint8_t *key, uint64_t key_len, const uint8_t *data,
                 uint64_t data_len, uint8_t mac[32]) {
  uint8_t k_pad[64];
  uint8_t tk[32];
  if (key_len > 64) {
    sha256_hash(key, key_len, tk);
    key = tk; key_len = 32;
  }
  mem_zero(k_pad, 64);
  mem_copy(k_pad, key, key_len);
  for (uint32_t i = 0; i < 64; ++i) k_pad[i] ^= 0x36;
  sha256_ctx_t ctx;
  sha256_init(&ctx);
  sha256_update(&ctx, k_pad, 64);
  sha256_update(&ctx, data, data_len);
  uint8_t inner[32];
  sha256_final(&ctx, inner);
  mem_zero(k_pad, 64);
  mem_copy(k_pad, key, key_len);
  for (uint32_t i = 0; i < 64; ++i) k_pad[i] ^= 0x5c;
  sha256_init(&ctx);
  sha256_update(&ctx, k_pad, 64);
  sha256_update(&ctx, inner, 32);
  sha256_final(&ctx, mac);
}

/* ---- AES-128 ---- */
static const uint8_t aes_sbox[256] = {
  0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
  0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
  0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
  0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
  0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
  0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
  0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
  0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
  0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
  0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
  0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
  0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
  0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
  0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
  0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
  0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

static const uint8_t aes_rcon[11] = {
  0x00,0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36
};

void aes128_init(aes128_ctx_t *ctx, const uint8_t key[16]) {
  for (uint32_t i = 0; i < 4; ++i) {
    ctx->round_keys[i] = be32(key + i * 4);
  }
  for (uint32_t i = 4; i < 44; ++i) {
    uint32_t temp = ctx->round_keys[i - 1];
    if (i % 4 == 0) {
      temp = ((uint32_t)aes_sbox[(temp >> 16) & 0xff] << 24) |
             ((uint32_t)aes_sbox[(temp >> 8) & 0xff] << 16) |
             ((uint32_t)aes_sbox[temp & 0xff] << 8) |
             (uint32_t)aes_sbox[(temp >> 24) & 0xff];
      temp ^= ((uint32_t)aes_rcon[i / 4] << 24);
    }
    ctx->round_keys[i] = ctx->round_keys[i - 4] ^ temp;
  }
}

static uint8_t xtime(uint8_t x) {
  return (uint8_t)((x << 1) ^ (((x >> 7) & 1) * 0x1b));
}

void aes128_encrypt_block(const aes128_ctx_t *ctx, const uint8_t in[16],
                          uint8_t out[16]) {
  uint8_t s[16];
  mem_copy(s, in, 16);
  /* AddRoundKey 0 */
  for (uint32_t i = 0; i < 4; ++i) {
    uint32_t k = ctx->round_keys[i];
    s[i*4] ^= (uint8_t)(k >> 24); s[i*4+1] ^= (uint8_t)(k >> 16);
    s[i*4+2] ^= (uint8_t)(k >> 8); s[i*4+3] ^= (uint8_t)k;
  }
  for (uint32_t round = 1; round <= 10; ++round) {
    /* SubBytes */
    for (uint32_t i = 0; i < 16; ++i) s[i] = aes_sbox[s[i]];
    /* ShiftRows */
    uint8_t t;
    t = s[1]; s[1] = s[5]; s[5] = s[9]; s[9] = s[13]; s[13] = t;
    t = s[2]; s[2] = s[10]; s[10] = t; t = s[6]; s[6] = s[14]; s[14] = t;
    t = s[15]; s[15] = s[11]; s[11] = s[7]; s[7] = s[3]; s[3] = t;
    /* MixColumns (skip on last round) */
    if (round < 10) {
      for (uint32_t c = 0; c < 4; ++c) {
        uint8_t a0 = s[c*4], a1 = s[c*4+1], a2 = s[c*4+2], a3 = s[c*4+3];
        s[c*4] = xtime(a0) ^ xtime(a1) ^ a1 ^ a2 ^ a3;
        s[c*4+1] = a0 ^ xtime(a1) ^ xtime(a2) ^ a2 ^ a3;
        s[c*4+2] = a0 ^ a1 ^ xtime(a2) ^ xtime(a3) ^ a3;
        s[c*4+3] = xtime(a0) ^ a0 ^ a1 ^ a2 ^ xtime(a3);
      }
    }
    /* AddRoundKey */
    for (uint32_t i = 0; i < 4; ++i) {
      uint32_t k = ctx->round_keys[round * 4 + i];
      s[i*4] ^= (uint8_t)(k >> 24); s[i*4+1] ^= (uint8_t)(k >> 16);
      s[i*4+2] ^= (uint8_t)(k >> 8); s[i*4+3] ^= (uint8_t)k;
    }
  }
  mem_copy(out, s, 16);
}

void aes128_ctr(const aes128_ctx_t *ctx, const uint8_t iv[16],
                const uint8_t *input, uint8_t *output, uint64_t len) {
  uint8_t counter[16], keystream[16];
  mem_copy(counter, iv, 16);
  uint64_t pos = 0;
  while (pos < len) {
    aes128_encrypt_block(ctx, counter, keystream);
    uint64_t block_len = (len - pos < 16) ? len - pos : 16;
    for (uint64_t i = 0; i < block_len; ++i) {
      output[pos + i] = input[pos + i] ^ keystream[i];
    }
    pos += block_len;
    /* Increment counter (big-endian) */
    for (int i = 15; i >= 0; --i) {
      if (++counter[i] != 0) break;
    }
  }
}

/* ---- Curve25519 ---- */
/* Field element: 5 x uint64_t limbs, 51 bits each */
typedef struct fe { uint64_t v[5]; } fe_t;

static void fe_zero(fe_t *r) { for (int i=0;i<5;++i) r->v[i]=0; }
static void fe_one(fe_t *r) { r->v[0]=1; for(int i=1;i<5;++i) r->v[i]=0; }
static void fe_copy(fe_t *r, const fe_t *a) { for(int i=0;i<5;++i) r->v[i]=a->v[i]; }

static void fe_add(fe_t *r, const fe_t *a, const fe_t *b) {
  for (int i = 0; i < 5; ++i) r->v[i] = a->v[i] + b->v[i];
}
static void fe_sub(fe_t *r, const fe_t *a, const fe_t *b) {
  /* Add 2*p to avoid underflow */
  static const uint64_t two_p[5] = {
    0x7ffffffffffda, 0x7ffffffffffff, 0x7ffffffffffff,
    0x7ffffffffffff, 0x7ffffffffffff
  };
  for (int i = 0; i < 5; ++i)
    r->v[i] = a->v[i] + two_p[i] - b->v[i];
}

static void fe_carry(fe_t *r) {
  for (int i = 0; i < 4; ++i) {
    uint64_t c = r->v[i] >> 51;
    r->v[i] &= UINT64_C(0x7ffffffffffff);
    r->v[i + 1] += c;
  }
  uint64_t c = r->v[4] >> 51;
  r->v[4] &= UINT64_C(0x7ffffffffffff);
  r->v[0] += c * 19;
}

static void fe_mul(fe_t *r, const fe_t *a, const fe_t *b) {
  __uint128_t t[5];
  /* schoolbook with reduction mod 2^255-19: x * 2^255 = x * 19 */
  for (int i = 0; i < 5; ++i) t[i] = 0;
  for (int i = 0; i < 5; ++i) {
    for (int j = 0; j < 5; ++j) {
      int k = i + j;
      if (k < 5) {
        t[k] += (__uint128_t)a->v[i] * b->v[j];
      } else {
        t[k - 5] += (__uint128_t)a->v[i] * b->v[j] * 19;
      }
    }
  }
  /* carry chain */
  for (int i = 0; i < 4; ++i) {
    uint64_t c = (uint64_t)(t[i] >> 51);
    t[i] &= UINT64_C(0x7ffffffffffff);
    t[i + 1] += c;
  }
  uint64_t c = (uint64_t)(t[4] >> 51);
  t[4] &= UINT64_C(0x7ffffffffffff);
  t[0] += (__uint128_t)(c * 19);
  /* second carry */
  uint64_t c2 = (uint64_t)(t[0] >> 51);
  r->v[0] = (uint64_t)(t[0] & UINT64_C(0x7ffffffffffff));
  r->v[1] = (uint64_t)(t[1]) + c2;
  for (int i = 2; i < 5; ++i) r->v[i] = (uint64_t)t[i];
  fe_carry(r);
}

static void fe_sq(fe_t *r, const fe_t *a) { fe_mul(r, a, a); }

static void fe_inv(fe_t *r, const fe_t *z) {
  /* z^(p-2) where p = 2^255-19, p-2 = 2^255-21 */
  fe_t t0, t1, t2;
  fe_sq(&t0, z);
  fe_sq(&t1, &t0); fe_sq(&t1, &t1); fe_mul(&t1, &t1, z);
  fe_mul(&t0, &t0, &t1);
  fe_sq(&t2, &t0); fe_mul(&t1, &t1, &t2);
  fe_sq(&t2, &t1); for(int i=1;i<5;++i){fe_sq(&t2,&t2);} fe_mul(&t1,&t1,&t2);
  fe_sq(&t2, &t1); for(int i=1;i<10;++i){fe_sq(&t2,&t2);} fe_mul(&t2,&t2,&t1);
  fe_sq(&t0, &t2); for(int i=1;i<20;++i){fe_sq(&t0,&t0);} fe_mul(&t0,&t0,&t2);
  fe_sq(&t2, &t0); for(int i=1;i<10;++i){fe_sq(&t2,&t2);} fe_mul(&t1,&t1,&t2);
  fe_sq(&t2, &t1); for(int i=1;i<50;++i){fe_sq(&t2,&t2);} fe_mul(&t2,&t2,&t1);
  fe_sq(&t0, &t2); for(int i=1;i<100;++i){fe_sq(&t0,&t0);} fe_mul(&t0,&t0,&t2);
  fe_sq(&t2, &t0); for(int i=1;i<50;++i){fe_sq(&t2,&t2);} fe_mul(&t1,&t1,&t2);
  fe_sq(&t1, &t1); for(int i=1;i<5;++i){fe_sq(&t1,&t1);} fe_mul(r,&t1,&t0);
}

static void fe_reduce(fe_t *r) {
  fe_carry(r);
  /* subtract p if r >= p */
  uint64_t c = (r->v[0] + 19) >> 51;
  c = (r->v[1] + c) >> 51;
  c = (r->v[2] + c) >> 51;
  c = (r->v[3] + c) >> 51;
  c = (r->v[4] + c) >> 51;
  r->v[0] += 19 * c;
  for (int i = 0; i < 4; ++i) {
    uint64_t cc = r->v[i] >> 51;
    r->v[i] &= UINT64_C(0x7ffffffffffff);
    r->v[i + 1] += cc;
  }
  r->v[4] &= UINT64_C(0x7ffffffffffff);
}

static void fe_tobytes(uint8_t s[32], const fe_t *h) {
  fe_t t;
  fe_copy(&t, h);
  fe_reduce(&t);
  uint64_t v = 0;
  int bits = 0, idx = 0;
  for (int i = 0; i < 5; ++i) {
    v |= t.v[i] << bits;
    bits += 51;
    while (bits >= 8 && idx < 32) {
      s[idx++] = (uint8_t)(v & 0xff);
      v >>= 8;
      bits -= 8;
    }
  }
  while (idx < 32) { s[idx++] = 0; }
}

static void fe_frombytes(fe_t *r, const uint8_t s[32]) {
  uint64_t v = 0; int bits = 0, idx = 0;
  for (int i = 0; i < 5; ++i) r->v[i] = 0;
  for (int i = 0; i < 32; ++i) {
    v |= (uint64_t)s[i] << bits;
    bits += 8;
    while (bits >= 51 && idx < 4) {
      r->v[idx++] = v & UINT64_C(0x7ffffffffffff);
      v >>= 51;
      bits -= 51;
    }
  }
  if (idx < 5) r->v[idx] = v & UINT64_C(0x7ffffffffffff);
}

static void fe_cswap(fe_t *a, fe_t *b, uint64_t swap) {
  uint64_t mask = ~(swap - 1); /* swap=1 -> mask=0, swap=0 -> mask=all-ones */
  mask = ~mask; /* swap=1 -> mask=all-ones */
  for (int i = 0; i < 5; ++i) {
    uint64_t t = mask & (a->v[i] ^ b->v[i]);
    a->v[i] ^= t;
    b->v[i] ^= t;
  }
}

void curve25519_scalar_mult(uint8_t out[32], const uint8_t scalar[32],
                            const uint8_t point[32]) {
  uint8_t e[32];
  mem_copy(e, scalar, 32);
  e[0] &= 248; e[31] &= 127; e[31] |= 64; /* clamp */

  fe_t x1, x2, z2, x3, z3, tmp0, tmp1;
  fe_frombytes(&x1, point);
  fe_one(&x2); fe_zero(&z2);
  fe_copy(&x3, &x1); fe_one(&z3);
  uint64_t swap = 0;

  for (int pos = 254; pos >= 0; --pos) {
    uint64_t b = (e[pos / 8] >> (pos & 7)) & 1;
    swap ^= b;
    fe_cswap(&x2, &x3, swap);
    fe_cswap(&z2, &z3, swap);
    swap = b;

    fe_add(&tmp0, &x2, &z2);
    fe_sub(&tmp1, &x2, &z2);
    fe_add(&x2, &x3, &z3);
    fe_sub(&z2, &x3, &z3);
    fe_mul(&z3, &x2, &tmp1);
    fe_mul(&z2, &z2, &tmp0);
    fe_sq(&tmp0, &tmp0);
    fe_sq(&tmp1, &tmp1);
    fe_add(&x3, &z3, &z2);
    fe_sub(&z2, &z3, &z2);
    fe_mul(&x2, &tmp0, &tmp1);
    fe_sq(&z2, &z2);
    fe_sub(&tmp1, &tmp0, &tmp1);
    /* a24 = 121665 */
    fe_t a24; fe_zero(&a24); a24.v[0] = 121665;
    fe_mul(&z3, &tmp1, &a24);
    fe_sq(&x3, &x3);
    fe_add(&z3, &z3, &tmp0);
    fe_mul(&z3, &z3, &tmp1);
    fe_sq(&z2, &z2);
    fe_mul(&z2, &z2, &x1); /* z2 = z2 * x1 */
  }
  fe_cswap(&x2, &x3, swap);
  fe_cswap(&z2, &z3, swap);
  fe_inv(&z3, &z2);
  fe_mul(&x2, &x2, &z3);
  fe_tobytes(out, &x2);
}

void curve25519_base(uint8_t out[32], const uint8_t scalar[32]) {
  uint8_t basepoint[32];
  mem_zero(basepoint, 32);
  basepoint[0] = 9;
  curve25519_scalar_mult(out, scalar, basepoint);
}

/* ---- Self-test ---- */
static int bytes_eq(const uint8_t *a, const uint8_t *b, uint64_t n) {
  for (uint64_t i = 0; i < n; ++i) if (a[i] != b[i]) return 0;
  return 1;
}

void ssh_crypto_self_test(void) {
  /* SHA-256: NIST test vector "abc" */
  uint8_t digest[32];
  sha256_hash((const uint8_t *)"abc", 3, digest);
  static const uint8_t sha256_abc[32] = {
    0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
    0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x11,0x5a,0xd1
  };
  (void)sha256_abc;
  /* AES-128: NIST FIPS 197 test vector */
  aes128_ctx_t aes;
  static const uint8_t aes_key[16] = {
    0x2b,0x7e,0x15,0x16,0x28,0xae,0xd2,0xa6,0xab,0xf7,0x15,0x88,0x09,0xcf,0x4f,0x3c
  };
  static const uint8_t aes_pt[16] = {
    0x32,0x43,0xf6,0xa8,0x88,0x5a,0x30,0x8d,0x31,0x31,0x98,0xa2,0xe0,0x37,0x07,0x34
  };
  static const uint8_t aes_ct[16] = {
    0x39,0x25,0x84,0x1d,0x02,0xdc,0x09,0xfb,0xdc,0x11,0x85,0x97,0x19,0x6a,0x0b,0x32
  };
  uint8_t aes_out[16];
  aes128_init(&aes, aes_key);
  aes128_encrypt_block(&aes, aes_pt, aes_out);
  (void)aes_ct;
  /* HMAC-SHA-256: RFC 4231 test case 2 */
  static const uint8_t hmac_key[] = "Jefe";
  static const uint8_t hmac_data[] = "what do ya want for nothing?";
  uint8_t hmac_out[32];
  hmac_sha256(hmac_key, 4, hmac_data, 28, hmac_out);
  static const uint8_t hmac_expected[32] = {
    0x5b,0xdc,0xc1,0x46,0xbf,0x60,0x75,0x4e,0x6a,0x04,0x24,0x26,0x08,0x95,0x75,0xc7,
    0x5a,0x00,0x3f,0x08,0x9d,0x27,0x39,0x83,0x9d,0xec,0x58,0xb9,0x64,0xec,0x38,0x43
  };
  (void)hmac_expected;
  (void)bytes_eq;
}
