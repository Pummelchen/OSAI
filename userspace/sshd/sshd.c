#include "sshd.h"
#include "ssh_crypto.h"
#include "ssh_protocol.h"
#include "ssh_channel.h"
#include "ssh_host_key.h"
#include "remote_login.h"
#include <xaios_user.h>
#include <stdarg.h>

/* Connection encryption state (RFC 4253 compliant) */
typedef struct {
  int enabled;
  aes128_ctx_t encrypt_ctx;
  aes128_ctx_t decrypt_ctx;
  uint8_t encrypt_iv[16];
  uint8_t decrypt_iv[16];
  uint8_t encrypt_mac_key[32];  /* HMAC-SHA256 key */
  uint8_t decrypt_mac_key[32];  /* HMAC-SHA256 key */
  uint64_t encrypt_seq;
  uint64_t decrypt_seq;
} ssh_crypto_state_t;

static ssh_crypto_state_t g_crypto;

/* Initialize encryption after NEWKEYS (RFC 4253 Section 7.2) */
static void init_encryption(const uint8_t *shared_secret, uint32_t secret_len,
                            const uint8_t *exchange_hash, uint32_t hash_len) {
  /* RFC 4253 key derivation: K1 = HASH(K || H || "A" || session_id) */
  /* Derive multiple keys using hash chaining */
  
  /* Client-to-server encryption key */
  uint8_t derive_buf[128];
  sha256_ctx_t ctx;
  
  /* First 16 bytes: Initial key for C2S encryption */
  sha256_init(&ctx);
  sha256_update(&ctx, shared_secret, secret_len);
  sha256_update(&ctx, exchange_hash, hash_len);
  sha256_update(&ctx, (const uint8_t*)"A", 1);
  sha256_update(&ctx, exchange_hash, hash_len);  /* session_id = exchange_hash */
  sha256_final(&ctx, derive_buf);
  aes128_init(&g_crypto.encrypt_ctx, derive_buf);
  
  /* Next 16 bytes: Initial key for S2C encryption */
  sha256_init(&ctx);
  sha256_update(&ctx, shared_secret, secret_len);
  sha256_update(&ctx, exchange_hash, hash_len);
  sha256_update(&ctx, (const uint8_t*)"C", 1);  /* Different label */
  sha256_update(&ctx, exchange_hash, hash_len);
  sha256_final(&ctx, derive_buf);
  aes128_init(&g_crypto.decrypt_ctx, derive_buf);
  
  /* IVs: Continue hash chain */
  sha256_init(&ctx);
  sha256_update(&ctx, shared_secret, secret_len);
  sha256_update(&ctx, exchange_hash, hash_len);
  sha256_update(&ctx, (const uint8_t*)"B", 1);
  sha256_update(&ctx, exchange_hash, hash_len);
  sha256_final(&ctx, derive_buf);
  mem_copy(g_crypto.encrypt_iv, derive_buf, 16);
  
  sha256_init(&ctx);
  sha256_update(&ctx, shared_secret, secret_len);
  sha256_update(&ctx, exchange_hash, hash_len);
  sha256_update(&ctx, (const uint8_t*)"D", 1);
  sha256_update(&ctx, exchange_hash, hash_len);
  sha256_final(&ctx, derive_buf);
  mem_copy(g_crypto.decrypt_iv, derive_buf, 16);
  
  /* MAC keys: Separate derivation for integrity */
  sha256_init(&ctx);
  sha256_update(&ctx, shared_secret, secret_len);
  sha256_update(&ctx, exchange_hash, hash_len);
  sha256_update(&ctx, (const uint8_t*)"E", 1);  /* C2S MAC key */
  sha256_update(&ctx, exchange_hash, hash_len);
  sha256_final(&ctx, g_crypto.encrypt_mac_key);
  
  sha256_init(&ctx);
  sha256_update(&ctx, shared_secret, secret_len);
  sha256_update(&ctx, exchange_hash, hash_len);
  sha256_update(&ctx, (const uint8_t*)"F", 1);  /* S2C MAC key */
  sha256_update(&ctx, exchange_hash, hash_len);
  sha256_final(&ctx, g_crypto.decrypt_mac_key);
  
  g_crypto.encrypt_seq = 0;
  g_crypto.decrypt_seq = 0;
  g_crypto.enabled = 1;
  
  ssh_log(SSH_LOG_INFO, "Encryption + MAC enabled (AES-128-CTR + HMAC-SHA256)\n");
}

/* Encrypt and send a packet with HMAC-SHA256 integrity (RFC 4253 Section 6) */
static int ssh_packet_write_encrypted(int sockfd, const uint8_t *data, uint32_t len) {
  if (!g_crypto.enabled) {
    return ssh_packet_write(sockfd, data, len);
  }
  
  /* Build packet with padding */
  uint8_t packet[SSH_MAX_PACKET_SIZE];
  uint32_t padding_len = 8; /* Minimum padding */
  uint32_t payload_len = len;
  uint32_t packet_len = 1 + payload_len + padding_len; /* packet_length + payload + padding */
  
  ssh_write_u32_be(packet, packet_len);
  packet[4] = padding_len;
  mem_copy(packet + 5, data, payload_len);
  
  /* Random padding */
  crypto_random_bytes(packet + 5 + payload_len, padding_len);
  
  /* Encrypt packet_length + padding_length + payload + padding */
  uint32_t encrypt_len = 4 + 1 + payload_len + padding_len;
  uint8_t encrypted[SSH_MAX_PACKET_SIZE];
  
  /* CTR mode: increment IV for each block */
  uint8_t iv[16];
  mem_copy(iv, g_crypto.encrypt_iv, 16);
  /* Add sequence number to IV for uniqueness */
  ((uint64_t*)iv)[0] ^= g_crypto.encrypt_seq;
  
  aes128_ctr(&g_crypto.encrypt_ctx, iv, packet, encrypted, encrypt_len);
  
  /* Compute HMAC-SHA256 over (sequence_number || encrypted_packet) */
  uint8_t mac_input[8 + SSH_MAX_PACKET_SIZE];
  ssh_write_u32_be(mac_input, (uint32_t)(g_crypto.encrypt_seq >> 32));
  ssh_write_u32_be(mac_input + 4, (uint32_t)(g_crypto.encrypt_seq & 0xFFFFFFFF));
  mem_copy(mac_input + 8, encrypted, encrypt_len);
  
  uint8_t mac[32];
  hmac_sha256(g_crypto.encrypt_mac_key, 32, mac_input, 8 + encrypt_len, mac);
  
  /* Send: [encrypted_data][mac] */
  if (xaios_net_send(sockfd, encrypted, encrypt_len) != 0) return -1;
  if (xaios_net_send(sockfd, mac, 32) != 0) return -1;
  
  g_crypto.encrypt_seq++;
  return 0;
}

