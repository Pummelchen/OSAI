#include "sshd.h"
#include "ssh_connection.h"
#include "ssh_crypto.h"
#include "ssh_protocol.h"
#include "ssh_channel.h"
#include "ssh_host_key.h"
#include "ssh_utils.h"
#include <xaios_user.h>
#include <stdarg.h>

static sshd_user_t g_users[SSHD_MAX_USERS];
static uint32_t g_user_count = 0;

static sshd_rate_limit_entry_t g_rate_limits[SSHD_RATE_LIMIT_MAX_ENTRIES];
static uint32_t g_rate_limit_count = 0;

static sshd_stats_t g_server_stats;

static sshd_queue_t g_conn_queue;

static int queue_push(sshd_queue_t *q, u64 conn) {
  uint32_t tail = __atomic_load_n(&q->tail, __ATOMIC_ACQUIRE);
  uint32_t next_tail = (tail + 1) % SSHD_MAX_PENDING_CONNECTIONS;
  uint32_t count = __atomic_load_n(&q->count, __ATOMIC_ACQUIRE);
  if (count >= SSHD_MAX_PENDING_CONNECTIONS) {
    return -1;
  }
  __atomic_store_n(&q->connections[tail], conn, __ATOMIC_RELEASE);
  __atomic_store_n(&q->tail, next_tail, __ATOMIC_RELEASE);
  __atomic_add_fetch(&q->count, 1, __ATOMIC_RELEASE);
  return 0;
}

__attribute__((unused)) static int queue_pop(sshd_queue_t *q, u64 *out_conn) {
  uint32_t count = __atomic_load_n(&q->count, __ATOMIC_ACQUIRE);
  if (count == 0) return -1;
  uint32_t head = __atomic_load_n(&q->head, __ATOMIC_ACQUIRE);
  *out_conn = __atomic_load_n(&q->connections[head], __ATOMIC_ACQUIRE);
  uint32_t next_head = (head + 1) % SSHD_MAX_PENDING_CONNECTIONS;
  __atomic_store_n(&q->head, next_head, __ATOMIC_RELEASE);
  __atomic_sub_fetch(&q->count, 1, __ATOMIC_RELEASE);
  return 0;
}

static int g_log_fd = -1;

static void int_to_str(uint64_t val, char *buf, uint32_t buf_size) {
  if (buf_size == 0) return;
  char temp[32];
  uint32_t pos = 0;
  if (val == 0) { temp[pos++] = '0'; }
  else {
    while (val > 0 && pos < 31) {
      temp[pos++] = '0' + (val % 10);
      val /= 10;
    }
  }
  uint32_t len = 0;
  for (int32_t i = (int32_t)(pos - 1); i >= 0 && len < buf_size - 1; i--) {
    buf[len++] = temp[i];
  }
  buf[len] = '\0';
}

static void hex_to_str(uint64_t val, char *buf, uint32_t buf_size) {
  if (buf_size < 3) return;
  const char *hex_chars = "0123456789abcdef";
  uint32_t len = 0;
  for (int i = 60; i >= 0 && len < buf_size - 2; i -= 4) {
    uint8_t digit = (uint8_t)((val >> i) & 0xF);
    if (digit != 0 || len > 0) {
      buf[len++] = hex_chars[digit];
    }
  }
  if (len == 0) buf[len++] = '0';
  buf[len] = '\0';
}

void ssh_log(int level, const char *fmt, ...) {
  if (g_log_fd < 0) {
    g_log_fd = xaios_fs_open("/var/log/sshd.log", 2);
    if (g_log_fd < 0) return;
  }
  const char *prefix;
  switch (level) {
    case SSH_LOG_INFO:  prefix = "[INFO]";  break;
    case SSH_LOG_WARN:  prefix = "[WARN]";  break;
    case SSH_LOG_ERROR: prefix = "[ERROR]"; break;
    default: prefix = "[UNKNOWN]"; break;
  }
  xaios_fs_write(g_log_fd, (const void*)prefix, ssh_str_len(prefix));
  xaios_fs_write(g_log_fd, " ", 1);
  va_list args;
  va_start(args, fmt);
  char buffer[512];
  uint32_t buf_pos = 0;
  for (const char *p = fmt; *p && buf_pos < 511; p++) {
    if (*p == '%' && *(p+1)) {
      p++;
      if (*p == 's') {
        const char *str = va_arg(args, const char*);
        if (str) {
          uint32_t len = ssh_str_len(str);
          if (buf_pos + len < 511) {
            for (uint32_t i = 0; i < len; i++) buffer[buf_pos++] = str[i];
          }
        }
      } else if (*p == 'u' || *p == 'd') {
        uint64_t val = va_arg(args, uint64_t);
        char num_buf[32];
        int_to_str(val, num_buf, 32);
        uint32_t len = ssh_str_len(num_buf);
        if (buf_pos + len < 511) {
          for (uint32_t i = 0; i < len; i++) buffer[buf_pos++] = num_buf[i];
        }
      } else if (*p == 'x' || *p == 'X') {
        uint64_t val = va_arg(args, uint64_t);
        char num_buf[32];
        hex_to_str(val, num_buf, 32);
        uint32_t len = ssh_str_len(num_buf);
        if (buf_pos + len < 511) {
          for (uint32_t i = 0; i < len; i++) buffer[buf_pos++] = num_buf[i];
        }
      } else if (*p == 'p') {
        uint64_t val = (uint64_t)va_arg(args, void*);
        char num_buf[32];
        hex_to_str(val, num_buf, 32);
        uint32_t len = ssh_str_len(num_buf);
        if (buf_pos + len + 2 < 511) {
          buffer[buf_pos++] = '0';
          buffer[buf_pos++] = 'x';
          for (uint32_t i = 0; i < len; i++) buffer[buf_pos++] = num_buf[i];
        }
      } else if (*p == '%') {
        if (buf_pos < 511) buffer[buf_pos++] = '%';
      }
    } else {
      buffer[buf_pos++] = *p;
    }
  }
  buffer[buf_pos] = '\0';
  va_end(args);
  xaios_fs_write(g_log_fd, buffer, buf_pos);
  xaios_fs_write(g_log_fd, "\n", 1);
}

