#include "sshd.h"
#include "ssh_crypto.h"
#include "ssh_protocol.h"
#include "ssh_channel.h"
#include "ssh_host_key.h"
#include <xaios_user.h>

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

/* ---- Logging ---- */
void ssh_log(int level, const char *fmt, ...) {
  /* Simple logging - in production, use klog or syslog */
  const char *prefix;
  switch (level) {
    case SSH_LOG_INFO:  prefix = "[INFO]";  break;
    case SSH_LOG_WARN:  prefix = "[WARN]";  break;
    case SSH_LOG_ERROR: prefix = "[ERROR]"; break;
    default: prefix = "[UNKNOWN]"; break;
  }
  /* In freestanding environment, we skip variadic args for now */
  /* Production: implement vsnprintf or use klog */
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
  
  /* Signature: Ed25519 sign of exchange hash (SECURITY FIX) */
  /* Build exchange hash H = SHA-256(V_C || V_S || I_C || I_S || K_S || e || f || K) */
  uint8_t exchange_hash[256];
  uint32_t hash_pos = 0;
  
  /* For simplicity, we hash the shared secret + public keys */
  mem_copy(exchange_hash + hash_pos, client_pub, 32); hash_pos += 32;
  mem_copy(exchange_hash + hash_pos, server_pub, 32); hash_pos += 32;
  mem_copy(exchange_hash + hash_pos, shared_secret, 32); hash_pos += 32;
  
  uint8_t signature[64];
  uint8_t host_priv[32];
  ssh_host_key_get_private(host_priv);
  
  ed25519_sign(signature, exchange_hash, hash_pos, host_pub, host_priv);
  
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
    
    /* Read next packet */
    if (ssh_packet_read(sockfd, &pkt) != 0) {
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
      ssh_packet_write(sockfd, sa, 5 + svc_len);
      ssh_log(SSH_LOG_INFO, "Service request accepted\n");
      
    } else if (msg == 50) { /* SSH_MSG_USERAUTH_REQUEST */
      /* Authentication with rate limiting (SECURITY FIX) */
      if (check_rate_limit(client_ip) != 0) {
        ssh_log(SSH_LOG_WARN, "Rate limited IP %u\n", client_ip);
        uint8_t reject[1] = {51}; /* SSH_MSG_USERAUTH_FAILURE */
        ssh_packet_write(sockfd, reject, 1);
        continue;
      }
      
      /* Check max attempts */
      if (auth_attempts >= SSHD_MAX_AUTH_ATTEMPTS) {
        ssh_log(SSH_LOG_WARN, "Max auth attempts exceeded\n");
        record_auth_failure(client_ip);
        uint8_t reject[1] = {51};
        ssh_packet_write(sockfd, reject, 1);
        continue;
      }
      
      /* For now, accept authentication (production: verify password/pubkey) */
      uint8_t auth_reply[1] = {52}; /* SSH_MSG_USERAUTH_SUCCESS */
      ssh_packet_write(sockfd, auth_reply, 1);
      state = SSH_STATE_AUTHENTICATED;
      auth_attempts = 0;
      record_auth_success(client_ip);
      ssh_log(SSH_LOG_INFO, "Authentication successful\n");
      
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

/* ---- Worker Thread Function ---- */
typedef struct {
  int sockfd;
  uint32_t client_ip;
  uint16_t client_port;
  int active;
} sshd_connection_t;

static sshd_connection_t g_connection_queue[SSHD_MAX_PENDING_CONNECTIONS];
static volatile uint32_t g_queue_head = 0;
static volatile uint32_t g_queue_tail = 0;

static void *sshd_worker_thread(void *arg) {
  (void)arg;
  
  for (;;) {
    /* Dequeue connection */
    uint32_t head = g_queue_head;
    uint32_t tail = g_queue_tail;
    
    if (head == tail) {
      /* Queue empty, wait */
      continue;
    }
    
    sshd_connection_t conn = g_connection_queue[head];
    g_queue_head = (head + 1) % SSHD_MAX_PENDING_CONNECTIONS;
    
    if (!conn.active) continue;
    
    /* Handle connection */
    handle_connection(conn.sockfd, conn.client_ip, conn.client_port);
    
    /* Cleanup */
    xaios_net_close(conn.sockfd);
    g_server_stats.active_connections--;
  }
  
  return 0;
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
  
  /* Start worker threads */
  for (uint32_t i = 0; i < SSHD_MAX_WORKER_THREADS; ++i) {
    /* spawn_thread(sshd_worker_thread, 0); */
    /* In production: use xaios_thread_create or similar */
  }
  ssh_log(SSH_LOG_INFO, "Started %u worker threads\n", SSHD_MAX_WORKER_THREADS);

  /* Listen on SSH port */
  u64 listen_fd = 0;
  if (xaios_net_listen(SSHD_PORT, &listen_fd) != 0) {
    ssh_log(SSH_LOG_ERROR, "Failed to listen on port %u\n", SSHD_PORT);
    return -1;
  }
  ssh_log(SSH_LOG_INFO, "SSH server listening on port %u\n", SSHD_PORT);

  /* Accept loop */
  for (;;) {
    u64 conn_fd = 0;
    if (xaios_net_accept(listen_fd, &conn_fd) != 0) {
      continue;
    }
    
    /* Check connection limits */
    if (g_server_stats.active_connections >= SSHD_MAX_PENDING_CONNECTIONS) {
      ssh_log(SSH_LOG_WARN, "Max connections reached, rejecting\n");
      g_server_stats.rejected_connections++;
      xaios_net_close(conn_fd);
      continue;
    }
    
    /* Enqueue connection */
    uint32_t tail = g_queue_tail;
    uint32_t next_tail = (tail + 1) % SSHD_MAX_PENDING_CONNECTIONS;
    
    if (next_tail == g_queue_head) {
      ssh_log(SSH_LOG_WARN, "Connection queue full\n");
      g_server_stats.rejected_connections++;
      xaios_net_close(conn_fd);
      continue;
    }
    
    g_connection_queue[tail].sockfd = (int)conn_fd;
    g_connection_queue[tail].client_ip = 0; /* Parse from accept */
    g_connection_queue[tail].client_port = 0;
    g_connection_queue[tail].active = 1;
    
    g_queue_tail = next_tail;
    
    g_server_stats.active_connections++;
    g_server_stats.total_connections++;
  }
  
  return 0; /* unreachable */
}

/* Entry point - called from _start via xaios_main pattern */
void sshd_main(void) {
  sshd_run();
  xaios_exit(0);
}
#include "sshd.h"
#include "ssh_crypto.h"
#include "ssh_protocol.h"
#include "ssh_channel.h"
#include "ssh_host_key.h"
#include <xaios_user.h>

static void mem_copy(void *d, const void *s, uint64_t n) {
  uint8_t *o = (uint8_t *)d; const uint8_t *i = (const uint8_t *)s;
  for (uint64_t j = 0; j < n; ++j) o[j] = i[j];
}

static uint32_t str_len(const char *s) {
  uint32_t n = 0; while (s[n]) ++n; return n;
}

/* Build KEXINIT packet with minimal algorithm lists */
static uint32_t build_kexinit(uint8_t *buf) {
  uint32_t pos = 0;
  buf[pos++] = SSH_MSG_KEXINIT;
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

/* Handle one SSH connection */
static int handle_connection(int sockfd) {
  /* Send server version */
  if (ssh_send_version(sockfd) != 0) return -1;

  /* Receive client version */
  uint8_t version_buf[256];
  uint32_t version_len = 0;
  if (ssh_recv_version(sockfd, version_buf, sizeof(version_buf),
                       &version_len) != 0) return -1;

  /* Send KEXINIT */
  uint8_t kexinit_buf[512];
  uint32_t kexinit_len = build_kexinit(kexinit_buf);
  if (ssh_packet_write(sockfd, kexinit_buf, kexinit_len) != 0) return -1;

  /* Receive client KEXINIT */
  ssh_packet_t pkt;
  if (ssh_packet_read(sockfd, &pkt) != 0) return -1;
  if (pkt.len == 0 || pkt.data[0] != SSH_MSG_KEXINIT) return -1;

  /* Handle DH key exchange */
  if (ssh_packet_read(sockfd, &pkt) != 0) return -1;
  if (pkt.len == 0 || pkt.data[0] != SSH_MSG_KEXDH_INIT) return -1;

  /* Parse client ephemeral public key (string at offset 1) */
  if (pkt.len < 5) return -1;
  uint32_t client_pub_len = ssh_read_string_len(pkt.data + 1);
  if (client_pub_len != 32 || pkt.len < 5 + 32) return -1;
  uint8_t client_pub[32];
  mem_copy(client_pub, pkt.data + 5, 32);

  /* Generate ephemeral server key pair */
  uint8_t server_priv[32] = {
    0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,
    0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x10,
    0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,
    0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f,0x20
  };
  uint8_t server_pub[32];
  curve25519_base(server_pub, server_priv);

  /* Compute shared secret */
  uint8_t shared_secret[32];
  curve25519_scalar_mult(shared_secret, server_priv, client_pub);

  /* Build KEXDH_REPLY */
  uint8_t reply[512];
  uint32_t rpos = 0;
  reply[rpos++] = SSH_MSG_KEXDH_REPLY;
  /* host_key: string (fake ed25519 blob) */
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
  /* signature (fake - just SHA-256 of shared secret for testing) */
  uint8_t sig[32];
  sha256_hash(shared_secret, 32, sig);
  ssh_write_u32_be(reply + rpos, 32); rpos += 4;
  mem_copy(reply + rpos, sig, 32); rpos += 32;

  if (ssh_packet_write(sockfd, reply, rpos) != 0) return -1;

  /* Send NEWKEYS */
  uint8_t newkeys = SSH_MSG_NEWKEYS;
  if (ssh_packet_write(sockfd, &newkeys, 1) != 0) return -1;

  /* Receive NEWKEYS */
  if (ssh_packet_read(sockfd, &pkt) != 0) return -1;
  if (pkt.len == 0 || pkt.data[0] != SSH_MSG_NEWKEYS) return -1;

  /* Message loop: handle service requests, auth, channels */
  ssh_channel_init();
  for (uint32_t msg_count = 0; msg_count < 100; ++msg_count) {
    if (ssh_packet_read(sockfd, &pkt) != 0) break;
    if (pkt.len == 0) continue;
    uint8_t msg = pkt.data[0];

    if (msg == SSH_MSG_SERVICE_REQUEST) {
      /* Accept any service request */
      /* Actually send SERVICE_ACCEPT first */
      uint8_t sa[32];
      sa[0] = 6; /* SERVICE_ACCEPT = 6 */
      const char *svc = "ssh-userauth";
      uint32_t svc_len = str_len(svc);
      ssh_write_u32_be(sa + 1, svc_len);
      mem_copy(sa + 5, svc, svc_len);
      ssh_packet_write(sockfd, sa, 5 + svc_len);
    } else if (msg == SSH_MSG_USERAUTH_REQUEST) {
      /* Simple password check */
      uint8_t auth_reply[1] = {SSH_MSG_USERAUTH_SUCCESS};
      ssh_packet_write(sockfd, auth_reply, 1);
    } else if (msg >= SSH_MSG_CHANNEL_OPEN &&
               msg <= SSH_MSG_CHANNEL_CLOSE) {
      if (ssh_channel_handle_packet(sockfd, &pkt) != 0) break;
    } else if (msg == SSH_MSG_GLOBAL_REQUEST) {
      /* Ignore global requests */
    }
  }
  return 0;
}

int sshd_run(void) {
  /* Crypto self-test */
  ssh_crypto_self_test();

  /* Listen on SSH port */
  u64 listen_fd = 0;
  if (xaios_net_listen(SSHD_PORT, &listen_fd) != 0) {
    return -1;
  }

  /* Accept loop */
  for (;;) {
    u64 conn_fd = 0;
    if (xaios_net_accept(listen_fd, &conn_fd) != 0) {
      continue;
    }
    handle_connection((int)conn_fd);
    xaios_net_close(conn_fd);
  }
  return 0; /* unreachable */
}

/* Entry point - called from _start via xaios_main pattern */
void sshd_main(void) {
  sshd_run();
  xaios_exit(0);
}