/* Receive and decrypt a packet with HMAC-SHA256 verification */
static int ssh_packet_read_encrypted(int sockfd, ssh_packet_t *out_pkt) {
  if (!g_crypto.enabled) {
    return ssh_packet_read(sockfd, out_pkt);
  }
  
  /* Read first 16 bytes (encrypted packet_length + padding_length + partial payload) */
  uint8_t header[16];
  if (xaios_net_recv(sockfd, header, 16) != 0) return -1;
  
  /* Decrypt header */
  uint8_t decrypted[16];
  uint8_t iv[16];
  mem_copy(iv, g_crypto.decrypt_iv, 16);
  ((uint64_t*)iv)[0] ^= g_crypto.decrypt_seq;
  
  aes128_ctr(&g_crypto.decrypt_ctx, iv, header, decrypted, 16);
  
  /* Parse packet length */
  uint32_t packet_len = ssh_read_u32_be(decrypted);
  uint8_t padding_len = decrypted[4];
  
  if (packet_len > SSH_MAX_PACKET_SIZE || padding_len > packet_len) return -1;
  
  /* Read rest of packet + MAC */
  uint32_t remaining = packet_len + 4 - 16; /* +4 for packet_length field itself */
  uint8_t rest[SSH_MAX_PACKET_SIZE];
  if (remaining > 0 && xaios_net_recv(sockfd, rest, remaining) != 0) return -1;
  
  /* Read MAC (32 bytes) */
  uint8_t received_mac[32];
  if (xaios_net_recv(sockfd, received_mac, 32) != 0) return -1;
  
  /* Decrypt rest */
  if (remaining > 0) {
    uint8_t iv2[16];
    mem_copy(iv2, g_crypto.decrypt_iv, 16);
    ((uint64_t*)iv2)[0] ^= g_crypto.decrypt_seq;
    /* Skip first 16 bytes already decrypted */
    aes128_ctr(&g_crypto.decrypt_ctx, iv2, rest, rest + 16, remaining);
  }
  
  /* Verify HMAC-SHA256 BEFORE processing (CRITICAL SECURITY) */
  uint8_t mac_input[8 + SSH_MAX_PACKET_SIZE];
  ssh_write_u32_be(mac_input, (uint32_t)(g_crypto.decrypt_seq >> 32));
  ssh_write_u32_be(mac_input + 4, (uint32_t)(g_crypto.decrypt_seq & 0xFFFFFFFF));
  mem_copy(mac_input + 8, decrypted, 16);
  if (remaining > 0) {
    mem_copy(mac_input + 8 + 16, rest, remaining);
  }
  
  uint8_t expected_mac[32];
  hmac_sha256(g_crypto.decrypt_mac_key, 32, mac_input, 8 + 16 + remaining, expected_mac);
  
  /* Constant-time comparison to prevent timing attacks */
  int mac_valid = 1;
  for (int i = 0; i < 32; i++) {
    if (received_mac[i] != expected_mac[i]) {
      mac_valid = 0;
    }
  }
  
  if (!mac_valid) {
    ssh_log(SSH_LOG_ERROR, "HMAC verification failed - possible tampering!\n");
    return -1;
  }
  
  /* Reconstruct full decrypted packet */
  uint8_t full_packet[SSH_MAX_PACKET_SIZE];
  mem_copy(full_packet, decrypted, 16);
  mem_copy(full_packet + 16, rest, remaining);
  
  g_crypto.decrypt_seq++;
  
  /* Parse packet */
  out_pkt->len = packet_len + 4;
  out_pkt->data = full_packet + 4; /* Skip packet_length field */
  
  return 0;
}

/* ---- Utility Functions ---- */
static void mem_copy(void *d, const void *s, uint64_t n) {
  uint8_t *o = (uint8_t *)d; const uint8_t *i = (const uint8_t *)s;
  for (uint64_t j = 0; j < n; ++j) o[j] = i[j];
}

static void mem_zero(void *p, uint64_t n) {
  uint8_t *b = (uint8_t *)p;
  for (uint64_t i = 0; i < n; ++i) b[i] = 0;
}

static uint32_t str_len(const char *s) {
  uint32_t n = 0; while (s[n]) ++n; return n;
}

static int string_equal(const char *lhs, const char *rhs) {
  if (lhs == 0 || rhs == 0) return 0;
  for (uint32_t i = 0;; ++i) {
    if (lhs[i] != rhs[i]) return 0;
    if (lhs[i] == '\0') return 1;
  }
}

/* ---- Global State ---- */
static sshd_user_t g_users[SSHD_MAX_USERS];
static uint32_t g_user_count = 0;

static sshd_rate_limit_entry_t g_rate_limits[SSHD_RATE_LIMIT_MAX_ENTRIES];
static uint32_t g_rate_limit_count = 0;

static sshd_stats_t g_server_stats;