static int ip_addr_equal(const xaios_ip_addr_user_t *a,
                         const xaios_ip_addr_user_t *b) {
  if (a->family != b->family) return 0;
  uint32_t len = (a->family == 4) ? 4U : 16U;
  for (uint32_t i = 0; i < len; ++i) {
    if (a->addr[i] != b->addr[i]) return 0;
  }
  return 1;
}

/* ---- Per-connection encryption (replaces globals) ---- */

static void conn_init_encryption(ssh_connection_t *conn,
                                  const uint8_t *shared_secret,
                                  uint32_t secret_len,
                                  const uint8_t *exchange_hash,
                                  uint32_t hash_len) {
  ssh_connection_crypto_t *c = &conn->crypto;
  uint8_t derive_buf[128];
  sha256_ctx_t ctx;

  sha256_init(&ctx);
  sha256_update(&ctx, shared_secret, secret_len);
  sha256_update(&ctx, exchange_hash, hash_len);
  sha256_update(&ctx, (const uint8_t*)"A", 1);
  sha256_update(&ctx, exchange_hash, hash_len);
  sha256_final(&ctx, derive_buf);
  ssh_mem_copy(c->decrypt_iv, derive_buf, 16);

  sha256_init(&ctx);
  sha256_update(&ctx, shared_secret, secret_len);
  sha256_update(&ctx, exchange_hash, hash_len);
  sha256_update(&ctx, (const uint8_t*)"B", 1);
  sha256_update(&ctx, exchange_hash, hash_len);
  sha256_final(&ctx, derive_buf);
  ssh_mem_copy(c->encrypt_iv, derive_buf, 16);

  sha256_init(&ctx);
  sha256_update(&ctx, shared_secret, secret_len);
  sha256_update(&ctx, exchange_hash, hash_len);
  sha256_update(&ctx, (const uint8_t*)"C", 1);
  sha256_update(&ctx, exchange_hash, hash_len);
  sha256_final(&ctx, derive_buf);
  aes128_init(&c->decrypt_ctx, derive_buf);

  sha256_init(&ctx);
  sha256_update(&ctx, shared_secret, secret_len);
  sha256_update(&ctx, exchange_hash, hash_len);
  sha256_update(&ctx, (const uint8_t*)"D", 1);
  sha256_update(&ctx, exchange_hash, hash_len);
  sha256_final(&ctx, derive_buf);
  aes128_init(&c->encrypt_ctx, derive_buf);

  sha256_init(&ctx);
  sha256_update(&ctx, shared_secret, secret_len);
  sha256_update(&ctx, exchange_hash, hash_len);
  sha256_update(&ctx, (const uint8_t*)"E", 1);
  sha256_update(&ctx, exchange_hash, hash_len);
  sha256_final(&ctx, derive_buf);
  ssh_mem_copy(c->decrypt_mac_key, derive_buf, 32);

  sha256_init(&ctx);
  sha256_update(&ctx, shared_secret, secret_len);
  sha256_update(&ctx, exchange_hash, hash_len);
  sha256_update(&ctx, (const uint8_t*)"F", 1);
  sha256_update(&ctx, exchange_hash, hash_len);
  sha256_final(&ctx, derive_buf);
  ssh_mem_copy(c->encrypt_mac_key, derive_buf, 32);

  c->encrypt_seq = 0;
  c->decrypt_seq = 0;
  c->enabled = 1;
}

static int conn_packet_write_encrypted(ssh_connection_t *conn,
                                        const uint8_t *data, uint32_t len) {
  ssh_connection_crypto_t *c = &conn->crypto;
  int sockfd = (int)conn->sockfd;

  if (!c->enabled) {
    return ssh_packet_write(sockfd, data, len);
  }

  if (len > SSH_MAX_PACKET_SIZE - 64) return -1;

  uint8_t *packet = conn->encrypt_packet;
  uint32_t padding_len = 8;
  uint32_t payload_len = len;
  uint32_t packet_len = 1 + payload_len + padding_len;

  ssh_write_u32_be(packet, packet_len);
  packet[4] = padding_len;
  ssh_mem_copy(packet + 5, data, payload_len);
  crypto_random_bytes(packet + 5 + payload_len, padding_len);

  uint32_t encrypt_len = 4 + 1 + payload_len + padding_len;
  uint8_t *encrypted = conn->encrypt_output;

  uint8_t iv[16];
  ssh_mem_copy(iv, c->encrypt_iv, 16);
  ((uint64_t*)iv)[0] ^= c->encrypt_seq;

  aes128_ctr(&c->encrypt_ctx, iv, packet, encrypted, encrypt_len);

  uint8_t *mac_buf = conn->mac_input;
  ssh_write_u32_be(mac_buf, (uint32_t)(c->encrypt_seq >> 32));
  ssh_write_u32_be(mac_buf + 4, (uint32_t)(c->encrypt_seq & 0xFFFFFFFF));
  ssh_mem_copy(mac_buf + 8, encrypted, encrypt_len);

  uint8_t mac[32];
  hmac_sha256(c->encrypt_mac_key, 32, mac_buf, 8 + encrypt_len, mac);

  u64 sent = 0;
  if (xaios_net_send(conn->sockfd, encrypted, encrypt_len, &sent) != 0) return -1;
  if (xaios_net_send(conn->sockfd, mac, 32, &sent) != 0) return -1;

  c->encrypt_seq++;
  return 0;
}

