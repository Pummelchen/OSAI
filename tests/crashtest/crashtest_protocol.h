/*
 * XAI OS Crash Test Protocol Definitions
 * 
 * Binary protocol for communication between crashtest_server (host Mac)
 * and crashtest_client (inside XAI OS).
 * 
 * Message Format:
 * ┌────────┬────────┬──────────┬─────────────────────────────┐
 * │ Type   │ Length │ Test ID  │ Payload                     │
 * │ 1 byte │ 2 bytes│ 2 bytes  │ Variable length             │
 * └────────┴────────┴──────────┴─────────────────────────────┘
 */

#ifndef XAIOS_CRASHTEST_PROTOCOL_H
#define XAIOS_CRASHTEST_PROTOCOL_H

/* Message Types */
#define CRASHTEST_MSG_TEST_COMMAND   0x01  /* Server → Client: Execute test */
#define CRASHTEST_MSG_TEST_RESULT    0x02  /* Client → Server: Test result */
#define CRASHTEST_MSG_CRASH_REPORT   0x03  /* Client → Server: Crash detected */
#define CRASHTEST_MSG_HEARTBEAT      0x04  /* Bidirectional: Keepalive */
#define CRASHTEST_MSG_LOG_MESSAGE    0x05  /* Client → Server: Log output */
#define CRASHTEST_MSG_TEST_ABORT     0x06  /* Server → Client: Timeout/abort */

/* Test Status Codes */
#define CRASHTEST_STATUS_PASS        0x00
#define CRASHTEST_STATUS_FAIL        0x01
#define CRASHTEST_STATUS_CRASH       0x02
#define CRASHTEST_STATUS_TIMEOUT     0x03
#define CRASHTEST_STATUS_SKIP        0x04

/* Test Categories */
#define CRASHTEST_CAT_TCP_ATTACK     1   /* TCP stack attacks (1-20) */
#define CRASHTEST_CAT_UDP_ATTACK     2   /* UDP stack attacks (21-35) */
#define CRASHTEST_CAT_ICMP_ATTACK    3   /* ICMP attacks (36-45) */
#define CRASHTEST_CAT_SSH_ATTACK     4   /* SSH protocol attacks (46-65) */
#define CRASHTEST_CAT_ARP_ATTACK     5   /* ARP attacks (66-75) */
#define CRASHTEST_CAT_NET_FUZZ       6   /* Network protocol fuzzing (76-90) */
#define CRASHTEST_CAT_CONN_MGMT      7   /* Connection management (91-100) */
#define CRASHTEST_CAT_MEM_CORRUPT    8   /* Memory corruption (101-120) */
#define CRASHTEST_CAT_SYSCALL_ABUSE  9   /* Syscall abuse (121-135) */
#define CRASHTEST_CAT_FS_DESTRUCT    10  /* Filesystem destruction (136-155) */
#define CRASHTEST_CAT_CPU_EXHAUST    11  /* CPU exhaustion (156-170) */
#define CRASHTEST_CAT_AI_ATTACK      12  /* AI stack attacks (171-185) */
#define CRASHTEST_CAT_THREAD_CHAOS   13  /* Threading chaos (186-200) */

/* Crash Types */
#define CRASHTEST_CRASH_NONE         0x00
#define CRASHTEST_CRASH_KERNEL_PANIC 0x01
#define CRASHTEST_CRASH_SEGFAULT     0x02
#define CRASHTEST_CRASH_OOM          0x03
#define CRASHTEST_CRASH_DEADLOCK     0x04
#define CRASHTEST_CRASH_STACK_OVERFLOW 0x05
#define CRASHTEST_CRASH_WATCHDOG     0x06
#define CRASHTEST_CRASH_TCP_LOST     0x07

/* Log Levels */
#define CRASHTEST_LOG_INFO           0x00
#define CRASHTEST_LOG_WARN           0x01
#define CRASHTEST_LOG_ERROR          0x02
#define CRASHTEST_LOG_DEBUG          0x03

/* Protocol Constants */
#define CRASHTEST_MAX_PAYLOAD_SIZE   4096
#define CRASHTEST_HEARTBEAT_INTERVAL 5    /* seconds */
#define CRASHTEST_TEST_TIMEOUT       30   /* seconds */
#define CRASHTEST_PORT               9999

/* Message Header Structure (5 bytes) */
typedef struct crashtest_msg_header {
  unsigned char type;        /* Message type (1 byte) */
  unsigned short length;     /* Payload length (2 bytes, big-endian) */
  unsigned short test_id;    /* Test ID (2 bytes, big-endian) */
} __attribute__((packed)) crashtest_msg_header_t;