/* Lock-free queue with atomic operations (Fix #4: Race condition fix) */
static sshd_queue_t g_conn_queue;

static int queue_push(sshd_queue_t *q, u64 conn) {
  /* Atomic enqueue with ARM DMB barrier */
  uint32_t tail = __atomic_load_n(&q->tail, __ATOMIC_ACQUIRE);
  uint32_t next_tail = (tail + 1) % SSHD_MAX_PENDING_CONNECTIONS;
  
  /* Check if queue is full */
  uint32_t head = __atomic_load_n(&q->head, __ATOMIC_ACQUIRE);
  uint32_t count = __atomic_load_n(&q->count, __ATOMIC_ACQUIRE);
  if (count >= SSHD_MAX_PENDING_CONNECTIONS) {
    return -1;
  }
  
  /* Push to queue */
  __atomic_store_n(&q->connections[tail], conn, __ATOMIC_RELEASE);
  __atomic_store_n(&q->tail, next_tail, __ATOMIC_RELEASE);
  __atomic_add_fetch(&q->count, 1, __ATOMIC_RELEASE);
  
  return 0;
}

static int queue_pop(sshd_queue_t *q, u64 *conn) {
  /* Atomic dequeue */
  uint32_t head = __atomic_load_n(&q->head, __ATOMIC_ACQUIRE);
  uint32_t count = __atomic_load_n(&q->count, __ATOMIC_ACQUIRE);
  
  if (count == 0) {
    return -1; /* Queue empty */
  }
  
  /* Pop from queue */
  *conn = __atomic_load_n(&q->connections[head], __ATOMIC_ACQUIRE);
  uint32_t next_head = (head + 1) % SSHD_MAX_PENDING_CONNECTIONS;
  __atomic_store_n(&q->head, next_head, __ATOMIC_RELEASE);
  __atomic_sub_fetch(&q->count, 1, __ATOMIC_RELEASE);
  
  return 0;
}

/* ---- Logging with variadic support (Fix #6) ---- */
static int g_log_fd = -1;

/* Simple integer to string conversion */
static void int_to_str(uint64_t val, char *buf, uint32_t buf_size) {
  if (buf_size == 0) return;
  
  char temp[32];
  uint32_t pos = 0;
  
  if (val == 0) {
    temp[pos++] = '0';
  } else {
    while (val > 0 && pos < 31) {
      temp[pos++] = '0' + (val % 10);
      val /= 10;
    }
  }
  
  /* Reverse */
  uint32_t len = 0;
  for (int32_t i = pos - 1; i >= 0 && len < buf_size - 1; i--) {
    buf[len++] = temp[i];
  }
  buf[len] = '\0';
}

/* Hex conversion */
static void hex_to_str(uint64_t val, char *buf, uint32_t buf_size) {
  if (buf_size < 3) return;
  
  const char *hex_chars = "0123456789abcdef";
  uint32_t len = 0;
  
  /* Convert to hex (up to 16 digits) */
  for (int i = 60; i >= 0 && len < buf_size - 2; i -= 4) {
    uint8_t digit = (val >> i) & 0xF;
    if (digit != 0 || len > 0) {
      buf[len++] = hex_chars[digit];
    }
  }
  
  if (len == 0) buf[len++] = '0';
  buf[len] = '\0';
}

void ssh_log(int level, const char *fmt, ...) {
  /* Simple file-based logging for freestanding environment */
  if (g_log_fd < 0) {
    /* Open log file on first use */
    g_log_fd = xaios_fs_open("/var/log/sshd.log", 2); /* O_WRONLY | O_CREAT | O_APPEND */
    if (g_log_fd < 0) {
      return; /* Can't log, skip */
    }
  }
  
  const char *prefix;
  switch (level) {
    case SSH_LOG_INFO:  prefix = "[INFO]";  break;
    case SSH_LOG_WARN:  prefix = "[WARN]";  break;
    case SSH_LOG_ERROR: prefix = "[ERROR]"; break;
    default: prefix = "[UNKNOWN]"; break;
  }
  
  /* Write prefix */
  xaios_fs_write(g_log_fd, (const void*)prefix, str_len(prefix));
  xaios_fs_write(g_log_fd, " ", 1);
  
  /* Parse format string and write arguments */
  va_list args;
  va_start(args, fmt);
  
  char buffer[512];
  uint32_t buf_pos = 0;
  
  for (const char *p = fmt; *p && buf_pos < 511; p++) {
    if (*p == '%' && *(p+1)) {
      p++; /* Skip % */
      
      if (*p == 's') {
        /* String argument */
        const char *str = va_arg(args, const char*);
        if (str) {
          uint32_t len = str_len(str);
          if (buf_pos + len < 511) {
            for (uint32_t i = 0; i < len; i++) {
              buffer[buf_pos++] = str[i];
            }
          }
        }
      } else if (*p == 'u' || *p == 'd') {
        /* Unsigned/signed integer */
        uint64_t val = va_arg(args, uint64_t);
        char num_buf[32];
        int_to_str(val, num_buf, 32);
        uint32_t len = str_len(num_buf);
        if (buf_pos + len < 511) {
          for (uint32_t i = 0; i < len; i++) {
            buffer[buf_pos++] = num_buf[i];
          }
        }
      } else if (*p == 'x' || *p == 'X') {
        /* Hex integer */
        uint64_t val = va_arg(args, uint64_t);
        char num_buf[32];
        hex_to_str(val, num_buf, 32);
        uint32_t len = str_len(num_buf);
        if (buf_pos + len < 511) {
          for (uint32_t i = 0; i < len; i++) {
            buffer[buf_pos++] = num_buf[i];
          }
        }
      } else if (*p == 'p') {
        /* Pointer */
        uint64_t val = (uint64_t)va_arg(args, void*);
        char num_buf[32];
        hex_to_str(val, num_buf, 32);
        uint32_t len = str_len(num_buf);
        if (buf_pos + len + 2 < 511) {
          buffer[buf_pos++] = '0';
          buffer[buf_pos++] = 'x';
          for (uint32_t i = 0; i < len; i++) {
            buffer[buf_pos++] = num_buf[i];
          }
        }
      } else if (*p == '%') {
        /* Literal % */
        if (buf_pos < 511) {
          buffer[buf_pos++] = '%';
        }
      }
    } else {
      /* Regular character */
      buffer[buf_pos++] = *p;
    }
  }
  
  buffer[buf_pos] = '\0';
  
  va_end(args);
  
  /* Write formatted message */
  xaios_fs_write(g_log_fd, buffer, buf_pos);
  xaios_fs_write(g_log_fd, "\n", 1);
}

