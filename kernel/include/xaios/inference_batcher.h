#ifndef XAIOS_INFERENCE_BATCHER_H
#define XAIOS_INFERENCE_BATCHER_H

#include <xaios/status.h>
#include <xaios/types.h>

/*
 * Continuous Batching for Multi-Request Throughput
 *
 * Batches multiple inference requests together for parallel processing.
 * Similar to vLLM's continuous batching, achieving 10-50× throughput improvement.
 */

#define XAIOS_BATCH_MAX_REQUESTS 32U      /* Max concurrent requests in batch */
#define XAIOS_BATCH_MAX_TOKENS 4096U      /* Max tokens per batch */

/* Inference request */
typedef struct xaios_inference_request {
  uint32_t request_id;
  uint32_t seq_id;              /* KV cache sequence ID */
  const uint8_t *input_tokens;
  uint32_t num_input_tokens;
  char *output_buffer;
  uint64_t output_capacity;
  uint64_t *output_bytes;
  uint32_t state;               /* 0=pending, 1=processing, 2=complete, 3=error */
  uint32_t priority;            /* Higher = more important */
  uint64_t arrival_time;        /* Tick count when request arrived */
} xaios_inference_request_t;

/* Batch manager */
typedef struct xaios_inference_batch {
  uint32_t batch_id;
  xaios_inference_request_t requests[XAIOS_BATCH_MAX_REQUESTS];
  uint32_t num_requests;
  uint32_t total_tokens;
  uint32_t capacity;
  
  /* Scheduling state */
  uint32_t current_request_idx;
  uint32_t processed_tokens;
  uint64_t start_time;
  
  /* Statistics */
  uint64_t total_requests_processed;
  uint64_t total_tokens_processed;
  uint64_t avg_batch_size;
  uint64_t batch_count;
} xaios_inference_batch_t;

/* Initialize batch manager */
xaios_status_t inference_batch_init(xaios_inference_batch_t *batch,
                                    uint32_t batch_id);

/* Submit a new inference request */
xaios_status_t inference_batch_submit(xaios_inference_batch_t *batch,
                                      uint32_t request_id,
                                      uint32_t seq_id,
                                      const uint8_t *input_tokens,
                                      uint32_t num_tokens,
                                      char *output_buffer,
                                      uint64_t output_capacity,
                                      uint64_t *output_bytes,
                                      uint32_t priority);

/* Check if batch is full */
int inference_batch_is_full(const xaios_inference_batch_t *batch);

/* Get number of pending requests */
uint32_t inference_batch_pending_count(const xaios_inference_batch_t *batch);

/* Execute batch (process all pending requests) */
xaios_status_t inference_batch_execute(xaios_inference_batch_t *batch,
                                       uint32_t cell_id);

/* Reset batch for next round */
void inference_batch_reset(xaios_inference_batch_t *batch);

/* Get batch statistics */
void inference_batch_get_stats(const xaios_inference_batch_t *batch,
                               uint64_t *total_requests,
                               uint64_t *total_tokens,
                               uint64_t *avg_batch_size,
                               uint64_t *batch_count);

#endif
