#ifndef OSAI_AI_CELL_H
#define OSAI_AI_CELL_H

#include <osai/status.h>
#include <osai/types.h>

typedef enum osai_ai_cell_state {
  OSAI_AI_CELL_EMPTY = 0,
  OSAI_AI_CELL_CREATED = 1,
  OSAI_AI_CELL_READY = 2,
  OSAI_AI_CELL_RUNNING = 3,
  OSAI_AI_CELL_STOPPED = 4,
  OSAI_AI_CELL_FAILED = 5,
} osai_ai_cell_state_t;

typedef struct osai_ai_cell_manifest {
  const char *name;
  uint32_t core_mask;
  uint32_t model_arena_id;
  uint64_t kv_cache_bytes;
  uint64_t source_index_bytes;
  uint32_t nic_queue_id;
  uint32_t git_workspace_id;
} osai_ai_cell_manifest_t;

typedef struct osai_ai_cell {
  uint32_t cell_id;
  osai_ai_cell_state_t state;
  osai_ai_cell_manifest_t manifest;
  uint64_t lifecycle_generation;
  uint32_t kv_cache_arena_id;
  uint32_t source_index_arena_id;
  uint32_t build_output_arena_id;
  uint32_t log_arena_id;
  uint64_t kv_cache_base;
  uint64_t source_index_base;
  uint64_t build_output_base;
  uint64_t log_base;
} osai_ai_cell_t;

void ai_cell_runtime_init(void);
osai_status_t ai_cell_create(uint32_t cell_id,
                             const osai_ai_cell_manifest_t *manifest);
osai_status_t ai_cell_prepare(uint32_t cell_id);
osai_status_t ai_cell_start(uint32_t cell_id);
osai_status_t ai_cell_stop(uint32_t cell_id);
uint64_t ai_cell_transition_count(void);
void ai_cell_self_test(void);

#endif