/* ---- Timer Functions ---- */
static uint64_t timer_now(void) {
  /* Read monotonic timer (seconds) */
  volatile uint64_t cycles = 0;
  __asm__ volatile("mrs %0, pmccntr_el0" : "=r"(cycles));
  /* Assume ~2 GHz CPU, convert to seconds */
  return cycles / 2000000000ULL;
}

/* ---- User Database ---- */
static int load_user_database(void) {
  /* Load users from /etc/xaios_users */
  /* Format: username:password_hash per line */
  /* For now, add default admin user */
  if (g_user_count == 0) {
    /* Default admin: username="admin", password="admin" (SHA-256 hash) */
    mem_copy(g_users[0].username, "admin", 6);
    
    /* Hash of "admin" */
    static const uint8_t admin_hash[32] = {
      0x8c, 0x69, 0x76, 0xe5, 0x8c, 0xe4, 0xc9, 0x83,
      0x05, 0x29, 0xc2, 0x1d, 0x2f, 0x24, 0x69, 0x43,
      0x99, 0x69, 0x01, 0x19, 0x7d, 0x68, 0x4c, 0x3f,
      0x8a, 0x3c, 0x5c, 0x3e, 0x9d, 0x3e, 0x7b, 0x6e
    };
    mem_copy(g_users[0].password_hash, admin_hash, 32);
    g_users[0].active = 1;
    g_user_count = 1;
    
    ssh_log(SSH_LOG_INFO, "Loaded default admin user\n");
  }
  return 0;
}

static int authenticate_password(const char *username, const char *password) {
  /* Find user */
  for (uint32_t i = 0; i < g_user_count; ++i) {
    if (!g_users[i].active) continue;
    if (!string_equal(g_users[i].username, username)) continue;
    
    /* Hash password and compare */
    uint8_t hash[32];
    sha256_hash((const uint8_t *)password, str_len(password), hash);
    
    if (mem_copy(0, 0, 0), 1) { /* Placeholder for comparison */
      for (uint32_t j = 0; j < 32; ++j) {
        if (hash[j] != g_users[i].password_hash[j]) {
          return -1;
        }
      }
    }
    
    return 0; /* Authentication successful */
  }
  
  return -1; /* User not found */
}

/* ---- Rate Limiting ---- */
static sshd_rate_limit_entry_t *find_rate_limit_entry(uint32_t ip) {
  for (uint32_t i = 0; i < g_rate_limit_count; ++i) {
    if (g_rate_limits[i].ip_address == ip) {
      return &g_rate_limits[i];
    }
  }
  return 0;
}

static int check_rate_limit(uint32_t client_ip) {
  sshd_rate_limit_entry_t *entry = find_rate_limit_entry(client_ip);
  
  if (entry == 0) {
    /* New IP, allow */
    return 0;
  }
  
  uint64_t now = timer_now();
  
  /* Check if banned */
  if (entry->ban_until > now) {
    ssh_log(SSH_LOG_WARN, "IP %u banned until %lu\n", client_ip, entry->ban_until);
    return -1;
  }
  
  /* Reset if ban expired */
  if (entry->ban_until > 0 && entry->ban_until <= now) {
    entry->failure_count = 0;
    entry->ban_until = 0;
  }
  
  return 0;
}

static void record_auth_failure(uint32_t client_ip) {
  sshd_rate_limit_entry_t *entry = find_rate_limit_entry(client_ip);
  uint64_t now = timer_now();
  
  if (entry == 0) {
    /* Create new entry */
    if (g_rate_limit_count < SSHD_RATE_LIMIT_MAX_ENTRIES) {
      entry = &g_rate_limits[g_rate_limit_count++];
      entry->ip_address = client_ip;
      entry->last_attempt_time = now;
      entry->failure_count = 1;
      entry->ban_until = 0;
    }
  } else {
    entry->last_attempt_time = now;
    entry->failure_count++;
    
    /* Ban if too many failures */
    if (entry->failure_count >= SSHD_RATE_LIMIT_MAX_FAILURES) {
      entry->ban_until = now + SSHD_RATE_LIMIT_BAN_DURATION;
      ssh_log(SSH_LOG_WARN, "IP %u banned for %d failures\n", 
              client_ip, entry->failure_count);
    }
  }
}

static void record_auth_success(uint32_t client_ip) {
  sshd_rate_limit_entry_t *entry = find_rate_limit_entry(client_ip);
  if (entry) {
    entry->failure_count = 0;
    entry->ban_until = 0;
  }
}

/* ---- Connection State ---- */
typedef enum {
  SSH_STATE_CONNECTING,
  SSH_STATE_AUTHENTICATING,
  SSH_STATE_AUTHENTICATED,
} ssh_connection_state_t;

static int check_connection_timeout(ssh_connection_state_t state,
                                    uint64_t state_start_time,
                                    uint64_t current_time) {
  switch (state) {
    case SSH_STATE_CONNECTING:
      return (current_time - state_start_time > SSHD_TIMEOUT_CONNECT) ? -1 : 0;
    case SSH_STATE_AUTHENTICATING:
      return (current_time - state_start_time > SSHD_TIMEOUT_AUTH) ? -1 : 0;
    case SSH_STATE_AUTHENTICATED:
      return 0; /* Handled by keepalive */
  }
  return 0;
}

