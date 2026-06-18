#ifndef XAIOS_INFERENCE_PREEMPT_H
#define XAIOS_INFERENCE_PREEMPT_H

#include <xaios/status.h>
#include <xaios/types.h>

/*
 * Inference Preemption with Checkpoint/Restore
 *
 * Allows long-running inferences to be paused and resumed for fair scheduling.
 * Prevents single requests from hogging AI cells for seconds.
 */

#define XAIOS_PREEMPT_MAX_CHECKPOINTS 8U    /* Max saved checkpoints */
#define XAIOS_PREEMPT_MAX_ACTIVATION_BYTES 65536U  /* 64KB max activation state */

/* Inference checkpoint */
typedef struct xaios_inference_checkpoint {
  uint32_t checkpoint_id;
  uint32_t cell_id;
  uint32_t request_id;
  
  /* Execution state */
  uint32_t current_layer;           /* Current neural network layer */
  uint32_t current_token;           /* Current token being processed */
  uint32_t current_batch;           /* Current batch index */
  uint64_t elapsed_ticks;           /* Ticks spent so far */
  
  /* Activation state (intermediate results) */
  void *activation_buffer;          /* Saved activations */
  uint64_t activation_bytes;        /* Size of activation buffer */
  
  /* KV cache state */
  uint32_t kv_tokens_committed;     /* Tokens already in KV cache */
  
  /* Output state */
  uint64_t output_offset;           /* Bytes written to output so far */
  
  uint32_t valid;                   /* 1 = valid checkpoint, 0 = empty */
} xaios_inference_checkpoint_t;

/* Checkpoint manager */
typedef struct xaios_preempt_manager {
  xaios_inference_checkpoint_t checkpoints[XAIOS_PREEMPT_MAX_CHECKPOINTS];
  uint32_t num_checkpoints;
  uint32_t next_checkpoint_id;
  
  /* Statistics */
  uint64_t checkpoint_count;
  uint64_t restore_count;
  uint64_t preemption_count;
  uint64_t total_preempted_ticks;
} xaios_preempt_manager_t;

/* Initialize preemption manager */
void inference_preempt_init(xaios_preempt_manager_t *mgr);

/* Create checkpoint for current inference state */
xaios_status_t inference_preempt_save(xaios_preempt_manager_t *mgr,
                                      uint32_t cell_id,
                                      uint32_t request_id,
                                      uint32_t current_layer,
                                      uint32_t current_token,
                                      const void *activation_data,
                                      uint64_t activation_bytes,
                                      uint32_t kv_tokens_committed,
                                      uint64_t output_offset,
                                      uint64_t elapsed_ticks,
                                      uint32_t *checkpoint_id_out);

/* Restore inference state from checkpoint */
xaios_status_t inference_preempt_restore(
    xaios_preempt_manager_t *mgr,
    uint32_t checkpoint_id,
    uint32_t *cell_id_out,
    uint32_t *request_id_out,
    uint32_t *current_layer_out,
    uint32_t *current_token_out,
    void *activation_data,
    uint64_t activation_capacity,
    uint64_t *activation_bytes_out,
    uint32_t *kv_tokens_committed_out,
    uint64_t *output_offset_out,
    uint64_t *elapsed_ticks_out);

/* Delete a checkpoint */
xaios_status_t inference_preempt_delete(xaios_preempt_manager_t *mgr,
                                        uint32_t checkpoint_id);

/* Get checkpoint statistics */
void inference_preempt_get_stats(const xaios_preempt_manager_t *mgr,
                                 uint64_t *checkpoint_count,
                                 uint64_t *restore_count,
                                 uint64_t *preemption_count,
                                 uint64_t *total_preempted_ticks);

/* Check if preemption is needed (based on elapsed time) */
int inference_should_preempt(uint64_t start_ticks,
                             uint64_t current_ticks,
                             uint64_t max_ticks);

#endif
