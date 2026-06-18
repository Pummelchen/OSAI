#ifndef XAIOS_RATE_LIMIT_H
#define XAIOS_RATE_LIMIT_H

#include <xaios/status.h>
#include <xaios/types.h>

#define XAIOS_RATE_LIMIT_MAX_CELLS 8U
#define XAIOS_RATE_LIMIT_DEFAULT_CPU_CAPACITY UINT64_C(1000)
#define XAIOS_RATE_LIMIT_DEFAULT_CPU_REFILL UINT64_C(1000)
#define XAIOS_RATE_LIMIT_DEFAULT_NET_BPS UINT64_C(1048576)
#define XAIOS_RATE_LIMIT_DEFAULT_NET_CAPACITY UINT64_C(1048576)

typedef struct xaios_token_bucket {
  uint64_t capacity;
  uint64_t tokens;
  uint64_t refill_rate;
  uint64_t last_refill_ns;
} xaios_token_bucket_t;

typedef struct xaios_rate_limit {
  uint32_t cell_id;
  uint32_t active;
  xaios_token_bucket_t cpu_bucket;
  xaios_token_bucket_t net_rx_bucket;
  xaios_token_bucket_t net_tx_bucket;
  uint64_t memory_cap_bytes;
  uint64_t memory_used_bytes;
  uint64_t cpu_violations;
  uint64_t net_violations;
  uint64_t memory_violations;
  uint64_t total_throttle_events;
} xaios_rate_limit_t;

void rate_limit_init(void);
xaios_status_t rate_limit_create(uint32_t cell_id, uint64_t cpu_capacity,
                                uint64_t cpu_refill_per_sec,
                                uint64_t net_capacity, uint64_t net_refill_per_sec,
                                uint64_t memory_cap_bytes);
xaios_status_t rate_limit_destroy(uint32_t cell_id);
xaios_status_t rate_limit_cpu_consume(uint32_t cell_id, uint64_t tokens);
xaios_status_t rate_limit_check_memory(uint32_t cell_id, uint64_t bytes);
xaios_status_t rate_limit_memory_commit(uint32_t cell_id, uint64_t bytes);
void rate_limit_memory_release(uint32_t cell_id, uint64_t bytes);
xaios_status_t rate_limit_check_network(uint32_t cell_id, uint64_t bytes,
                                       uint32_t is_tx);
uint64_t rate_limit_cpu_violations(uint32_t cell_id);
uint64_t rate_limit_net_violations(uint32_t cell_id);
uint64_t rate_limit_memory_violations(uint32_t cell_id);
uint64_t rate_limit_total_throttle_events(void);
void rate_limit_self_test(void);

#endif
