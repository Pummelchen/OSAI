#include <xaios/assert.h>
#include <xaios/inference_preempt.h>
#include <xaios/kheap.h>
#include <xaios/klog.h>

/*
 * Inference Preemption Implementation
 *
 * Checkpoint/restore for fair scheduling of long-running inferences.
 */

static void bytes_copy(void *dst, const void *src, uint64_t size) {
  uint8_t *out = (uint8_t *)dst;
  const uint8_t *in = (const uint8_t *)src;
  for (uint64_t i = 0; i < size; ++i) {
    out[i] = in[i];
  }
}

void inference_preempt_init(xaios_preempt_manager_t *mgr) {
  kassert(mgr != 0);
  
  for (uint32_t i = 0; i < XAIOS_PREEMPT_MAX_CHECKPOINTS; ++i) {
    mgr->checkpoints[i].checkpoint_id = i;
    mgr->checkpoints[i].valid = 0;
    mgr->checkpoints[i].activation_buffer = 0;
    mgr->checkpoints[i].activation_bytes = 0;
  }
  
  mgr->num_checkpoints = 0;
  mgr->next_checkpoint_id = 0;
  mgr->checkpoint_count = 0;
  mgr->restore_count = 0;
  mgr->preemption_count = 0;
  mgr->total_preempted_ticks = 0;
  
  klog("inference-preempt: initialized max_checkpoints=%u\n",
       XAIOS_PREEMPT_MAX_CHECKPOINTS);
}

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
                                      uint32_t *checkpoint_id_out) {
  kassert(mgr != 0 && activation_data != 0 && checkpoint_id_out != 0);
  
  if (activation_bytes > XAIOS_PREEMPT_MAX_ACTIVATION_BYTES) {
    return XAIOS_ERR_INVALID; /* Activation too large */
  }
  
  /* Find empty checkpoint slot */
  int slot = -1;
  for (uint32_t i = 0; i < XAIOS_PREEMPT_MAX_CHECKPOINTS; ++i) {
    if (mgr->checkpoints[i].valid == 0) {
      slot = (int)i;
      break;
    }
  }
  
  if (slot < 0) {
    return XAIOS_ERR_NO_MEMORY; /* No checkpoint slots available */
  }
  
  /* Allocate activation buffer from kernel heap */
  void *buffer = kheap_alloc(activation_bytes, 64);
  if (buffer == 0) {
    klog("inference-preempt: failed to allocate activation buffer (%lu bytes)\n",
         activation_bytes);
    return XAIOS_ERR_NO_MEMORY;
  }
  
  bytes_copy(buffer, activation_data, activation_bytes);
  
  /* Save checkpoint */
  xaios_inference_checkpoint_t *ckpt = &mgr->checkpoints[slot];
  ckpt->checkpoint_id = mgr->next_checkpoint_id++;
  ckpt->cell_id = cell_id;
  ckpt->request_id = request_id;
  ckpt->current_layer = current_layer;
  ckpt->current_token = current_token;
  ckpt->current_batch = 0;
  ckpt->elapsed_ticks = elapsed_ticks;
  ckpt->activation_buffer = buffer;
  ckpt->activation_bytes = activation_bytes;
  ckpt->kv_tokens_committed = kv_tokens_committed;
  ckpt->output_offset = output_offset;
  ckpt->valid = 1;
  
  mgr->num_checkpoints++;
  mgr->checkpoint_count++;
  mgr->preemption_count++;
  mgr->total_preempted_ticks += elapsed_ticks;
  
  *checkpoint_id_out = ckpt->checkpoint_id;
  
  klog("inference-preempt: saved checkpoint_id=%u cell=%u request=%u layer=%u token=%u ticks=%lu\n",
       ckpt->checkpoint_id, cell_id, request_id, current_layer,
       current_token, elapsed_ticks);
  
  return XAIOS_OK;
}

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
    uint64_t *elapsed_ticks_out) {
  kassert(mgr != 0 && activation_data != 0);
  
  /* Find checkpoint */
  int slot = -1;
  for (uint32_t i = 0; i < XAIOS_PREEMPT_MAX_CHECKPOINTS; ++i) {
    if (mgr->checkpoints[i].valid != 0 &&
        mgr->checkpoints[i].checkpoint_id == checkpoint_id) {
      slot = (int)i;
      break;
    }
  }
  
  if (slot < 0) {
    return XAIOS_ERR_INVALID; /* Checkpoint not found */
  }
  
  const xaios_inference_checkpoint_t *ckpt = &mgr->checkpoints[slot];
  
  /* Check activation buffer capacity */
  if (ckpt->activation_bytes > activation_capacity) {
    return XAIOS_ERR_NO_MEMORY;
  }
  
  /* Restore state */
  bytes_copy(activation_data, ckpt->activation_buffer, ckpt->activation_bytes);
  
  if (cell_id_out != 0) *cell_id_out = ckpt->cell_id;
  if (request_id_out != 0) *request_id_out = ckpt->request_id;
  if (current_layer_out != 0) *current_layer_out = ckpt->current_layer;
  if (current_token_out != 0) *current_token_out = ckpt->current_token;
  if (activation_bytes_out != 0) *activation_bytes_out = ckpt->activation_bytes;
  if (kv_tokens_committed_out != 0) *kv_tokens_committed_out = ckpt->kv_tokens_committed;
  if (output_offset_out != 0) *output_offset_out = ckpt->output_offset;
  if (elapsed_ticks_out != 0) *elapsed_ticks_out = ckpt->elapsed_ticks;
  
  mgr->restore_count++;
  
  klog("inference-preempt: restored checkpoint_id=%u cell=%u request=%u layer=%u token=%u\n",
       checkpoint_id, ckpt->cell_id, ckpt->request_id,
       ckpt->current_layer, ckpt->current_token);
  
  return XAIOS_OK;
}