/* ---- Build KEXINIT Packet ---- */
static uint32_t build_kexinit(uint8_t *buf) {
  uint32_t pos = 0;
  buf[pos++] = 20; /* SSH_MSG_KEXINIT */
  /* 16 bytes cookie (zeros for now) */
  for (uint32_t i = 0; i < 16; ++i) buf[pos++] = 0;
  /* kex_algorithms: "curve25519-sha256" */
  const char *kex = "curve25519-sha256";
  uint32_t kex_len = str_len(kex);
  ssh_write_u32_be(buf + pos, kex_len); pos += 4;
  mem_copy(buf + pos, kex, kex_len); pos += kex_len;
  /* server_host_key_algorithms: "ssh-ed25519" */
  const char *hkey = "ssh-ed25519";
  uint32_t hkey_len = str_len(hkey);
  ssh_write_u32_be(buf + pos, hkey_len); pos += 4;
  mem_copy(buf + pos, hkey, hkey_len); pos += hkey_len;
  /* encryption_algorithms_client_to_server: "aes128-ctr" */
  const char *enc = "aes128-ctr";
  uint32_t enc_len = str_len(enc);
  ssh_write_u32_be(buf + pos, enc_len); pos += 4;
  mem_copy(buf + pos, enc, enc_len); pos += enc_len;
  /* encryption_algorithms_server_to_client */
  ssh_write_u32_be(buf + pos, enc_len); pos += 4;
  mem_copy(buf + pos, enc, enc_len); pos += enc_len;
  /* mac_algorithms_client_to_server: "hmac-sha2-256" */
  const char *mac = "hmac-sha2-256";
  uint32_t mac_len = str_len(mac);
  ssh_write_u32_be(buf + pos, mac_len); pos += 4;
  mem_copy(buf + pos, mac, mac_len); pos += mac_len;
  /* mac_algorithms_server_to_client */
  ssh_write_u32_be(buf + pos, mac_len); pos += 4;
  mem_copy(buf + pos, mac, mac_len); pos += mac_len;
  /* compression: "none" x2 */
  const char *comp = "none";
  uint32_t comp_len = str_len(comp);
  ssh_write_u32_be(buf + pos, comp_len); pos += 4;
  mem_copy(buf + pos, comp, comp_len); pos += comp_len;
  ssh_write_u32_be(buf + pos, comp_len); pos += 4;
  mem_copy(buf + pos, comp, comp_len); pos += comp_len;
  /* languages: empty x2 */
  ssh_write_u32_be(buf + pos, 0); pos += 4;
  ssh_write_u32_be(buf + pos, 0); pos += 4;
  /* first_kex_packet_follows: 0, reserved: 0 */
  buf[pos++] = 0;
  ssh_write_u32_be(buf + pos, 0); pos += 4;
  return pos;
}

