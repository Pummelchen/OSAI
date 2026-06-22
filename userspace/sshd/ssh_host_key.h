#ifndef SSH_HOST_KEY_H
#define SSH_HOST_KEY_H

#include <xaios/types.h>

/* Curve25519 host key pair.
 * Production: generated from CSPRNG on first use.
 * XAIOS_TEST_MODE: uses baked-in test key. */
void ssh_host_key_get_private(uint8_t priv[32]);
void ssh_host_key_get_public(uint8_t pub[32]);

#endif
