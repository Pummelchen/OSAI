#ifndef XAIOS_AI_CELL_H
#define XAIOS_AI_CELL_H

#include <xaios/status.h>
#include <xaios/types.h>

#define XAIOS_AI_CELL_DESCRIPTOR_MAGIC UINT32_C(0x41494331)
#define XAIOS_AI_CELL_DESCRIPTOR_VERSION UINT32_C(1)
#define XAIOS_AI_CELL_DESCRIPTOR_FLAG_CPU_ONLY UINT32_C(1)
#define XAIOS_AI_CELL_DESCRIPTOR_FLAG_FIXED_CORES (UINT32_C(1) << 1)
#define XAIOS_AI_CELL_DESCRIPTOR_FLAG_SHARED_MODEL (UINT32_C(1) << 2)
#define XAIOS_AI_CELL_DESCRIPTOR_FLAG_PRIVATE_KV (UINT32_C(1) << 3)
#define XAIOS_AI_CELL_DESCRIPTOR_FLAG_NIC_QUEUE (UINT32_C(1) << 4)
#define XAIOS_AI_CELL_DESCRIPTOR_FLAG_GIT_WORKSPACE (UINT32_C(1) << 5)
#define XAIOS_AI_CELL_DESCRIPTOR_REQUIRED_FLAGS                         \
  (XAIOS_AI_CELL_DESCRIPTOR_FLAG_CPU_ONLY |                              \
   XAIOS_AI_CELL_DESCRIPTOR_FLAG_FIXED_CORES |                           \
   XAIOS_AI_CELL_DESCRIPTOR_FLAG_SHARED_MODEL |                          \
   XAIOS_AI_CELL_DESCRIPTOR_FLAG_PRIVATE_KV |                            \
   XAIOS_AI_CELL_DESCRIPTOR_FLAG_NIC_QUEUE |                             \
   XAIOS_AI_CELL_DESCRIPTOR_FLAG_GIT_WORKSPACE)
#define XAIOS_AI_CELL_DESCRIPTOR_NAME_MAX 32U

typedef enum xaios_ai_cell_state {
  XAIOS_AI_CELL_EMPTY = 0,
  XAIOS_AI_CELL_CREATED = 1,
  XAIOS_AI_CELL_READY = 2,
  XAIOS_AI_CELL_RUNNING = 3,
  XAIOS_AI_CELL_STOPPED = 4,
  XAIOS_AI_CELL_FAILED = 5,
} xaios_ai_cell_state_t;

typedef struct xaios_ai_cell_manifest {
  const char *name;
  uint32_t core_mask;
  uint32_t model_arena_id;
  uint64_t kv_cache_bytes;
  uint64_t source_index_bytes;
  uint32_t nic_queue_id;
  uint32_t git_workspace_id;
} xaios_ai_cell_manifest_t;

typedef struct xaios_ai_cell_descriptor_v1 {
  uint32_t magic;
  uint32_t version;
  uint32_t descriptor_bytes;
  uint32_t flags;
  uint32_t cell_id;
  uint32_t core_mask;
  uint32_t model_arena_id;
  uint32_t nic_queue_id;
  uint32_t git_workspace_id;
  uint32_t reserved0;
  uint64_t kv_cache_bytes;
  uint64_t source_index_bytes;
  uint64_t build_output_bytes;
  uint64_t log_bytes;
  char name[XAIOS_AI_CELL_DESCRIPTOR_NAME_MAX];
  uint64_t checksum;
} xaios_ai_cell_descriptor_v1_t;

typedef struct xaios_ai_cell {
  uint32_t cell_id;
  xaios_ai_cell_state_t state;
  xaios_ai_cell_manifest_t manifest;
  char name_storage[XAIOS_AI_CELL_DESCRIPTOR_NAME_MAX];
  uint64_t lifecycle_generation;
  uint32_t kv_cache_arena_id;
  uint32_t source_index_arena_id;
  uint32_t build_output_arena_id;
  uint32_t log_arena_id;
  uint64_t kv_cache_base;
  uint64_t source_index_base;
  uint64_t build_output_base;
  uint64_t log_base;
} xaios_ai_cell_t;

void ai_cell_runtime_init(void);
xaios_status_t ai_cell_create_from_descriptor(
    const xaios_ai_cell_descriptor_v1_t *descriptor);
xaios_status_t ai_cell_create(uint32_t cell_id,
                             const xaios_ai_cell_manifest_t *manifest);
xaios_status_t ai_cell_prepare(uint32_t cell_id);
xaios_status_t ai_cell_start(uint32_t cell_id);
xaios_status_t ai_cell_stop(uint32_t cell_id);
uint64_t ai_cell_transition_count(void);
uint64_t ai_cell_descriptor_accept_count(void);
uint64_t ai_cell_descriptor_reject_count(void);
uint64_t ai_cell_resource_admission_count(void);
uint64_t ai_cell_resource_reject_count(void);
uint64_t ai_cell_arena_pages_reserved(void);
uint64_t ai_cell_arena_bytes_reserved(void);
uint64_t ai_cell_arena_pages_peak(void);
uint64_t ai_cell_arena_bytes_peak(void);
uint64_t ai_cell_queue_bind_count(void);
uint64_t ai_cell_queue_release_count(void);
uint64_t ai_cell_workspace_bind_count(void);
uint64_t ai_cell_workspace_release_count(void);
uint64_t ai_cell_conflict_count(void);
void ai_cell_self_test(void);

#endif
