#include <xaios/assert.h>
#include <xaios/ai_kernels.h>
#include <xaios/klog.h>
#include <xaios/model_parallel.h>
#include <xaios/timer.h>

/*
 * Model Parallelism Implementation
 *
 * Pipeline parallelism for large models across multiple AI cells.
 * Enables running 70B+ parameter models that don't fit in single cell memory.
 */

static void bytes_copy(void *dst, const void *src, uint64_t size) {
  uint8_t *out = (uint8_t *)dst;
  const uint8_t *in = (const uint8_t *)src;
  for (uint64_t i = 0; i < size; ++i) {
    out[i] = in[i];
  }
}

xaios_status_t model_parallel_init(xaios_model_parallel_config_t *config,
                                   uint32_t config_id,
                                   uint32_t total_layers,
                                   uint32_t num_cells,
                                   uint64_t activation_bytes) {
  kassert(config != 0 && num_cells > 0 && num_cells <= XAIOS_MP_MAX_CELLS_PER_MODEL);
  kassert(total_layers > 0 && activation_bytes > 0);
  
  config->config_id = config_id;
  config->total_layers = total_layers;
  config->num_cells = num_cells;
  config->current_micro_batch = 0;
  config->total_micro_batches = 0;
  config->pipeline_latency_ticks = 0;
  
  /* Initialize cell ranges */
  for (uint32_t i = 0; i < num_cells; ++i) {
    config->cell_ranges[i].cell_id = i;
    config->cell_ranges[i].start_layer = 0;
    config->cell_ranges[i].end_layer = 0;
    config->cell_ranges[i].num_layers = 0;
    config->cell_ranges[i].model_arena_id = 0;
    
    config->activation_buffers[i] = 0;
    config->activation_bytes[i] = 0;
  }
  
  klog("model-parallel: initialized config_id=%u layers=%u cells=%u activation_bytes=%lu\n",
       config_id, total_layers, num_cells, activation_bytes);
  
  return XAIOS_OK;
}

xaios_status_t model_parallel_assign_layers(
    xaios_model_parallel_config_t *config,
    uint32_t cell_id,
    uint32_t start_layer,
    uint32_t num_layers,
    uint32_t model_arena_id) {
  kassert(config != 0 && num_layers > 0);
  
  if (cell_id >= config->num_cells) {
    return XAIOS_ERR_INVALID;
  }
  
  if (start_layer + num_layers > config->total_layers) {
    return XAIOS_ERR_INVALID;
  }
  
  config->cell_ranges[cell_id].cell_id = cell_id;
  config->cell_ranges[cell_id].start_layer = start_layer;
  config->cell_ranges[cell_id].end_layer = start_layer + num_layers;
  config->cell_ranges[cell_id].num_layers = num_layers;
  config->cell_ranges[cell_id].model_arena_id = model_arena_id;
  
  klog("model-parallel: cell %u assigned layers %u-%u (%u layers) arena=%u\n",
       cell_id, start_layer, start_layer + num_layers - 1, num_layers,
       model_arena_id);
  
  return XAIOS_OK;
}

static xaios_status_t execute_cell_forward(
    uint32_t cell_id,
    uint32_t model_arena_id,
    uint32_t start_layer,
    uint32_t num_layers,
    const void *input_activation,
    void *output_activation,
    uint64_t activation_bytes) {
  /* Placeholder: In production, this would call the actual cell execution */
  /* For now, just copy input to output (identity function) */
  (void)cell_id;
  (void)model_arena_id;
  (void)start_layer;
  (void)num_layers;
  
  bytes_copy(output_activation, input_activation, activation_bytes);
  
  return XAIOS_OK;
}

xaios_status_t model_parallel_forward(xaios_model_parallel_config_t *config,
                                      xaios_pipeline_state_t *state,
                                      const void *input,
                                      void *output,
                                      uint64_t activation_bytes) {
  kassert(config != 0 && state != 0 && input != 0 && output != 0);
  kassert(activation_bytes <= XAIOS_MP_MAX_ACTIVATION_BYTES);
  
  state->start_ticks = timer_counter();
  state->current_stage = 0;
  state->stage_complete_mask = 0;
  state->activation_bytes = activation_bytes;
  
  /* Copy input to first activation buffer */
  bytes_copy(config->activation_buffers[0], input, activation_bytes);
  
  /* Execute pipeline stages sequentially */
  for (uint32_t stage = 0; stage < config->num_cells; ++stage) {
    uint64_t stage_start = timer_counter();
    
    const xaios_mp_layer_range_t *range = &config->cell_ranges[stage];
    if (range->num_layers == 0) {
      continue; /* Skip unassigned cells */
    }
    
    /* Execute this cell's layers */
    xaios_status_t status = execute_cell_forward(
        range->cell_id,
        range->model_arena_id,
        range->start_layer,
        range->num_layers,
        config->activation_buffers[stage],
        (stage + 1 < config->num_cells) ? config->activation_buffers[stage + 1] : output,
        activation_bytes);
    
    if (status != XAIOS_OK) {
      klog("model-parallel: cell %u forward pass failed with status=%d\n",
           stage, status);
      return status;
    }
    
    uint64_t stage_elapsed = timer_counter() - stage_start;
    state->stage_ticks[stage] = stage_elapsed;
    state->stage_complete_mask |= (1U << stage);
    state->current_stage = stage + 1;
    
    klog("model-parallel: stage %u complete, layers %u-%u, %lu ticks\n",
         stage, range->start_layer, range->end_layer - 1, stage_elapsed);
  }
  
  state->pipeline_latency_ticks = timer_counter() - state->start_ticks;
  config->pipeline_latency_ticks = state->pipeline_latency_ticks;
  
  klog("model-parallel: pipeline complete, %u stages, %lu ticks total\n",
       config->num_cells, state->pipeline_latency_ticks);
  
  return XAIOS_OK;
}

void model_parallel_get_stats(const xaios_model_parallel_config_t *config,
                              const xaios_pipeline_state_t *state,
                              uint64_t *total_latency,
                              uint64_t *stage_latency,
                              uint32_t *num_stages_complete) {
  kassert(config != 0 && state != 0);
  
  if (total_latency != 0) {
    *total_latency = state->pipeline_latency_ticks;
  }
  
  if (stage_latency != 0) {
    for (uint32_t i = 0; i < config->num_cells; ++i) {
      ((uint64_t *)stage_latency)[i] = state->stage_ticks[i];
    }
  }
  
  if (num_stages_complete != 0) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < config->num_cells; ++i) {
      if ((state->stage_complete_mask & (1U << i)) != 0) {
        count++;
      }
    }
    *num_stages_complete = count;
  }
}

void model_parallel_destroy(xaios_model_parallel_config_t *config) {
  kassert(config != 0);
  
  /* Reset configuration */
  config->num_cells = 0;
  config->total_layers = 0;
  config->current_micro_batch = 0;
  config->pipeline_latency_ticks = 0;
  
  for (uint32_t i = 0; i < XAIOS_MP_MAX_CELLS_PER_MODEL; ++i) {
    config->cell_ranges[i].num_layers = 0;
    config->activation_buffers[i] = 0;
    config->activation_bytes[i] = 0;
  }
  
  klog("model-parallel: destroyed config_id=%u\n", config->config_id);
}