static int conn_packet_read_encrypted(ssh_connection_t *conn,
                                       ssh_packet_t *out_pkt) {
  ssh_connection_crypto_t *c = &conn->crypto;
  int sockfd = (int)conn->sockfd;

  if (!c->enabled) {
    return ssh_packet_read(sockfd, out_pkt);
  }

  uint8_t header[16];
  u64 recv_bytes = 0;
  if (xaios_net_recv(conn->sockfd, header, 16, &recv_bytes) != 0) return -1;

  uint8_t decrypted[16];
  uint8_t iv[16];
  ssh_mem_copy(iv, c->decrypt_iv, 16);
  ((uint64_t*)iv)[0] ^= c->decrypt_seq;

  aes128_ctr(&c->decrypt_ctx, iv, header, decrypted, 16);

  uint32_t packet_len = ssh_read_u32_be(decrypted);
  uint8_t padding_len = decrypted[4];

  if (packet_len > SSH_MAX_PACKET_SIZE || padding_len > packet_len) return -1;

  uint32_t remaining = packet_len + 4 - 16;
  uint8_t *rest = conn->decrypt_rest;
  if (remaining > 0 && xaios_net_recv(conn->sockfd, rest, remaining, &recv_bytes) != 0) return -1;

  uint8_t received_mac[32];
  if (xaios_net_recv(conn->sockfd, received_mac, 32, &recv_bytes) != 0) return -1;

  if (remaining > 0) {
    uint8_t iv2[16];
    ssh_mem_copy(iv2, c->decrypt_iv, 16);
    uint32_t ctr = 1;
    for (int i = 15; i >= 12 && ctr > 0; i--) {
      uint32_t sum = iv2[i] + (ctr & 0xFF);
      iv2[i] = (uint8_t)(sum & 0xFF);
      ctr = (ctr >> 8) + (sum >> 8);
    }
    ((uint64_t*)iv2)[0] ^= c->decrypt_seq;
    aes128_ctr(&c->decrypt_ctx, iv2, rest, rest + 16, remaining);
  }

  uint8_t *mac_input = conn->decrypt_mac_input;
  ssh_write_u32_be(mac_input, (uint32_t)(c->decrypt_seq >> 32));
  ssh_write_u32_be(mac_input + 4, (uint32_t)(c->decrypt_seq & 0xFFFFFFFF));
  ssh_mem_copy(mac_input + 8, decrypted, 16);
  if (remaining > 0) {
    ssh_mem_copy(mac_input + 8 + 16, rest, remaining);
  }

  uint8_t expected_mac[32];
  hmac_sha256(c->decrypt_mac_key, 32, mac_input, 8 + 16 + remaining, expected_mac);

  int mac_valid = 1;
  for (int i = 0; i < 32; i++) {
    if (received_mac[i] != expected_mac[i]) mac_valid = 0;
  }
  if (!mac_valid) {
    ssh_log(SSH_LOG_ERROR, "HMAC verification failed\n");
    return -1;
  }

  uint8_t *full_packet = conn->decrypt_full_packet;
  ssh_mem_copy(full_packet, decrypted, 16);
  ssh_mem_copy(full_packet + 16, rest, remaining);

  c->decrypt_seq++;

  out_pkt->len = packet_len + 4;
  ssh_mem_copy(out_pkt->data, full_packet + 4, packet_len);

  return 0;
}

/* ---- Timer ---- */
static uint64_t timer_now(void) {
  volatile uint64_t cycles = 0;
  __asm__ volatile("mrs %0, cntvct_el0" : "=r"(cycles));
  volatile uint64_t freq = 0;
  __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
  if (freq == 0) return 0;
  return cycles / freq;
}

/* ---- User Database ---- */
static int load_user_database(void) {
  if (g_user_count == 0) {
    ssh_mem_copy(g_users[0].username, "admin", 6);
    static const uint8_t admin_hash[32] = {
      0x8c, 0x69, 0x76, 0xe5, 0xb5, 0x41, 0x04, 0x15,
      0xbd, 0xe9, 0x08, 0xbd, 0x4d, 0xee, 0x15, 0xdf,
      0xb1, 0x67, 0xa9, 0xc8, 0x73, 0xfc, 0x4b, 0xb8,
      0xa8, 0x1f, 0x6f, 0x2a, 0xb4, 0x48, 0xa9, 0x18
    };
    ssh_mem_copy(g_users[0].password_hash, admin_hash, 32);
    g_users[0].active = 1;
    g_user_count = 1;
    ssh_log(SSH_LOG_INFO, "Loaded default admin user\n");
  }
  return 0;
}

static int authenticate_password(const char *username, const char *password) {
  for (uint32_t i = 0; i < g_user_count; ++i) {
    if (!g_users[i].active) continue;
    if (!ssh_str_eq(g_users[i].username, username)) continue;
    uint8_t hash[32];
    sha256_hash((const uint8_t *)password, ssh_str_len(password), hash);
    uint8_t diff = 0;
    for (uint32_t j = 0; j < 32; ++j) diff |= hash[j] ^ g_users[i].password_hash[j];
    if (diff != 0) { ssh_mem_zero(hash, 32); return -1; }
    ssh_mem_zero(hash, 32);
    return 0;
  }
  return -1;
}

/* ---- Authorized Keys for Public Key Auth ---- */
#define AUTHORIZED_KEYS_PATH "/etc/xaios_authorized_keys"
#define MAX_AUTHORIZED_KEYS 16

typedef struct {
  uint8_t key[32];
  int active;
} authorized_key_t;

static authorized_key_t g_authorized_keys[MAX_AUTHORIZED_KEYS];
static uint32_t g_authorized_key_count = 0;

static int hex_to_key(const char *hex, uint8_t *key) {
  uint32_t len = 0;
  while (hex[len]) ++len;
  if (len != 64) return -1;
  for (uint32_t i = 0; i < 32; ++i) {
    int hi = 0, lo = 0;
    char c = hex[i * 2];
    if (c >= '0' && c <= '9') hi = c - '0';
    else if (c >= 'a' && c <= 'f') hi = 10 + (c - 'a');
    else if (c >= 'A' && c <= 'F') hi = 10 + (c - 'A');
    else return -1;
    c = hex[i * 2 + 1];
    if (c >= '0' && c <= '9') lo = c - '0';
    else if (c >= 'a' && c <= 'f') lo = 10 + (c - 'a');
    else if (c >= 'A' && c <= 'F') lo = 10 + (c - 'A');
    else return -1;
    key[i] = (uint8_t)((hi << 4) | lo);
  }
  return 0;
}

