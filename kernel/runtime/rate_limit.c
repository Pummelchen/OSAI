#include <xaios/assert.h>
#include <xaios/klog.h>
#include <xaios/rate_limit.h>
#include <xaios/timer.h>

static xaios_rate_limit_t g_rate_limits[XAIOS_RATE_LIMIT_MAX_CELLS];
static uint64_t g_total_throttle_events;

static void bucket_init(xaios_token_bucket_t *bucket, uint64_t capacity,
                        uint64_t refill_rate) {
  bucket->capacity = capacity;
  bucket->tokens = capacity;
  bucket->refill_rate = refill_rate;
  bucket->last_refill_ns = timer_now_ns();
}

static void bucket_refill(xaios_token_bucket_t *bucket) {
  uint64_t now = timer_now_ns();
  if (now <= bucket->last_refill_ns) {
    return;
  }
  uint64_t elapsed_ns = now - bucket->last_refill_ns;
  uint64_t new_tokens = (elapsed_ns * bucket->refill_rate) / UINT64_C(1000000000);
  if (new_tokens > 0) {
    bucket->tokens += new_tokens;
    if (bucket->tokens > bucket->capacity) {
      bucket->tokens = bucket->capacity;
    }
    bucket->last_refill_ns = now;
  }
}

static xaios_status_t bucket_consume(xaios_token_bucket_t *bucket,
                                    uint64_t count) {
  bucket_refill(bucket);
  if (bucket->tokens < count) {
    return XAIOS_ERR_BUSY;
  }
  bucket->tokens -= count;
  return XAIOS_OK;
}

void rate_limit_init(void) {
  for (uint32_t i = 0; i < XAIOS_RATE_LIMIT_MAX_CELLS; ++i) {
    g_rate_limits[i].cell_id = i;
    g_rate_limits[i].active = 0;
    g_rate_limits[i].memory_cap_bytes = 0;
    g_rate_limits[i].memory_used_bytes = 0;
    g_rate_limits[i].cpu_violations = 0;
    g_rate_limits[i].net_violations = 0;
    g_rate_limits[i].memory_violations = 0;
    g_rate_limits[i].total_throttle_events = 0;
  }
  g_total_throttle_events = 0;
  klog("rate_limit: initialized max_cells=%u\n", XAIOS_RATE_LIMIT_MAX_CELLS);
}

xaios_status_t rate_limit_create(uint32_t cell_id, uint64_t cpu_capacity,
                                uint64_t cpu_refill_per_sec,
                                uint64_t net_capacity,
                                uint64_t net_refill_per_sec,
                                uint64_t memory_cap_bytes) {
  if (cell_id >= XAIOS_RATE_LIMIT_MAX_CELLS) {
    return XAIOS_ERR_INVALID;
  }
  xaios_rate_limit_t *rl = &g_rate_limits[cell_id];
  rl->cell_id = cell_id;
  rl->active = 1;
  rl->memory_cap_bytes = memory_cap_bytes;
  rl->memory_used_bytes = 0;
  rl->cpu_violations = 0;
  rl->net_violations = 0;
  rl->memory_violations = 0;
  rl->total_throttle_events = 0;

  bucket_init(&rl->cpu_bucket, cpu_capacity, cpu_refill_per_sec);
  bucket_init(&rl->net_rx_bucket, net_capacity, net_refill_per_sec);
  bucket_init(&rl->net_tx_bucket, net_capacity, net_refill_per_sec);

  return XAIOS_OK;
}

xaios_status_t rate_limit_destroy(uint32_t cell_id) {
  if (cell_id >= XAIOS_RATE_LIMIT_MAX_CELLS) {
    return XAIOS_ERR_INVALID;
  }
  g_rate_limits[cell_id].active = 0;
  g_rate_limits[cell_id].memory_used_bytes = 0;
  return XAIOS_OK;
}

xaios_status_t rate_limit_cpu_consume(uint32_t cell_id, uint64_t tokens) {
  if (cell_id >= XAIOS_RATE_LIMIT_MAX_CELLS || !g_rate_limits[cell_id].active) {
    return XAIOS_ERR_INVALID;
  }
  xaios_rate_limit_t *rl = &g_rate_limits[cell_id];
  xaios_status_t status = bucket_consume(&rl->cpu_bucket, tokens);
  if (status != XAIOS_OK) {
    ++rl->cpu_violations;
    ++rl->total_throttle_events;
    ++g_total_throttle_events;
  }
  return status;
}

xaios_status_t rate_limit_check_memory(uint32_t cell_id, uint64_t bytes) {
  if (cell_id >= XAIOS_RATE_LIMIT_MAX_CELLS || !g_rate_limits[cell_id].active) {
    return XAIOS_ERR_INVALID;
  }
  xaios_rate_limit_t *rl = &g_rate_limits[cell_id];
  if (rl->memory_used_bytes + bytes > rl->memory_cap_bytes) {
    ++rl->memory_violations;
    ++rl->total_throttle_events;
    ++g_total_throttle_events;
    return XAIOS_ERR_NO_MEMORY;
  }
  return XAIOS_OK;
}