/* TEST_COMMAND Payload */
typedef struct crashtest_test_command {
  unsigned char category;    /* Test category (1 byte) */
  unsigned short test_number;/* Test number within category (2 bytes) */
  unsigned char param_len;   /* Parameter length (1 byte) */
  unsigned char params[256]; /* Test parameters (variable) */
} __attribute__((packed)) crashtest_test_command_t;

/* TEST_RESULT Payload */
typedef struct crashtest_test_result {
  unsigned char status;      /* PASS/FAIL/CRASH/TIMEOUT (1 byte) */
  unsigned int exec_time_ms; /* Execution time in milliseconds (4 bytes) */
  unsigned char detail_len;  /* Detail message length (1 byte) */
  char details[512];         /* Result details (variable) */
} __attribute__((packed)) crashtest_test_result_t;

/* CRASH_REPORT Payload */
typedef struct crashtest_crash_report {
  unsigned char crash_type;  /* Type of crash (1 byte) */
  unsigned short last_syscall;/* Last syscall number (2 bytes) */
  unsigned int crash_addr;   /* Crash address (4 bytes) */
  unsigned char stack_len;   /* Stack trace length (1 byte) */
  unsigned int stack_trace[16]; /* Stack trace (up to 16 addresses) */
} __attribute__((packed)) crashtest_crash_report_t;

/* HEARTBEAT Payload */
typedef struct crashtest_heartbeat {
  unsigned long long timestamp; /* Current timestamp (8 bytes) */
  unsigned int tests_completed; /* Total tests completed (4 bytes) */
  unsigned int tests_passed;    /* Tests passed (4 bytes) */
  unsigned int tests_failed;    /* Tests failed (4 bytes) */
} __attribute__((packed)) crashtest_heartbeat_t;

/* LOG_MESSAGE Payload */
typedef struct crashtest_log_message {
  unsigned char level;       /* Log level (1 byte) */
  unsigned short test_id;    /* Current test ID (2 bytes) */
  unsigned char msg_len;     /* Message length (1 byte) */
  char message[1024];        /* Log message (variable) */
} __attribute__((packed)) crashtest_log_message_t;

/* Helper Functions */

/* Write big-endian 16-bit value */
static inline void crashtest_write_u16_be(unsigned char *buf, unsigned short val) {
  buf[0] = (val >> 8) & 0xFF;
  buf[1] = val & 0xFF;
}

/* Read big-endian 16-bit value */
static inline unsigned short crashtest_read_u16_be(const unsigned char *buf) {
  return ((unsigned short)buf[0] << 8) | (unsigned short)buf[1];
}

/* Write big-endian 32-bit value */
static inline void crashtest_write_u32_be(unsigned char *buf, unsigned int val) {
  buf[0] = (val >> 24) & 0xFF;
  buf[1] = (val >> 16) & 0xFF;
  buf[2] = (val >> 8) & 0xFF;
  buf[3] = val & 0xFF;
}

/* Read big-endian 32-bit value */
static inline unsigned int crashtest_read_u32_be(const unsigned char *buf) {
  return ((unsigned int)buf[0] << 24) |
         ((unsigned int)buf[1] << 16) |
         ((unsigned int)buf[2] << 8) |
         (unsigned int)buf[3];
}

/* Write big-endian 64-bit value */
static inline void crashtest_write_u64_be(unsigned char *buf, unsigned long long val) {
  buf[0] = (val >> 56) & 0xFF;
  buf[1] = (val >> 48) & 0xFF;
  buf[2] = (val >> 40) & 0xFF;
  buf[3] = (val >> 32) & 0xFF;
  buf[4] = (val >> 24) & 0xFF;
  buf[5] = (val >> 16) & 0xFF;
  buf[6] = (val >> 8) & 0xFF;
  buf[7] = val & 0xFF;
}

/* Read big-endian 64-bit value */
static inline unsigned long long crashtest_read_u64_be(const unsigned char *buf) {
  return ((unsigned long long)buf[0] << 56) |
         ((unsigned long long)buf[1] << 48) |
         ((unsigned long long)buf[2] << 40) |
         ((unsigned long long)buf[3] << 32) |
         ((unsigned long long)buf[4] << 24) |
         ((unsigned long long)buf[5] << 16) |
         ((unsigned long long)buf[6] << 8) |
         (unsigned long long)buf[7];
}

#endif /* XAIOS_CRASHTEST_PROTOCOL_H */
