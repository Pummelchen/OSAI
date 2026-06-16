#include "ssh_host_key.h"
#include "ssh_crypto.h"

/* Baked-in test keypair (NOT for production use) */
static const uint8_t g_host_private_key[32] = {
  0x77,0x07,0x6d,0x0a,0x73,0x18,0xa5,0x7d,
  0x3c,0x16,0xc1,0x72,0x51,0xb2,0x66,0x45,
  0xdf,0x4c,0x2f,0x87,0xeb,0xc0,0x99,0x2a,
  0xb1,0x77,0xfb,0xa5,0x1d,0xb9,0x2c,0x2a
};

static uint8_t g_host_public_key[32];
static uint32_t g_key_initialized = 0;

static void ensure_key(void) {
  if (!g_key_initialized) {
    curve25519_base(g_host_public_key, g_host_private_key);
    g_key_initialized = 1;
  }
}

void ssh_host_key_get_private(uint8_t priv[32]) {
  for (uint32_t i = 0; i < 32; ++i) priv[i] = g_host_private_key[i];
}

void ssh_host_key_get_public(uint8_t pub[32]) {
  ensure_key();
  for (uint32_t i = 0; i < 32; ++i) pub[i] = g_host_public_key[i];
}
