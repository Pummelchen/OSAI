#ifndef XAIOS_MODEL_PARALLEL_H
#define XAIOS_MODEL_PARALLEL_H

#include <xaios/status.h>
#include <xaios/types.h>

/*
 * Model Parallelism for Large Models (70B+ parameters)
 *
 * Splits large neural networks across multiple AI cells using pipeline parallelism.
 * Each cell processes a subset of layers, passing activations to the next cell.
 *
 * Example: 80-layer model across 4 cells
 * - Cell 0: Layers 0-19
 * - Cell 1: Layers 20-39
 * - Cell 2: Layers 40-59
 * - Cell 3: Layers 60-79
 *
 * Pipeline execution: Cell0 → Cell1 → Cell2 → Cell3
 */

#define XAIOS_MP_MAX_CELLS_PER_MODEL 8U    /* Max cells in pipeline */
#define XAIOS_MP_MAX_LAYERS_PER_CELL 128U  /* Max layers per cell */
#define XAIOS_MP_MAX_ACTIVATION_BYTES 1048576U  /* 1MB max activation buffer */

/* Layer range assigned to a cell */
typedef struct xaios_mp_layer_range {
  uint32_t cell_id;
  uint32_t start_layer;
  uint32_t end_layer;          /* Exclusive */
  uint32_t num_layers;
  uint32_t model_arena_id;     /* Weights for this cell */
} xaios_mp_layer_range_t;

/* Pipeline configuration */
typedef struct xaios_model_parallel_config {
  uint32_t config_id;
  uint32_t total_layers;
  uint32_t num_cells;
  xaios_mp_layer_range_t cell_ranges[XAIOS_MP_MAX_CELLS_PER_MODEL];
  
  /* Communication buffers (between cells) */
  void *activation_buffers[XAIOS_MP_MAX_CELLS_PER_MODEL];
  uint64_t activation_bytes[XAIOS_MP_MAX_CELLS_PER_MODEL];
  
  /* Pipeline state */
  uint32_t current_micro_batch;
  uint32_t total_micro_batches;
  uint64_t pipeline_latency_ticks;
} xaios_model_parallel_config_t;

/* Pipeline execution state */
typedef struct xaios_pipeline_state {
  uint32_t config_id;
  uint32_t request_id;
  uint32_t current_stage;      /* Which cell is executing */
  uint32_t stage_complete_mask; /* Bitmask of completed stages */
  
  /* Activation flow */
  void *input_activation;      /* Input to pipeline */
  void *output_activation;     /* Output from pipeline */
  uint64_t activation_bytes;
  
  /* Timing */
  uint64_t start_ticks;
  uint64_t stage_ticks[XAIOS_MP_MAX_CELLS_PER_MODEL];
  uint64_t pipeline_latency_ticks;
} xaios_pipeline_state_t;

/* Initialize model parallel configuration */
xaios_status_t model_parallel_init(xaios_model_parallel_config_t *config,
                                   uint32_t config_id,
                                   uint32_t total_layers,
                                   uint32_t num_cells,
                                   uint64_t activation_bytes);

/* Assign layer range to a specific cell */
xaios_status_t model_parallel_assign_layers(
    xaios_model_parallel_config_t *config,
    uint32_t cell_id,
    uint32_t start_layer,
    uint32_t num_layers,
    uint32_t model_arena_id);

/* Execute pipeline forward pass */
xaios_status_t model_parallel_forward(xaios_model_parallel_config_t *config,
                                      xaios_pipeline_state_t *state,
                                      const void *input,
                                      void *output,
                                      uint64_t activation_bytes);

/* Get pipeline statistics */
void model_parallel_get_stats(const xaios_model_parallel_config_t *config,
                              const xaios_pipeline_state_t *state,
                              uint64_t *total_latency,
                              uint64_t *stage_latency,
                              uint32_t *num_stages_complete);

/* Destroy pipeline configuration */
void model_parallel_destroy(xaios_model_parallel_config_t *config);

#endif
