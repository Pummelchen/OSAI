#ifndef OSAI_KLOG_RING_H
#define OSAI_KLOG_RING_H

#include <osai/status.h>
#include <osai/types.h>

typedef enum osai_log_level {
  OSAI_LOG_DEBUG = 0,
  OSAI_LOG_INFO = 1,
  OSAI_LOG_WARN = 2,
  OSAI_LOG_ERROR = 3,
  OSAI_LOG_PANIC = 4,
} osai_log_level_t;

#define OSAI_KLOG_RING_SIZE UINT32_C(65536)
#define OSAI_KLOG_LINE_MAX UINT32_C(256)
#define OSAI_KLOG_FLUSH_MAX UINT32_C(8192)

void klog_ring_init(void);
void klog_ring_write(const char *data, uint32_t length);
uint32_t klog_ring_read(char *out, uint32_t max_len);
void klog_ring_clear(void);
uint32_t klog_ring_count(void);
uint32_t klog_ring_overflow_count(void);

osai_status_t klog_flush(void);
osai_status_t klog_rotate(void);
uint64_t klog_persist_count(void);
uint64_t klog_rotate_count(void);
void klog_ring_self_test(void);

#endif