static int load_authorized_keys(void) {
  if (g_authorized_key_count > 0) return 0;
  char buf[4096];
  int ret = xaios_read_file(AUTHORIZED_KEYS_PATH, buf, sizeof(buf));
  if (ret != 0) {
    ssh_log(SSH_LOG_INFO, "No authorized keys file\n");
    return -1;
  }
  uint32_t line_start = 0;
  uint32_t key_idx = 0;
  for (uint32_t i = 0; buf[i] && key_idx < MAX_AUTHORIZED_KEYS; ++i) {
    if (buf[i] == '\n' || buf[i] == '\0') {
      uint32_t line_len = i - line_start;
      if (line_len == 64) {
        char hex[65];
        ssh_mem_copy(hex, buf + line_start, 64);
        hex[64] = '\0';
        if (hex_to_key(hex, g_authorized_keys[key_idx].key) == 0) {
          g_authorized_keys[key_idx].active = 1;
          key_idx++;
        }
      }
      line_start = i + 1;
    }
  }
  g_authorized_key_count = key_idx;
  ssh_log(SSH_LOG_INFO, "Loaded %u authorized keys\n", g_authorized_key_count);
  return (g_authorized_key_count > 0) ? 0 : -1;
}

static int check_authorized_key(const uint8_t *pubkey) {
  for (uint32_t i = 0; i < g_authorized_key_count; ++i) {
    if (!g_authorized_keys[i].active) continue;
    int match = 1;
    for (uint32_t j = 0; j < 32; ++j) {
      if (g_authorized_keys[i].key[j] != pubkey[j]) { match = 0; break; }
    }
    if (match) return 0;
  }
  return -1;
}

/* ---- Rate Limiting ---- */
static sshd_rate_limit_entry_t *find_rate_limit_entry(
    const xaios_ip_addr_user_t *ip) {
  for (uint32_t i = 0; i < g_rate_limit_count; ++i) {
    if (ip_addr_equal(&g_rate_limits[i].ip_address, ip)) {
      return &g_rate_limits[i];
    }
  }
  return 0;
}

static int check_rate_limit(const xaios_ip_addr_user_t *client_addr) {
  sshd_rate_limit_entry_t *entry = find_rate_limit_entry(client_addr);
  if (entry == 0) return 0;
  uint64_t now = timer_now();
  if (entry->ban_until > now) return -1;
  if (entry->ban_until > 0 && entry->ban_until <= now) {
    entry->failure_count = 0;
    entry->ban_until = 0;
  }
  return 0;
}

static void record_auth_failure(const xaios_ip_addr_user_t *client_addr) {
  sshd_rate_limit_entry_t *entry = find_rate_limit_entry(client_addr);
  uint64_t now = timer_now();
  if (entry == 0) {
    if (g_rate_limit_count < SSHD_RATE_LIMIT_MAX_ENTRIES) {
      entry = &g_rate_limits[g_rate_limit_count++];
      entry->ip_address = *client_addr;
      entry->last_attempt_time = now;
      entry->failure_count = 1;
      entry->ban_until = 0;
    }
  } else {
    entry->last_attempt_time = now;
    entry->failure_count++;
    if (entry->failure_count >= SSHD_RATE_LIMIT_MAX_FAILURES) {
      entry->ban_until = now + SSHD_RATE_LIMIT_BAN_DURATION;
    }
  }
}

static void record_auth_success(const xaios_ip_addr_user_t *client_addr) {
  sshd_rate_limit_entry_t *entry = find_rate_limit_entry(client_addr);
  if (entry) { entry->failure_count = 0; entry->ban_until = 0; }
}

/* ---- Build KEXINIT Packet ---- */
static uint32_t build_kexinit(uint8_t *buf) {
  uint32_t pos = 0;
  buf[pos++] = 20;
  crypto_random_bytes(buf + pos, 16);
  pos += 16;
  const char *kex = "curve25519-sha256";
  uint32_t kex_len = ssh_str_len(kex);
  ssh_write_u32_be(buf + pos, kex_len); pos += 4;
  ssh_mem_copy(buf + pos, kex, kex_len); pos += kex_len;
  const char *hkey = "ssh-ed25519";
  uint32_t hkey_len = ssh_str_len(hkey);
  ssh_write_u32_be(buf + pos, hkey_len); pos += 4;
  ssh_mem_copy(buf + pos, hkey, hkey_len); pos += hkey_len;
  const char *enc = "aes128-ctr";
  uint32_t enc_len = ssh_str_len(enc);
  ssh_write_u32_be(buf + pos, enc_len); pos += 4;
  ssh_mem_copy(buf + pos, enc, enc_len); pos += enc_len;
  ssh_write_u32_be(buf + pos, enc_len); pos += 4;
  ssh_mem_copy(buf + pos, enc, enc_len); pos += enc_len;
  const char *mac = "hmac-sha2-256";
  uint32_t mac_len = ssh_str_len(mac);
  ssh_write_u32_be(buf + pos, mac_len); pos += 4;
  ssh_mem_copy(buf + pos, mac, mac_len); pos += mac_len;
  ssh_write_u32_be(buf + pos, mac_len); pos += 4;
  ssh_mem_copy(buf + pos, mac, mac_len); pos += mac_len;
  const char *comp = "none";
  uint32_t comp_len = ssh_str_len(comp);
  ssh_write_u32_be(buf + pos, comp_len); pos += 4;
  ssh_mem_copy(buf + pos, comp, comp_len); pos += comp_len;
  ssh_write_u32_be(buf + pos, comp_len); pos += 4;
  ssh_mem_copy(buf + pos, comp, comp_len); pos += comp_len;
  ssh_write_u32_be(buf + pos, 0); pos += 4;
  ssh_write_u32_be(buf + pos, 0); pos += 4;
  buf[pos++] = 0;
  ssh_write_u32_be(buf + pos, 0); pos += 4;
  return pos;
}

/* ---- Connection State Machine Processor ---- */

/* Process one step for a connection. Returns 0 if connection should remain,
   -1 if closed/done. */