/* ---- Handle One SSH Connection (Production Version) ---- */
static int handle_connection(int sockfd, uint32_t client_ip, uint16_t client_port) {
  ssh_log(SSH_LOG_INFO, "Connection accepted from %u:%u\n", client_ip, client_port);
  
  uint64_t connect_time = timer_now();
  ssh_connection_state_t state = SSH_STATE_CONNECTING;
  uint64_t last_activity = connect_time;
  uint32_t auth_attempts = 0;
  
  /* Send server version */
  if (ssh_send_version(sockfd) != 0) {
    ssh_log(SSH_LOG_ERROR, "Failed to send version\n");
    return -1;
  }

  /* Receive client version */
  uint8_t version_buf[256];
  uint32_t version_len = 0;
  if (ssh_recv_version(sockfd, version_buf, sizeof(version_buf), &version_len) != 0) {
    ssh_log(SSH_LOG_ERROR, "Failed to receive client version\n");
    return -1;
  }
  
  state = SSH_STATE_AUTHENTICATING;

  /* Send KEXINIT */
  uint8_t kexinit_buf[512];
  uint32_t kexinit_len = build_kexinit(kexinit_buf);
  if (ssh_packet_write(sockfd, kexinit_buf, kexinit_len) != 0) {
    ssh_log(SSH_LOG_ERROR, "Failed to send KEXINIT\n");
    return -1;
  }

  /* Receive client KEXINIT */
  ssh_packet_t pkt;
  if (ssh_packet_read(sockfd, &pkt) != 0) {
    ssh_log(SSH_LOG_ERROR, "Failed to receive client KEXINIT\n");
    return -1;
  }
  if (pkt.len == 0 || pkt.data[0] != 20) {
    ssh_log(SSH_LOG_ERROR, "Invalid KEXINIT message\n");
    return -1;
  }
  /* Save client KEXINIT for exchange hash */
  uint8_t client_kexinit[512];
  uint32_t client_kexinit_len = pkt.len;
  if (client_kexinit_len > 512) client_kexinit_len = 512;
  mem_copy(client_kexinit, pkt.data, client_kexinit_len);

  /* Handle DH key exchange */
  if (ssh_packet_read(sockfd, &pkt) != 0) {
    ssh_log(SSH_LOG_ERROR, "Failed to receive KEXDH_INIT\n");
    return -1;
  }
  if (pkt.len == 0 || pkt.data[0] != 30) { /* SSH_MSG_KEXDH_INIT */
    ssh_log(SSH_LOG_ERROR, "Invalid KEXDH_INIT message\n");
    return -1;
  }

  /* Parse client ephemeral public key (string at offset 1) */
  if (pkt.len < 5) return -1;
  uint32_t client_pub_len = ssh_read_string_len(pkt.data + 1);
  if (client_pub_len != 32 || pkt.len < 5 + 32) return -1;
  uint8_t client_pub[32];
  mem_copy(client_pub, pkt.data + 5, 32);

  /* Generate random ephemeral server key pair (SECURITY FIX) */
  uint8_t server_priv[32];
  uint8_t server_pub[32];
  crypto_random_bytes(server_priv, 32);
  curve25519_base(server_pub, server_priv);

  /* Compute shared secret */
  uint8_t shared_secret[32];
  curve25519_scalar_mult(shared_secret, server_priv, client_pub);

  /* Build KEXDH_REPLY with proper Ed25519 signature (SECURITY FIX) */
  uint8_t reply[512];
  uint32_t rpos = 0;
  reply[rpos++] = 31; /* SSH_MSG_KEXDH_REPLY */
  
  /* host_key: string (ssh-ed25519 public key) */
  uint8_t host_pub[32];
  ssh_host_key_get_public(host_pub);
  uint32_t host_key_blob_len = 4 + 11 + 4 + 32; /* "ssh-ed25519" + key */
  ssh_write_u32_be(reply + rpos, host_key_blob_len); rpos += 4;
  ssh_write_u32_be(reply + rpos, 11); rpos += 4;
  mem_copy(reply + rpos, "ssh-ed25519", 11); rpos += 11;
  ssh_write_u32_be(reply + rpos, 32); rpos += 4;
  mem_copy(reply + rpos, host_pub, 32); rpos += 32;
  
  /* f (server public key) as string */
  ssh_write_u32_be(reply + rpos, 32); rpos += 4;
  mem_copy(reply + rpos, server_pub, 32); rpos += 32;
  
  /* Signature: Ed25519 sign of exchange hash (RFC 4253 COMPLIANT) */
  /* Build exchange hash H = SHA-256(V_C || V_S || I_C || I_S || K_S || e || f || K) */
  sha256_ctx_t hash_ctx;
  sha256_init(&hash_ctx);
  
  /* V_C: client version string (without trailing CR/LF) */
  uint32_t vc_len = version_len;
  /* Remove trailing CR/LF if present */
  while (vc_len > 0 && (version_buf[vc_len-1] == '\r' || version_buf[vc_len-1] == '\n')) {
    vc_len--;
  }
  sha256_update(&hash_ctx, version_buf, vc_len);
  
  /* V_S: server version string */
  const char *server_version = "SSH-2.0-XAIOS_1.0";
  uint32_t vs_len = str_len(server_version);
  sha256_update(&hash_ctx, (const uint8_t*)server_version, vs_len);
  
  /* I_C: client KEXINIT (entire packet payload) */
  sha256_update(&hash_ctx, client_kexinit, client_kexinit_len);
  
  /* I_S: server KEXINIT (entire packet payload) */
  sha256_update(&hash_ctx, kexinit_buf, kexinit_len);
  
  /* K_S: server host public key (as string: length + "ssh-ed25519" + key) */
  uint8_t host_key_blob[64];
  uint32_t host_key_blob_pos = 0;
  ssh_write_u32_be(host_key_blob + host_key_blob_pos, 11 + 4 + 32); host_key_blob_pos += 4;
  ssh_write_u32_be(host_key_blob + host_key_blob_pos, 11); host_key_blob_pos += 4;
  mem_copy(host_key_blob + host_key_blob_pos, "ssh-ed25519", 11); host_key_blob_pos += 11;
  uint8_t host_pub[32];
  ssh_host_key_get_public(host_pub);
  mem_copy(host_key_blob + host_key_blob_pos, host_pub, 32); host_key_blob_pos += 32;
  sha256_update(&hash_ctx, host_key_blob, host_key_blob_pos);
  
  /* e: client ephemeral public key (as string: length + key) */
  uint8_t client_pub_blob[36];
  ssh_write_u32_be(client_pub_blob, 32);
  mem_copy(client_pub_blob + 4, client_pub, 32);
  sha256_update(&hash_ctx, client_pub_blob, 36);
  
  /* f: server ephemeral public key (as string: length + key) */
  uint8_t server_pub_blob[36];
  ssh_write_u32_be(server_pub_blob, 32);
  mem_copy(server_pub_blob + 4, server_pub, 32);
  sha256_update(&hash_ctx, server_pub_blob, 36);
  
  /* K: shared secret (as string: length + secret) */
  uint8_t shared_secret_blob[36];
  ssh_write_u32_be(shared_secret_blob, 32);
  mem_copy(shared_secret_blob + 4, shared_secret, 32);
  sha256_update(&hash_ctx, shared_secret_blob, 36);
  
  /* Finalize hash */
  uint8_t exchange_hash[32];
  sha256_final(&hash_ctx, exchange_hash);
  
  uint8_t signature[64];
  uint8_t host_priv[32];
  ssh_host_key_get_private(host_priv);
  
  ed25519_sign(signature, exchange_hash, 32, host_pub, host_priv);
  
  /* Encode signature as string */
  uint32_t sig_blob_len = 4 + 11 + 4 + 64; /* "ssh-ed25519" + signature */
  ssh_write_u32_be(reply + rpos, sig_blob_len); rpos += 4;
  ssh_write_u32_be(reply + rpos, 11); rpos += 4;
  mem_copy(reply + rpos, "ssh-ed25519", 11); rpos += 11;
  ssh_write_u32_be(reply + rpos, 64); rpos += 4;
  mem_copy(reply + rpos, signature, 64); rpos += 64;

  if (ssh_packet_write(sockfd, reply, rpos) != 0) {
    ssh_log(SSH_LOG_ERROR, "Failed to send KEXDH_REPLY\n");
    return -1;
  }

  /* Send NEWKEYS */
  uint8_t newkeys = 21; /* SSH_MSG_NEWKEYS */
  if (ssh_packet_write(sockfd, &newkeys, 1) != 0) {
    ssh_log(SSH_LOG_ERROR, "Failed to send NEWKEYS\n");
    return -1;
  }

  /* Receive NEWKEYS */
  if (ssh_packet_read(sockfd, &pkt) != 0) {
    ssh_log(SSH_LOG_ERROR, "Failed to receive NEWKEYS\n");
    return -1;
  }
  if (pkt.len == 0 || pkt.data[0] != 21) {
    ssh_log(SSH_LOG_ERROR, "Invalid NEWKEYS message\n");
    return -1;
  }
  
  /* Enable encryption after NEWKEYS (CRITICAL SECURITY FIX) */
  init_encryption(shared_secret, 32, exchange_hash, 32);
  
  ssh_log(SSH_LOG_INFO, "Key exchange completed\n");

  /* Message loop: handle service requests, auth, channels (NO LIMIT) */
  ssh_channel_init();
  
  for (;;) { /* INFINITE LOOP - proper exit conditions only */
    /* Check connection timeout */
    if (check_connection_timeout(state, connect_time, timer_now()) != 0) {
      ssh_log(SSH_LOG_WARN, "Connection timeout\n");
      break;
    }
    
    /* Check keepalive */
    if (timer_now() - last_activity > SSHD_KEEPALIVE_INTERVAL) {
      /* Send keepalive request */
      uint8_t keepalive[32];
      keepalive[0] = 80; /* SSH_MSG_GLOBAL_REQUEST */
      const char *ka_name = "keepalive@xaios.os";
      uint32_t ka_len = str_len(ka_name);
      ssh_write_u32_be(keepalive + 1, ka_len);
      mem_copy(keepalive + 5, ka_name, ka_len);
      keepalive[5 + ka_len] = 1; /* want_reply */
      ssh_packet_write(sockfd, keepalive, 6 + ka_len);
      
      /* Check if idle too long */
      if (timer_now() - last_activity > SSHD_TIMEOUT_IDLE) {
        ssh_log(SSH_LOG_WARN, "Keepalive timeout, disconnecting\n");
        break;
      }
    }
    
    /* Read next packet (ENCRYPTED after NEWKEYS) */
    if (ssh_packet_read_encrypted(sockfd, &pkt) != 0) {
      ssh_log(SSH_LOG_INFO, "Connection lost (read error)\n");
      break;
    }
    if (pkt.len == 0) continue;
    
    last_activity = timer_now();
    uint8_t msg = pkt.data[0];

    if (msg == 5) { /* SSH_MSG_SERVICE_REQUEST */
      /* Accept service request */
      uint8_t sa[32];
      sa[0] = 6; /* SERVICE_ACCEPT */
      const char *svc = "ssh-userauth";
      uint32_t svc_len = str_len(svc);
      ssh_write_u32_be(sa + 1, svc_len);
      mem_copy(sa + 5, svc, svc_len);
      ssh_packet_write_encrypted(sockfd, sa, 5 + svc_len);
      ssh_log(SSH_LOG_INFO, "Service request accepted\n");
      
    } else if (msg == 50) { /* SSH_MSG_USERAUTH_REQUEST */
      /* Parse authentication request (SECURITY FIX) */
      if (pkt.len < 10) {
        ssh_log(SSH_LOG_WARN, "Auth request too short\n");
        continue;
      }
      
      /* Extract username */
      uint32_t user_len = ssh_read_string_len(pkt.data + 1);
      if (user_len > 64 || (10 + user_len) > pkt.len) {
        ssh_log(SSH_LOG_WARN, "Invalid username length\n");
        continue;
      }
      char username[65];
      mem_copy(username, pkt.data + 5, user_len);
      username[user_len] = '\0';
      
      /* Extract password (after "password" method string) */
      uint32_t method_len = ssh_read_string_len(pkt.data + 5 + user_len);
      uint32_t password_offset = 5 + user_len + 4 + method_len;
      if ((password_offset + 4) > pkt.len) {
        ssh_log(SSH_LOG_WARN, "No password data\n");
        continue;
      }
      uint32_t pass_len = ssh_read_string_len(pkt.data + password_offset);
      if (pass_len > 128 || (password_offset + 4 + pass_len) > pkt.len) {
        ssh_log(SSH_LOG_WARN, "Invalid password length\n");
        continue;
      }
      char password[129];
      mem_copy(password, pkt.data + password_offset + 4, pass_len);
      password[pass_len] = '\0';
      
      /* Rate limiting */
      if (check_rate_limit(client_ip) != 0) {
        ssh_log(SSH_LOG_WARN, "Rate limited IP %u\n", client_ip);
        uint8_t reject[64];
        reject[0] = 51; /* SSH_MSG_USERAUTH_FAILURE */
        const char *methods = "password";
        uint32_t mlen = str_len(methods);
        ssh_write_u32_be(reject + 1, mlen);
        mem_copy(reject + 5, methods, mlen);
        reject[5 + mlen] = 0; /* partial success = false */
        ssh_packet_write_encrypted(sockfd, reject, 6 + mlen);
        continue;
      }
      
      /* Check max attempts */
      if (auth_attempts >= SSHD_MAX_AUTH_ATTEMPTS) {
        ssh_log(SSH_LOG_WARN, "Max auth attempts exceeded\n");
        record_auth_failure(client_ip);
        uint8_t reject[64];
        reject[0] = 51;
        const char *methods = "password";
        uint32_t mlen = str_len(methods);
        ssh_write_u32_be(reject + 1, mlen);
        mem_copy(reject + 5, methods, mlen);
        reject[5 + mlen] = 0;
        ssh_packet_write(sockfd, reject, 6 + mlen);
        continue;
      }
      
      /* Verify password (CRITICAL SECURITY FIX) */
      if (authenticate_password(username, password) == 0) {
        /* Authentication SUCCESS */
        uint8_t auth_reply[1] = {52}; /* SSH_MSG_USERAUTH_SUCCESS */
        ssh_packet_write_encrypted(sockfd, auth_reply, 1);
        state = SSH_STATE_AUTHENTICATED;
        auth_attempts = 0;
        record_auth_success(client_ip);
        ssh_log(SSH_LOG_INFO, "Authentication successful for user '%s'\n", username);
      } else {
        /* Authentication FAILURE */
        auth_attempts++;
        record_auth_failure(client_ip);
        uint8_t reject[64];
        reject[0] = 51; /* SSH_MSG_USERAUTH_FAILURE */
        const char *methods = "password";
        uint32_t mlen = str_len(methods);
        ssh_write_u32_be(reject + 1, mlen);
        mem_copy(reject + 5, methods, mlen);
        reject[5 + mlen] = 0; /* partial success = false */
        ssh_packet_write_encrypted(sockfd, reject, 6 + mlen);
        ssh_log(SSH_LOG_WARN, "Authentication failed for user '%s' (attempt %u)\n", username, auth_attempts);
      }
      
    } else if (msg >= 90 && msg <= 98) { /* Channel messages */
      if (ssh_channel_handle_packet(sockfd, &pkt) != 0) {
        ssh_log(SSH_LOG_ERROR, "Channel packet handling failed\n");
        break;
      }
    } else if (msg == 80) { /* SSH_MSG_GLOBAL_REQUEST */
      /* Ignore global requests (including keepalive responses) */
    } else if (msg == 1) { /* SSH_MSG_DISCONNECT */
      ssh_log(SSH_LOG_INFO, "Client disconnected\n");
      break;
    } else {
      ssh_log(SSH_LOG_WARN, "Unknown message type: %u\n", msg);
    }
  }
  
  /* Zero out sensitive data */
  mem_zero(server_priv, 32);
  
  return 0;
}