xaios_status_t inference_preempt_delete(xaios_preempt_manager_t *mgr,
                                        uint32_t checkpoint_id) {
  kassert(mgr != 0);
  
  /* Find checkpoint */
  int slot = -1;
  for (uint32_t i = 0; i < XAIOS_PREEMPT_MAX_CHECKPOINTS; ++i) {
    if (mgr->checkpoints[i].valid != 0 &&
        mgr->checkpoints[i].checkpoint_id == checkpoint_id) {
      slot = (int)i;
      break;
    }
  }
  
  if (slot < 0) {
    return XAIOS_ERR_INVALID;
  }
  
  /* Clear checkpoint — free the activation buffer first */
  if (mgr->checkpoints[slot].activation_buffer != 0) {
    kheap_free(mgr->checkpoints[slot].activation_buffer);
  }
  mgr->checkpoints[slot].valid = 0;
  mgr->checkpoints[slot].activation_buffer = 0;
  mgr->checkpoints[slot].activation_bytes = 0;
  mgr->num_checkpoints--;
  
  klog("inference-preempt: deleted checkpoint_id=%u\n", checkpoint_id);
  return XAIOS_OK;
}

void inference_preempt_get_stats(const xaios_preempt_manager_t *mgr,
                                 uint64_t *checkpoint_count,
                                 uint64_t *restore_count,
                                 uint64_t *preemption_count,
                                 uint64_t *total_preempted_ticks) {
  kassert(mgr != 0);
  
  if (checkpoint_count != 0) *checkpoint_count = mgr->checkpoint_count;
  if (restore_count != 0) *restore_count = mgr->restore_count;
  if (preemption_count != 0) *preemption_count = mgr->preemption_count;
  if (total_preempted_ticks != 0) *total_preempted_ticks = mgr->total_preempted_ticks;
}

int inference_should_preempt(uint64_t start_ticks,
                             uint64_t current_ticks,
                             uint64_t max_ticks) {
  if (max_ticks == 0) {
    return 0; /* No preemption limit */
  }
  
  uint64_t elapsed = current_ticks - start_ticks;
  return elapsed > max_ticks;
}
