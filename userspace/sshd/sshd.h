#ifndef SSHD_H
#define SSHD_H

#include <stdint.h>
#include <xaios_user.h>

#define SSHD_PORT 22U

/* Connection limits */
#define SSHD_MAX_WORKER_THREADS 16
#define SSHD_MAX_PENDING_CONNECTIONS 1024
#define SSHD_MAX_CONNECTIONS_PER_IP 10

/* Timeout values (seconds) */
#define SSHD_TIMEOUT_CONNECT 30
#define SSHD_TIMEOUT_AUTH 120
#define SSHD_TIMEOUT_IDLE 300
#define SSHD_KEEPALIVE_INTERVAL 30

/* Rate limiting */
#define SSHD_RATE_LIMIT_MAX_ENTRIES 1024
#define SSHD_RATE_LIMIT_MAX_FAILURES 10
#define SSHD_RATE_LIMIT_BAN_DURATION 3600 /* 1 hour */

/* Authentication */
#define SSHD_MAX_AUTH_ATTEMPTS 5
#define SSHD_MAX_USERS 100
#define SSHD_USERNAME_MAX 64
#define SSHD_PASSWORD_HASH_SIZE 32

/* User database entry */
typedef struct {
  char username[SSHD_USERNAME_MAX];
  uint8_t password_hash[SSHD_PASSWORD_HASH_SIZE];
  int active;
} sshd_user_t;

/* Rate limiting entry */
typedef struct {
  uint32_t ip_address;
  uint64_t last_attempt_time;
  uint32_t failure_count;
  uint64_t ban_until;
} sshd_rate_limit_entry_t;

/* Connection statistics */
typedef struct {
  uint32_t active_connections;
  uint32_t total_connections;
  uint32_t rejected_connections;
  uint64_t bytes_sent;
  uint64_t bytes_received;
} sshd_stats_t;

/* Lock-free connection queue (atomic operations) */
typedef struct {
  volatile u64 connections[SSHD_MAX_PENDING_CONNECTIONS];
  volatile uint32_t head;  /* Atomic: consumers read */
  volatile uint32_t tail;  /* Atomic: producers write */
  volatile uint32_t count; /* Atomic: queue size */
} sshd_queue_t;

/* Active connection tracking for multi-client support */
typedef struct {
  u64 sockfd;
  int active;
  uint32_t client_ip;
  uint64_t last_activity;
} sshd_active_conn_t;

#define SSHD_MAX_ACTIVE_CONNECTIONS 64

/* Logging levels */
#define SSH_LOG_INFO  0
#define SSH_LOG_WARN  1
#define SSH_LOG_ERROR 2

void ssh_log(int level, const char *fmt, ...);
int sshd_run(void);

#endif