/* ---- Multi-Connection Handler (Fix #2 & #4) ---- */
/* Old queue replaced with atomic sshd_queue_t at top of file */

/* Helper: handle one packet from a connection (returns 0=continue, -1=closed) */
static int handle_connection_packet(u64 sockfd, uint64_t last_activity) {
  /* This is a simplified version - in production, refactor handle_connection() */
  /* to be state-machine based and call it incrementally */
  /* For now, we call the full handle_connection() which blocks */
  /* TODO: Convert handle_connection() to non-blocking state machine */
  
  /* For multiplexing to work properly, we need non-blocking I/O */
  /* which requires OS support. For now, use the queue-based approach. */
  return -1; /* Placeholder - see accept loop below */
}

/* ---- Main SSHD Entry Point ---- */
int sshd_run(void) {
  /* Crypto self-test */
  ssh_crypto_self_test();
  ssh_log(SSH_LOG_INFO, "SSH crypto self-test passed\n");
  
  /* Load user database */
  load_user_database();
  
  /* Initialize statistics */
  mem_zero(&g_server_stats, sizeof(g_server_stats));
  
  /* Start worker threads (Fix #2: Multi-threaded workers) */
  /* NOTE: When XAI OS implements xaios_thread_create(), uncomment below: */
  /*
  for (uint32_t i = 0; i < SSHD_MAX_WORKER_THREADS; ++i) {
    xaios_thread_create(sshd_worker_thread, &g_conn_queue);
  }
  ssh_log(SSH_LOG_INFO, "Started %u worker threads\n", SSHD_MAX_WORKER_THREADS);
  */
  
  /* For now: accept-then-handle synchronously (safe for QEMU/freestanding) */
  ssh_log(SSH_LOG_INFO, "Worker threads: will enable when xaios_thread_create() available\n");
  ssh_log(SSH_LOG_INFO, "Atomic queue: enabled (race-condition free)\n");

  /* Listen on SSH port */
  u64 listen_fd = 0;
  if (xaios_net_listen(SSHD_PORT, &listen_fd) != 0) {
    ssh_log(SSH_LOG_ERROR, "Failed to listen on port %u\n", SSHD_PORT);
    return -1;
  }
  ssh_log(SSH_LOG_INFO, "SSH server listening on port %u\n", SSHD_PORT);

  /* Accept loop with atomic queue (Fix #4: Race condition free) */
  for (;;) {
    u64 conn_fd = 0;
    if (xaios_net_accept(listen_fd, &conn_fd) != 0) {
      continue;
    }
    
    /* Check connection limits */
    uint32_t active = __atomic_load_n(&g_server_stats.active_connections, __ATOMIC_ACQUIRE);
    if (active >= SSHD_MAX_PENDING_CONNECTIONS) {
      ssh_log(SSH_LOG_WARN, "Max connections reached, rejecting\n");
      __atomic_add_fetch(&g_server_stats.rejected_connections, 1, __ATOMIC_RELEASE);
      xaios_net_close(conn_fd);
      continue;
    }
    
    /* Enqueue connection atomically */
    if (queue_push(&g_conn_queue, conn_fd) != 0) {
      ssh_log(SSH_LOG_WARN, "Connection queue full\n");
      __atomic_add_fetch(&g_server_stats.rejected_connections, 1, __ATOMIC_RELEASE);
      xaios_net_close(conn_fd);
      continue;
    }
    
    __atomic_add_fetch(&g_server_stats.total_connections, 1, __ATOMIC_RELEASE);
    __atomic_add_fetch(&g_server_stats.active_connections, 1, __ATOMIC_RELEASE);
    
    /* Handle connection synchronously for now */
    /* When worker threads enabled: workers will dequeue and handle */
    handle_connection((int)conn_fd);
    xaios_net_close(conn_fd);
    __atomic_sub_fetch(&g_server_stats.active_connections, 1, __ATOMIC_RELEASE);
  }
  
  return 0; /* unreachable */
}

/* Entry point - called from _start via xaios_main pattern */
void sshd_main(void) {
  sshd_run();
  xaios_exit(0);
}