static int process_connection(ssh_connection_t *conn) {
  int sockfd = (int)conn->sockfd;
  ssh_packet_t *pkt = &conn->pkt;
  uint64_t now = timer_now();

  if (conn->state == SSH_STATE_INIT) {
    /* Send server version */
    if (ssh_send_version(sockfd) != 0) {
      ssh_log(SSH_LOG_ERROR, "Failed to send version\n");
      return -1;
    }
    conn->state = SSH_STATE_KEX;
    conn->kex_start_time = now;
    return 0;
  }

  if (conn->state == SSH_STATE_KEX) {
    /* Receive client version */
    if (conn->version_len == 0) {
      uint32_t pos = 0;
      while (pos < 256) {
        u64 n = 0;
        int r = xaios_net_recv(conn->sockfd, conn->version_buf + pos, 1, &n);
        if (r != 0 || n == 0) {
          /* Not ready yet */
          return 0;
        }
        if (pos > 255) return -1;
        if (conn->version_buf[pos] == '\n') {
          conn->version_len = pos + 1;
          break;
        }
        ++pos;
      }
      if (conn->version_len == 0) return 0;
    }

    /* Send KEXINIT */
    conn->server_kexinit_len = build_kexinit(conn->server_kexinit);
    if (ssh_packet_write(sockfd, conn->server_kexinit, conn->server_kexinit_len) != 0) {
      return -1;
    }
    conn->state = SSH_STATE_KEX_SENT;
    return 0;
  }

  if (conn->state == SSH_STATE_KEX_SENT) {
    /* Receive client KEXINIT */
    if (ssh_packet_read(sockfd, pkt) != 0) return 0;
    if (pkt->len == 0 || pkt->data[0] != 20) return -1;
    conn->client_kexinit_len = pkt->len;
    if (conn->client_kexinit_len > 512) conn->client_kexinit_len = 512;
    ssh_mem_copy(conn->client_kexinit, pkt->data, conn->client_kexinit_len);
    conn->state = SSH_STATE_NEWKEYS;
    return 0;
  }

  if (conn->state == SSH_STATE_NEWKEYS) {
    /* KEXDH_INIT */
    if (ssh_packet_read(sockfd, pkt) != 0) return 0;
    if (pkt->len == 0 || pkt->data[0] != 30) return -1;

    if (pkt->len < 5) return -1;
    uint32_t client_pub_len = ssh_read_string_len(pkt->data + 1);
    if (client_pub_len != 32 || pkt->len < 5 + 32) return -1;
    ssh_mem_copy(conn->client_ephemeral_pub, pkt->data + 5, 32);

    /* Generate server ephemeral key pair */
    crypto_random_bytes(conn->server_ephemeral_priv, 32);
    curve25519_base(conn->server_ephemeral_pub, conn->server_ephemeral_priv);

    /* Compute shared secret */
    curve25519_scalar_mult(conn->shared_secret, conn->server_ephemeral_priv,
                           conn->client_ephemeral_pub);

    /* Build exchange hash */
    sha256_ctx_t hash_ctx;
    sha256_init(&hash_ctx);

    uint32_t vc_len = conn->version_len;
    while (vc_len > 0 && (conn->version_buf[vc_len-1] == '\r' ||
           conn->version_buf[vc_len-1] == '\n')) {
      vc_len--;
    }
    sha256_update(&hash_ctx, conn->version_buf, vc_len);

    const char *server_version = "SSH-2.0-XAIOS_1.0";
    uint32_t vs_len = ssh_str_len(server_version);
    sha256_update(&hash_ctx, (const uint8_t*)server_version, vs_len);

    sha256_update(&hash_ctx, conn->client_kexinit, conn->client_kexinit_len);
    sha256_update(&hash_ctx, conn->server_kexinit, conn->server_kexinit_len);

    /* K_S: host key blob */
    uint8_t host_pub[32];
    ssh_host_key_get_public(host_pub);
    uint8_t host_key_blob[64];
    uint32_t host_key_blob_pos = 0;
    ssh_write_u32_be(host_key_blob + host_key_blob_pos, 11 + 4 + 32);
    host_key_blob_pos += 4;
    ssh_write_u32_be(host_key_blob + host_key_blob_pos, 11);
    host_key_blob_pos += 4;
    ssh_mem_copy(host_key_blob + host_key_blob_pos, "ssh-ed25519", 11);
    host_key_blob_pos += 11;
    ssh_mem_copy(host_key_blob + host_key_blob_pos, host_pub, 32);
    host_key_blob_pos += 32;
    sha256_update(&hash_ctx, host_key_blob, host_key_blob_pos);

    /* e: client ephemeral */
    uint8_t client_pub_blob[36];
    ssh_write_u32_be(client_pub_blob, 32);
    ssh_mem_copy(client_pub_blob + 4, conn->client_ephemeral_pub, 32);
    sha256_update(&hash_ctx, client_pub_blob, 36);

    /* f: server ephemeral */
    uint8_t server_pub_blob[36];
    ssh_write_u32_be(server_pub_blob, 32);
    ssh_mem_copy(server_pub_blob + 4, conn->server_ephemeral_pub, 32);
    sha256_update(&hash_ctx, server_pub_blob, 36);

    /* K: shared secret */
    uint8_t shared_secret_blob[36];
    ssh_write_u32_be(shared_secret_blob, 32);
    ssh_mem_copy(shared_secret_blob + 4, conn->shared_secret, 32);
    sha256_update(&hash_ctx, shared_secret_blob, 36);

    sha256_final(&hash_ctx, conn->exchange_hash);

    /* Build KEXDH_REPLY */
    uint8_t reply[512];
    uint32_t rpos = 0;
    reply[rpos++] = 31;

    /* host key */
    uint32_t host_key_blob_len = 4 + 11 + 4 + 32;
    ssh_write_u32_be(reply + rpos, host_key_blob_len); rpos += 4;
    ssh_write_u32_be(reply + rpos, 11); rpos += 4;
    ssh_mem_copy(reply + rpos, "ssh-ed25519", 11); rpos += 11;
    ssh_write_u32_be(reply + rpos, 32); rpos += 4;
    ssh_mem_copy(reply + rpos, host_pub, 32); rpos += 32;

    /* f (server public) */
    ssh_write_u32_be(reply + rpos, 32); rpos += 4;
    ssh_mem_copy(reply + rpos, conn->server_ephemeral_pub, 32); rpos += 32;

    /* Signature */
    uint8_t signature[64];
    uint8_t host_priv[32];
    ssh_host_key_get_private(host_priv);
    ed25519_sign(signature, conn->exchange_hash, 32, host_pub, host_priv);

    ssh_write_u32_be(reply + rpos, 4 + 11 + 4 + 64); rpos += 4;
    ssh_write_u32_be(reply + rpos, 11); rpos += 4;
    ssh_mem_copy(reply + rpos, "ssh-ed25519", 11); rpos += 11;
    ssh_write_u32_be(reply + rpos, 64); rpos += 4;
    ssh_mem_copy(reply + rpos, signature, 64); rpos += 64;

    if (ssh_packet_write(sockfd, reply, rpos) != 0) return -1;

    /* Send NEWKEYS */
    uint8_t newkeys = 21;
    if (ssh_packet_write(sockfd, &newkeys, 1) != 0) return -1;

    /* Receive NEWKEYS */
    if (ssh_packet_read(sockfd, pkt) != 0) return 0;
    if (pkt->len == 0 || pkt->data[0] != 21) return -1;

    conn_init_encryption(conn, conn->shared_secret, 32,
                         conn->exchange_hash, 32);

    ssh_log(SSH_LOG_INFO, "KEX completed for connection %llx\n", conn->sockfd);
    conn->state = SSH_STATE_AUTH;
    return 0;
  }

  if (conn->state == SSH_STATE_AUTH) {
    if (conn_packet_read_encrypted(conn, pkt) != 0) return 0;
    if (pkt->len == 0) return 0;
    uint8_t msg = pkt->data[0];

    if (msg == SSH_MSG_SERVICE_REQUEST) {
      uint8_t sa[32];
      sa[0] = SSH_MSG_SERVICE_ACCEPT;
      const char *svc = "ssh-userauth";
      uint32_t svc_len = ssh_str_len(svc);
      ssh_write_u32_be(sa + 1, svc_len);
      ssh_mem_copy(sa + 5, svc, svc_len);
      conn_packet_write_encrypted(conn, sa, 5 + svc_len);
      return 0;
    }

    if (msg == SSH_MSG_USERAUTH_REQUEST) {
      if (pkt->len < 10) return 0;

      uint32_t user_len = ssh_read_string_len(pkt->data + 1);
      if (user_len > 64 || (10 + user_len) > pkt->len) return 0;
      char username[65];
      ssh_mem_copy(username, pkt->data + 5, user_len);
      username[user_len] = '\0';

      uint32_t method_len = ssh_read_string_len(pkt->data + 5 + user_len);
      if (method_len > 64) return 0;
      char method[65];
      ssh_mem_copy(method, pkt->data + 9 + user_len, method_len);
      method[method_len] = '\0';

      if (check_rate_limit(&conn->client_addr) != 0) {
        uint8_t reject[64];
        reject[0] = SSH_MSG_USERAUTH_FAILURE;
        const char *methods = "password,publickey";
        uint32_t mlen = ssh_str_len(methods);
        ssh_write_u32_be(reject + 1, mlen);
        ssh_mem_copy(reject + 5, methods, mlen);
        reject[5 + mlen] = 0;
        conn_packet_write_encrypted(conn, reject, 6 + mlen);
        return 0;
      }

      if (conn->auth_attempts >= SSHD_MAX_AUTH_ATTEMPTS) {
        record_auth_failure(&conn->client_addr);
        uint8_t reject[64];
        reject[0] = SSH_MSG_USERAUTH_FAILURE;
        const char *methods = "password,publickey";
        uint32_t mlen = ssh_str_len(methods);
        ssh_write_u32_be(reject + 1, mlen);
        ssh_mem_copy(reject + 5, methods, mlen);
        reject[5 + mlen] = 0;
        conn_packet_write_encrypted(conn, reject, 6 + mlen);
        return 0;
      }

      /* ---- "password" method ---- */
      if (ssh_str_eq(method, "password")) {
        uint32_t password_offset = 9 + user_len + method_len;
        if ((password_offset + 4) > pkt->len) return 0;
        uint32_t pass_len = ssh_read_string_len(pkt->data + password_offset);
        if (pass_len > 128 || (password_offset + 4 + pass_len) > pkt->len) return 0;
        char password[129];
        ssh_mem_copy(password, pkt->data + password_offset + 4, pass_len);
        password[pass_len] = '\0';

        if (authenticate_password(username, password) == 0) {
          uint8_t auth_reply[1] = {SSH_MSG_USERAUTH_SUCCESS};
          conn_packet_write_encrypted(conn, auth_reply, 1);
          conn->auth_attempts = 0;
          record_auth_success(&conn->client_addr);
          ssh_log(SSH_LOG_INFO, "Password auth success: '%s'\n", username);
          conn->state = SSH_STATE_AUTHENTICATED;
        } else {
          conn->auth_attempts++;
          record_auth_failure(&conn->client_addr);
          uint8_t reject[64];
          reject[0] = SSH_MSG_USERAUTH_FAILURE;
          const char *methods = "password,publickey";
          uint32_t mlen = ssh_str_len(methods);
          ssh_write_u32_be(reject + 1, mlen);
          ssh_mem_copy(reject + 5, methods, mlen);
          reject[5 + mlen] = 0;
          conn_packet_write_encrypted(conn, reject, 6 + mlen);
          ssh_log(SSH_LOG_WARN, "Password auth failed: '%s'\n", username);
        }
        return 0;
      }

      /* ---- "publickey" method (RFC 4252 Section 7) ---- */
      if (ssh_str_eq(method, "publickey")) {
        uint32_t offset = 9 + user_len + method_len;
        if (offset + 1 > pkt->len) return 0;
        uint8_t has_signature = pkt->data[offset];
        offset += 1;

        /* Read public key algorithm */
        if (offset + 4 > pkt->len) return 0;
        uint32_t algo_len = ssh_read_string_len(pkt->data + offset);
        offset += 4;
        if (offset + algo_len > pkt->len) return 0;
        offset += algo_len;

        /* Read public key blob */
        if (offset + 4 > pkt->len) return 0;
        uint32_t pubkey_len = ssh_read_string_len(pkt->data + offset);
        offset += 4;
        if (pubkey_len != 32 || offset + 32 > pkt->len) return 0;
        uint8_t client_pubkey[32];
        ssh_mem_copy(client_pubkey, pkt->data + offset, 32);
        offset += 32;

        load_authorized_keys();

        if (check_authorized_key(client_pubkey) != 0) {
          ssh_log(SSH_LOG_WARN, "Public key not authorized\n");
          uint8_t reject[64];
          reject[0] = SSH_MSG_USERAUTH_FAILURE;
          const char *methods = "password,publickey";
          uint32_t mlen = ssh_str_len(methods);
          ssh_write_u32_be(reject + 1, mlen);
          ssh_mem_copy(reject + 5, methods, mlen);
          reject[5 + mlen] = 0;
          conn_packet_write_encrypted(conn, reject, 6 + mlen);
          return 0;
        }

        if (!has_signature) {
          /* Test request: public key is acceptable */
          uint8_t pk_ok[64];
          pk_ok[0] = SSH_MSG_USERAUTH_PK_OK;
          uint32_t poff = 1;
          ssh_write_u32_be(pk_ok + poff, algo_len); poff += 4;
          ssh_mem_copy(pk_ok + poff, pkt->data + offset - 32 - 4, algo_len);
          poff += algo_len;
          ssh_write_u32_be(pk_ok + poff, 32); poff += 4;
          ssh_mem_copy(pk_ok + poff, client_pubkey, 32); poff += 32;
          conn_packet_write_encrypted(conn, pk_ok, poff);
          return 0;
        }

        /* Read signature blob */
        if (offset + 4 > pkt->len) return 0;
        uint32_t sig_len = ssh_read_string_len(pkt->data + offset);
        offset += 4;
        if (offset + sig_len > pkt->len) return 0;
        uint8_t *sig_blob = pkt->data + offset;

        /* Parse signature: string algorithm + string (R,s) */
        uint32_t sig_algo_len = ssh_read_string_len(sig_blob);
        uint32_t sig_data_off = 4 + sig_algo_len;
        if (sig_data_off + 4 > sig_len) return 0;
        uint32_t sig_data_len = ssh_read_string_len(sig_blob + sig_data_off);
        if (sig_data_off + 4 + sig_data_len > sig_len) return 0;
        uint8_t *sig_data = sig_blob + sig_data_off + 4;
        if (sig_data_len != 64) return 0;

        /* Build data to verify: session_id || SSH_MSG_USERAUTH_REQUEST packet */
        uint8_t verify_buf[1024];
        uint32_t vpos = 0;
        ssh_mem_copy(verify_buf + vpos, conn->exchange_hash, 32);
        vpos += 32;
        ssh_mem_copy(verify_buf + vpos, pkt->data, pkt->len);
        vpos += pkt->len;

        int verify_result = ed25519_verify(sig_data, verify_buf, vpos,
                                            client_pubkey);
        if (verify_result == 0) {
          uint8_t auth_reply[1] = {SSH_MSG_USERAUTH_SUCCESS};
          conn_packet_write_encrypted(conn, auth_reply, 1);
          conn->auth_attempts = 0;
          record_auth_success(&conn->client_addr);
          ssh_log(SSH_LOG_INFO, "Public key auth success\n");
          conn->state = SSH_STATE_AUTHENTICATED;
        } else {
          conn->auth_attempts++;
          record_auth_failure(&conn->client_addr);
          uint8_t reject[64];
          reject[0] = SSH_MSG_USERAUTH_FAILURE;
          const char *methods = "password,publickey";
          uint32_t mlen = ssh_str_len(methods);
          ssh_write_u32_be(reject + 1, mlen);
          ssh_mem_copy(reject + 5, methods, mlen);
          reject[5 + mlen] = 0;
          conn_packet_write_encrypted(conn, reject, 6 + mlen);
          ssh_log(SSH_LOG_WARN, "Public key auth failed (verify)\n");
        }
        return 0;
      }

      /* Unknown auth method */
      {
        uint8_t reject[64];
        reject[0] = SSH_MSG_USERAUTH_FAILURE;
        const char *methods = "password,publickey";
        uint32_t mlen = ssh_str_len(methods);
        ssh_write_u32_be(reject + 1, mlen);
        ssh_mem_copy(reject + 5, methods, mlen);
        reject[5 + mlen] = 0;
        conn_packet_write_encrypted(conn, reject, 6 + mlen);
      }
      return 0;
    }

    return 0;
  }

  if (conn->state == SSH_STATE_AUTHENTICATED ||
      conn->state == SSH_STATE_CHANNEL) {
    conn->state = SSH_STATE_CHANNEL;

    /* Check re-keying: 1GB transferred or 1 hour */
    uint64_t total_sent = conn->crypto.encrypt_seq;
    uint64_t elapsed = now - conn->kex_start_time;
    if (total_sent >= 1048576 || elapsed >= 3600) {
      ssh_log(SSH_LOG_INFO, "Initiating re-key\n");
      /* Reset for re-key */
      conn->crypto.enabled = 0;
      conn->state = SSH_STATE_KEX;
      conn->kex_start_time = now;
      return 0;
    }

    /* Check keepalive */
    if (now - conn->last_activity > SSHD_KEEPALIVE_INTERVAL) {
      uint8_t keepalive[32];
      keepalive[0] = SSH_MSG_GLOBAL_REQUEST;
      const char *ka_name = "keepalive@xaios.os";
      uint32_t ka_len = ssh_str_len(ka_name);
      ssh_write_u32_be(keepalive + 1, ka_len);
      ssh_mem_copy(keepalive + 5, ka_name, ka_len);
      keepalive[5 + ka_len] = 1;
      conn_packet_write_encrypted(conn, keepalive, 6 + ka_len);
      if (now - conn->last_activity > SSHD_TIMEOUT_IDLE) {
        ssh_log(SSH_LOG_WARN, "Idle timeout\n");
        return -1;
      }
    }

    /* Read one packet */
    if (conn_packet_read_encrypted(conn, pkt) != 0) return 0;
    if (pkt->len == 0) return 0;

    conn->last_activity = now;
    uint8_t msg = pkt->data[0];

    if (msg == SSH_MSG_GLOBAL_REQUEST) {
      return 0;
    }

    if (msg >= 90 && msg <= 100) {
      if (ssh_channel_handle_packet(sockfd, pkt) != 0) return -1;
      return 0;
    }

    if (msg == SSH_MSG_DISCONNECT) {
      ssh_log(SSH_LOG_INFO, "Client disconnected\n");
      return -1;
    }

    /* Unknown message */
    return 0;
  }

  return 0;
}