xaios_status_t rate_limit_memory_commit(uint32_t cell_id, uint64_t bytes) {
  xaios_status_t status = rate_limit_check_memory(cell_id, bytes);
  if (status != XAIOS_OK) {
    return status;
  }
  g_rate_limits[cell_id].memory_used_bytes += bytes;
  return XAIOS_OK;
}

void rate_limit_memory_release(uint32_t cell_id, uint64_t bytes) {
  if (cell_id >= XAIOS_RATE_LIMIT_MAX_CELLS || !g_rate_limits[cell_id].active) {
    return;
  }
  if (bytes > g_rate_limits[cell_id].memory_used_bytes) {
    g_rate_limits[cell_id].memory_used_bytes = 0;
  } else {
    g_rate_limits[cell_id].memory_used_bytes -= bytes;
  }
}

xaios_status_t rate_limit_check_network(uint32_t cell_id, uint64_t bytes,
                                       uint32_t is_tx) {
  if (cell_id >= XAIOS_RATE_LIMIT_MAX_CELLS || !g_rate_limits[cell_id].active) {
    return XAIOS_ERR_INVALID;
  }
  xaios_rate_limit_t *rl = &g_rate_limits[cell_id];
  xaios_token_bucket_t *bucket =
      is_tx ? &rl->net_tx_bucket : &rl->net_rx_bucket;
  xaios_status_t status = bucket_consume(bucket, bytes);
  if (status != XAIOS_OK) {
    ++rl->net_violations;
    ++rl->total_throttle_events;
    ++g_total_throttle_events;
  }
  return status;
}

uint64_t rate_limit_cpu_violations(uint32_t cell_id) {
  if (cell_id >= XAIOS_RATE_LIMIT_MAX_CELLS) {
    return 0;
  }
  return g_rate_limits[cell_id].cpu_violations;
}

uint64_t rate_limit_net_violations(uint32_t cell_id) {
  if (cell_id >= XAIOS_RATE_LIMIT_MAX_CELLS) {
    return 0;
  }
  return g_rate_limits[cell_id].net_violations;
}

uint64_t rate_limit_memory_violations(uint32_t cell_id) {
  if (cell_id >= XAIOS_RATE_LIMIT_MAX_CELLS) {
    return 0;
  }
  return g_rate_limits[cell_id].memory_violations;
}

uint64_t rate_limit_total_throttle_events(void) {
  return g_total_throttle_events;
}

void rate_limit_self_test(void) {
  uint32_t test_cell = 0;

  /* Create rate limiter: 100 CPU tokens, 1/sec refill (low to avoid timing
   * issues in self-test loop), 4096 net tokens, 1/sec net refill, 4096 mem cap */
  kassert(rate_limit_create(test_cell, 100, 1, 4096, 1, 4096) == XAIOS_OK);
  kassert(g_rate_limits[test_cell].active == 1);

  /* Consume all 100 CPU tokens */
  for (uint32_t i = 0; i < 100; ++i) {
    kassert(rate_limit_cpu_consume(test_cell, 1) == XAIOS_OK);
  }

  /* Next consume should be throttled */
  kassert(rate_limit_cpu_consume(test_cell, 1) == XAIOS_ERR_BUSY);
  kassert(rate_limit_cpu_violations(test_cell) == 1);

  /* Memory: allocate 4096 bytes (at cap) */
  kassert(rate_limit_memory_commit(test_cell, 4096) == XAIOS_OK);

  /* One more byte should fail */
  kassert(rate_limit_check_memory(test_cell, 1) == XAIOS_ERR_NO_MEMORY);
  kassert(rate_limit_memory_violations(test_cell) == 1);

  /* Release some memory and try again */
  rate_limit_memory_release(test_cell, 2048);
  kassert(rate_limit_check_memory(test_cell, 1024) == XAIOS_OK);

  /* Network: consume all RX bandwidth */
  for (uint32_t i = 0; i < 4096; ++i) {
    rate_limit_check_network(test_cell, 1, 0);
  }
  /* Next byte should be dropped */
  kassert(rate_limit_check_network(test_cell, 1, 0) == XAIOS_ERR_BUSY);
  kassert(rate_limit_net_violations(test_cell) == 1);

  /* TX should still have tokens */
  kassert(rate_limit_check_network(test_cell, 1, 1) == XAIOS_OK);

  /* Verify throttle event count */
  kassert(rate_limit_total_throttle_events() >= 3);

  /* Destroy */
  kassert(rate_limit_destroy(test_cell) == XAIOS_OK);
  kassert(g_rate_limits[test_cell].active == 0);

  /* Out-of-range should fail */
  kassert(rate_limit_create(XAIOS_RATE_LIMIT_MAX_CELLS, 100, 100, 100, 100, 100) ==
          XAIOS_ERR_INVALID);

  klog("rate_limit: self-test passed throttle_events=%lu\n",
       rate_limit_total_throttle_events());
}
