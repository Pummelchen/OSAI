#ifndef SSH_CONNECTION_H
#define SSH_CONNECTION_H

#include "ssh_crypto.h"
#include "ssh_protocol.h"
#include <xaios_user.h>

#define SSH_MAX_CONNECTIONS 16U

#define SSH_STATE_INIT 0
#define SSH_STATE_KEX 1
#define SSH_STATE_KEX_SENT 2
#define SSH_STATE_NEWKEYS 3
#define SSH_STATE_AUTH 4
#define SSH_STATE_AUTHENTICATED 5
#define SSH_STATE_CHANNEL 6
#define SSH_STATE_CLOSED 7

typedef struct {
  int enabled;
  aes128_ctx_t encrypt_ctx;
  aes128_ctx_t decrypt_ctx;
  uint8_t encrypt_iv[16];
  uint8_t decrypt_iv[16];
  uint8_t encrypt_mac_key[32];
  uint8_t decrypt_mac_key[32];
  uint64_t encrypt_seq;
  uint64_t decrypt_seq;
} ssh_connection_crypto_t;

typedef struct {
  int active;
  uint64_t sockfd;
  ssh_connection_crypto_t crypto;
  uint8_t encrypt_packet[SSH_MAX_PACKET_SIZE];
  uint8_t encrypt_output[SSH_MAX_PACKET_SIZE];
  uint8_t mac_input[8 + SSH_MAX_PACKET_SIZE];
  uint8_t decrypt_rest[SSH_MAX_PACKET_SIZE];
  uint8_t decrypt_mac_input[8 + SSH_MAX_PACKET_SIZE];
  uint8_t decrypt_full_packet[SSH_MAX_PACKET_SIZE];
  ssh_packet_t pkt;
  uint64_t last_activity;
  xaios_ip_addr_user_t client_addr;
  uint16_t client_port;
  uint32_t auth_attempts;
  int state;
  uint8_t session_id[32];
  uint8_t exchange_hash[32];
  uint8_t shared_secret[32];
  uint8_t version_buf[256];
  uint32_t version_len;
  uint8_t client_kexinit[512];
  uint32_t client_kexinit_len;
  uint8_t server_kexinit[512];
  uint32_t server_kexinit_len;
  uint8_t server_ephemeral_priv[32];
  uint8_t server_ephemeral_pub[32];
  uint8_t client_ephemeral_pub[32];
  uint64_t connect_time;
  uint64_t kex_start_time;
} ssh_connection_t;

ssh_connection_t *ssh_conn_alloc(void);
void ssh_conn_free(ssh_connection_t *conn);
ssh_connection_t *ssh_conn_find(uint64_t sockfd);
ssh_connection_t *ssh_conn_by_index(uint32_t idx);
void ssh_conn_pool_init(void);

#endif