/* ---- Cooperative Polling Main Loop ---- */
int sshd_run(void) {
  ssh_crypto_self_test();
  ssh_log(SSH_LOG_INFO, "SSH crypto self-test passed\n");

  load_user_database();
  load_authorized_keys();

  ssh_mem_zero(&g_server_stats, sizeof(g_server_stats));
  ssh_mem_zero(&g_conn_queue, sizeof(g_conn_queue));

  ssh_conn_pool_init();

  u64 listen_fd = 0;
  if (xaios_net_listen(SSHD_PORT, &listen_fd) != 0) {
    ssh_log(SSH_LOG_ERROR, "Failed to listen on port %u\n", SSHD_PORT);
    return -1;
  }
  ssh_log(SSH_LOG_INFO, "SSH server listening on port %u\n", SSHD_PORT);
  ssh_log(SSH_LOG_INFO, "Cooperative polling: max %u concurrent connections\n",
          (uint64_t)SSH_MAX_CONNECTIONS);

  ssh_channel_init();

  for (;;) {
    /* Try to accept new connections (non-blocking) */
    for (uint32_t i = 0; i < 4; ++i) {
      u64 conn_fd = 0;
      xaios_ip_addr_user_t peer_addr;
      u64 peer_port = 0;
      xaios_memzero(&peer_addr, sizeof(peer_addr));
      if (xaios_net_accept_addr(listen_fd, &conn_fd, &peer_addr, &peer_port) != 0) {
        break;
      }

      uint32_t active = __atomic_load_n(&g_server_stats.active_connections,
                                         __ATOMIC_ACQUIRE);
      if (active >= SSH_MAX_CONNECTIONS) {
        ssh_log(SSH_LOG_WARN, "Max connections reached\n");
        __atomic_add_fetch(&g_server_stats.rejected_connections, 1,
                           __ATOMIC_RELEASE);
        xaios_net_close(conn_fd);
        continue;
      }

      ssh_connection_t *conn = ssh_conn_alloc();
      if (!conn) {
        xaios_net_close(conn_fd);
        continue;
      }

      conn->sockfd = conn_fd;
      conn->client_addr = peer_addr;
      conn->client_port = (uint16_t)peer_port;
      conn->state = SSH_STATE_INIT;
      conn->last_activity = timer_now();
      conn->connect_time = conn->last_activity;
      conn->version_len = 0;
      conn->auth_attempts = 0;

      queue_push(&g_conn_queue, conn_fd);
      __atomic_add_fetch(&g_server_stats.total_connections, 1, __ATOMIC_RELEASE);
      __atomic_add_fetch(&g_server_stats.active_connections, 1, __ATOMIC_RELEASE);
      ssh_log(SSH_LOG_INFO, "Accepted connection %llx (total: %u)\n",
              conn_fd, active + 1);
    }

    /* Process each active connection (cooperative time-slicing) */
    for (uint32_t i = 0; i < SSH_MAX_CONNECTIONS; ++i) {
      ssh_connection_t *conn = ssh_conn_by_index(i);
      if (!conn) continue;

      /* Check for timeouts */
      uint64_t now = timer_now();
      if (conn->state == SSH_STATE_INIT || conn->state == SSH_STATE_KEX ||
          conn->state == SSH_STATE_KEX_SENT || conn->state == SSH_STATE_NEWKEYS) {
        if (now - conn->connect_time > SSHD_TIMEOUT_CONNECT) {
          ssh_log(SSH_LOG_WARN, "Connect timeout\n");
          goto close_conn;
        }
      }
      if (conn->state == SSH_STATE_AUTH) {
        if (now - conn->connect_time > SSHD_TIMEOUT_AUTH) {
          ssh_log(SSH_LOG_WARN, "Auth timeout\n");
          goto close_conn;
        }
      }

      int result = process_connection(conn);
      if (result != 0) {
close_conn:
        /* Send disconnect message if encrypted */
        if (conn->state >= SSH_STATE_AUTH) {
          uint8_t disconnect_msg[17];
          ssh_mem_zero(disconnect_msg, sizeof(disconnect_msg));
          disconnect_msg[0] = SSH_MSG_DISCONNECT;
          ssh_write_u32_be(disconnect_msg + 1, SSH_DISCONNECT_BY_APPLICATION);
          ssh_write_u32_be(disconnect_msg + 5, 0);
          ssh_write_u32_be(disconnect_msg + 9, 0);
          conn_packet_write_encrypted(conn, disconnect_msg, 13);
        }

        xaios_net_close(conn->sockfd);
        __atomic_sub_fetch(&g_server_stats.active_connections, 1,
                           __ATOMIC_RELEASE);
        ssh_conn_free(conn);
        ssh_log(SSH_LOG_INFO, "Connection closed\n");
      }
    }
  }

  return 0;
}

int main(void) {
  sshd_run();
  xaios_exit(0);
  return 0;
}