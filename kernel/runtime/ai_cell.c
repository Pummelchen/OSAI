#include <osai/arena.h>
#include <osai/ai_cell.h>
#include <osai/assert.h>
#include <osai/cpu_ai_runtime.h>
#include <osai/core_lease.h>
#include <osai/klog.h>
#include <osai/model_arena.h>

#define MAX_AI_CELLS 4U
#define MAX_NIC_QUEUES 4U
#define MAX_WORKSPACES 2U
#define CELL_KV_ARENA_BASE 4U
#define CELL_SOURCE_ARENA_BASE 8U
#define CELL_BUILD_ARENA_BASE 12U
#define CELL_LOG_ARENA_BASE 16U
#define CELL_BUILD_OUTPUT_BYTES UINT64_C(65536)
#define CELL_LOG_BYTES UINT64_C(65536)

static osai_ai_cell_t g_ai_cells[MAX_AI_CELLS];
static uint8_t g_nic_queue_owner[MAX_NIC_QUEUES];
static uint8_t g_workspace_owner[MAX_WORKSPACES + 1U];
static uint64_t g_transition_count;

static int str_nonempty(const char *value) {
  return value != 0 && value[0] != '\0';
}

static osai_ai_cell_t *cell_by_id(uint32_t cell_id) {
  if (cell_id >= MAX_AI_CELLS) {
    return 0;
  }
  return &g_ai_cells[cell_id];
}

static osai_status_t validate_manifest(const osai_ai_cell_manifest_t *manifest) {
  if (manifest == 0 || !str_nonempty(manifest->name)) {
    return OSAI_ERR_INVALID;
  }
  if (manifest->core_mask == 0 || manifest->kv_cache_bytes == 0 ||
      manifest->source_index_bytes == 0) {
    return OSAI_ERR_INVALID;
  }
  if (manifest->git_workspace_id == 0 ||
      manifest->git_workspace_id > MAX_WORKSPACES ||
      manifest->nic_queue_id >= MAX_NIC_QUEUES) {
    return OSAI_ERR_INVALID;
  }

  const osai_model_arena_t *arena = 0;
  if (model_arena_acquire(manifest->model_arena_id, &arena) != OSAI_OK) {
    return OSAI_ERR_INVALID;
  }
  kassert(model_arena_release(manifest->model_arena_id) == OSAI_OK);
  return OSAI_OK;
}

static osai_status_t bind_nic_queue(uint32_t cell_id, uint32_t queue_id) {
  if (queue_id >= MAX_NIC_QUEUES || g_nic_queue_owner[queue_id] != 0) {
    return OSAI_ERR_BUSY;
  }
  g_nic_queue_owner[queue_id] = (uint8_t)(cell_id + 1U);
  return OSAI_OK;
}

static void release_nic_queue(uint32_t cell_id, uint32_t queue_id) {
  if (queue_id < MAX_NIC_QUEUES &&
      g_nic_queue_owner[queue_id] == (uint8_t)(cell_id + 1U)) {
    g_nic_queue_owner[queue_id] = 0;
  }
}

static osai_status_t bind_workspace(uint32_t cell_id, uint32_t workspace_id) {
  if (workspace_id == 0 || workspace_id > MAX_WORKSPACES ||
      g_workspace_owner[workspace_id] != 0) {
    return OSAI_ERR_BUSY;
  }
  g_workspace_owner[workspace_id] = (uint8_t)(cell_id + 1U);
  return OSAI_OK;
}

static void release_workspace(uint32_t cell_id, uint32_t workspace_id) {
  if (workspace_id <= MAX_WORKSPACES &&
      g_workspace_owner[workspace_id] == (uint8_t)(cell_id + 1U)) {
    g_workspace_owner[workspace_id] = 0;
  }
}

static void destroy_cell_arenas(osai_ai_cell_t *cell) {
  if (cell->kv_cache_arena_id != 0) {
    kassert(arena_destroy(cell->kv_cache_arena_id) == OSAI_OK);
  }
  if (cell->source_index_arena_id != 0) {
    kassert(arena_destroy(cell->source_index_arena_id) == OSAI_OK);
  }
  if (cell->build_output_arena_id != 0) {
    kassert(arena_destroy(cell->build_output_arena_id) == OSAI_OK);
  }
  if (cell->log_arena_id != 0) {
    kassert(arena_destroy(cell->log_arena_id) == OSAI_OK);
  }
  cell->kv_cache_arena_id = 0;
  cell->source_index_arena_id = 0;
  cell->build_output_arena_id = 0;
  cell->log_arena_id = 0;
  cell->kv_cache_base = 0;
  cell->source_index_base = 0;
  cell->build_output_base = 0;
  cell->log_base = 0;
}

static osai_status_t reserve_memory_arenas(osai_ai_cell_t *cell) {
  const osai_arena_t *kv = 0;
  const osai_arena_t *source = 0;
  const osai_arena_t *build = 0;
  const osai_arena_t *log = 0;
  uint32_t kv_id = CELL_KV_ARENA_BASE + cell->cell_id;
  uint32_t source_id = CELL_SOURCE_ARENA_BASE + cell->cell_id;
  uint32_t build_id = CELL_BUILD_ARENA_BASE + cell->cell_id;
  uint32_t log_id = CELL_LOG_ARENA_BASE + cell->cell_id;

  if (arena_create(kv_id, OSAI_ARENA_KV_CACHE, cell->cell_id,
                   "cell-kv-cache", cell->manifest.kv_cache_bytes, 0,
                   &kv) != OSAI_OK) {
    destroy_cell_arenas(cell);
    return OSAI_ERR_NO_MEMORY;
  }
  cell->kv_cache_arena_id = kv_id;

  if (arena_create(source_id, OSAI_ARENA_SOURCE_INDEX, cell->cell_id,
                   "cell-source-index", cell->manifest.source_index_bytes, 0,
                   &source) != OSAI_OK) {
    destroy_cell_arenas(cell);
    return OSAI_ERR_NO_MEMORY;
  }
  cell->source_index_arena_id = source_id;

  if (arena_create(build_id, OSAI_ARENA_BUILD_OUTPUT, cell->cell_id,
                   "cell-build-output", CELL_BUILD_OUTPUT_BYTES, 0,
                   &build) != OSAI_OK) {
    destroy_cell_arenas(cell);
    return OSAI_ERR_NO_MEMORY;
  }
  cell->build_output_arena_id = build_id;

  if (arena_create(log_id, OSAI_ARENA_LOG, cell->cell_id, "cell-log",
                   CELL_LOG_BYTES, 0, &log) != OSAI_OK) {
    destroy_cell_arenas(cell);
    return OSAI_ERR_NO_MEMORY;
  }
  cell->log_arena_id = log_id;

  cell->kv_cache_base = kv->base;
  cell->source_index_base = source->base;
  cell->build_output_base = build->base;
  cell->log_base = log->base;
  return OSAI_OK;
}

static void copy_manifest(osai_ai_cell_manifest_t *dst,
                          const osai_ai_cell_manifest_t *src) {
  dst->name = src->name;
  dst->core_mask = src->core_mask;
  dst->model_arena_id = src->model_arena_id;
  dst->kv_cache_bytes = src->kv_cache_bytes;
  dst->source_index_bytes = src->source_index_bytes;
  dst->nic_queue_id = src->nic_queue_id;
  dst->git_workspace_id = src->git_workspace_id;
}

void ai_cell_runtime_init(void) {
  core_lease_init();
  g_transition_count = 0;
  for (uint32_t i = 0; i < MAX_NIC_QUEUES; ++i) {
    g_nic_queue_owner[i] = 0;
  }
  for (uint32_t i = 0; i <= MAX_WORKSPACES; ++i) {
    g_workspace_owner[i] = 0;
  }
  for (uint32_t i = 0; i < MAX_AI_CELLS; ++i) {
    g_ai_cells[i].cell_id = i;
    g_ai_cells[i].state = OSAI_AI_CELL_EMPTY;
    g_ai_cells[i].lifecycle_generation = 0;
    g_ai_cells[i].kv_cache_arena_id = 0;
    g_ai_cells[i].source_index_arena_id = 0;
    g_ai_cells[i].build_output_arena_id = 0;
    g_ai_cells[i].log_arena_id = 0;
    g_ai_cells[i].kv_cache_base = 0;
    g_ai_cells[i].source_index_base = 0;
    g_ai_cells[i].build_output_base = 0;
    g_ai_cells[i].log_base = 0;
  }
  klog("ai-cell: runtime initialized\n");
}

osai_status_t ai_cell_create(uint32_t cell_id,
                             const osai_ai_cell_manifest_t *manifest) {
  osai_ai_cell_t *cell = cell_by_id(cell_id);
  if (cell == 0 || cell->state != OSAI_AI_CELL_EMPTY ||
      validate_manifest(manifest) != OSAI_OK) {
    return OSAI_ERR_INVALID;
  }

  copy_manifest(&cell->manifest, manifest);
  cell->state = OSAI_AI_CELL_CREATED;
  ++cell->lifecycle_generation;
  ++g_transition_count;
  klog("ai-cell: %u created name=%s core_mask=0x%x model_arena=%u\n",
       cell_id, manifest->name, manifest->core_mask, manifest->model_arena_id);
  return OSAI_OK;
}

osai_status_t ai_cell_prepare(uint32_t cell_id) {
  osai_ai_cell_t *cell = cell_by_id(cell_id);
  if (cell == 0 || cell->state != OSAI_AI_CELL_CREATED) {
    return OSAI_ERR_INVALID;
  }
  const osai_model_arena_t *arena = 0;
  if (model_arena_acquire(cell->manifest.model_arena_id, &arena) != OSAI_OK) {
    cell->state = OSAI_AI_CELL_FAILED;
    ++g_transition_count;
    return OSAI_ERR_INVALID;
  }
  if (core_lease_acquire(cell_id, cell->manifest.core_mask) != OSAI_OK) {
    kassert(model_arena_release(cell->manifest.model_arena_id) == OSAI_OK);
    cell->state = OSAI_AI_CELL_FAILED;
    ++g_transition_count;
    return OSAI_ERR_INVALID;
  }
  if (bind_nic_queue(cell_id, cell->manifest.nic_queue_id) != OSAI_OK ||
      bind_workspace(cell_id, cell->manifest.git_workspace_id) != OSAI_OK ||
      reserve_memory_arenas(cell) != OSAI_OK) {
    release_nic_queue(cell_id, cell->manifest.nic_queue_id);
    release_workspace(cell_id, cell->manifest.git_workspace_id);
    kassert(core_lease_release(cell_id) == OSAI_OK);
    kassert(model_arena_release(cell->manifest.model_arena_id) == OSAI_OK);
    cell->state = OSAI_AI_CELL_FAILED;
    ++g_transition_count;
    return OSAI_ERR_INVALID;
  }
  if (cpu_ai_runtime_bind_model(cell_id, cell->manifest.model_arena_id) != OSAI_OK) {
    destroy_cell_arenas(cell);
    release_nic_queue(cell_id, cell->manifest.nic_queue_id);
    release_workspace(cell_id, cell->manifest.git_workspace_id);
    kassert(core_lease_release(cell_id) == OSAI_OK);
    kassert(model_arena_release(cell->manifest.model_arena_id) == OSAI_OK);
    cell->state = OSAI_AI_CELL_FAILED;
    ++g_transition_count;
    return OSAI_ERR_INVALID;
  }

  cell->state = OSAI_AI_CELL_READY;
  ++g_transition_count;
  klog("ai-cell: %u ready shared_model=%s refs=%u kv=0x%lx source=0x%lx build=0x%lx log=0x%lx nic_queue=%u workspace=%u\n",
       cell_id, arena->name, arena->ref_count, cell->kv_cache_base,
       cell->source_index_base, cell->build_output_base, cell->log_base,
       cell->manifest.nic_queue_id, cell->manifest.git_workspace_id);
  return OSAI_OK;
}

osai_status_t ai_cell_start(uint32_t cell_id) {
  osai_ai_cell_t *cell = cell_by_id(cell_id);
  if (cell == 0 || cell->state != OSAI_AI_CELL_READY) {
    return OSAI_ERR_INVALID;
  }

  cell->state = OSAI_AI_CELL_RUNNING;
  ++g_transition_count;
  klog("ai-cell: %u running\n", cell_id);
  return OSAI_OK;
}

osai_status_t ai_cell_stop(uint32_t cell_id) {
  osai_ai_cell_t *cell = cell_by_id(cell_id);
  if (cell == 0 || cell->state != OSAI_AI_CELL_RUNNING) {
    return OSAI_ERR_INVALID;
  }

  kassert(cpu_ai_runtime_unbind_model(cell_id) == OSAI_OK);
  kassert(model_arena_release(cell->manifest.model_arena_id) == OSAI_OK);
  kassert(core_lease_release(cell_id) == OSAI_OK);
  release_nic_queue(cell_id, cell->manifest.nic_queue_id);
  release_workspace(cell_id, cell->manifest.git_workspace_id);
  destroy_cell_arenas(cell);
  cell->state = OSAI_AI_CELL_STOPPED;
  ++g_transition_count;
  klog("ai-cell: %u stopped\n", cell_id);
  return OSAI_OK;
}

uint64_t ai_cell_transition_count(void) {
  return g_transition_count;
}

void ai_cell_self_test(void) {
  ai_cell_runtime_init();
  osai_ai_cell_manifest_t invalid;
  invalid.name = "invalid";
  invalid.core_mask = 0;
  invalid.model_arena_id = 2;
  invalid.kv_cache_bytes = 4096;
  invalid.source_index_bytes = 4096;
  invalid.nic_queue_id = 1;
  invalid.git_workspace_id = 1;
  kassert(ai_cell_create(0, &invalid) == OSAI_ERR_INVALID);

  invalid.core_mask = 0x8;
  invalid.model_arena_id = 3;
  invalid.git_workspace_id = 1;
  kassert(ai_cell_create(3, &invalid) == OSAI_ERR_INVALID);

  invalid.model_arena_id = 2;
  invalid.git_workspace_id = 99;
  kassert(ai_cell_create(3, &invalid) == OSAI_ERR_INVALID);

  osai_ai_cell_manifest_t manifest;
  manifest.name = "codex-app-agent";
  manifest.core_mask = 0x2;
  manifest.model_arena_id = 2;
  manifest.kv_cache_bytes = 64 * 1024;
  manifest.source_index_bytes = 128 * 1024;
  manifest.nic_queue_id = 1;
  manifest.git_workspace_id = 1;
  kassert(ai_cell_create(0, &manifest) == OSAI_OK);
  kassert(ai_cell_prepare(0) == OSAI_OK);
  kassert(ai_cell_start(0) == OSAI_OK);
  char output[32];
  uint64_t out = 0;
  const uint8_t piece[] = {'A', 'B', 'C', 'D'};
  kassert(cpu_ai_runtime_decode_piece(0, piece, sizeof(piece), output,
                                     sizeof(output), &out) == OSAI_OK);
  kassert(cpu_ai_runtime_decode_count(0) == 1);
  kassert(output[0] == '1');
  kassert(output[1] == 'B');
  kassert(output[2] == '1');
  kassert(output[3] == 'F');
  kassert(output[4] == '2');
  kassert(output[5] == '3');
  kassert(output[6] == '2');
  kassert(output[7] == '7');
  kassert(output[8] == '\0');

  osai_ai_cell_manifest_t conflict;
  conflict.name = "conflict-agent";
  conflict.core_mask = 0x2;
  conflict.model_arena_id = 2;
  conflict.kv_cache_bytes = 64 * 1024;
  conflict.source_index_bytes = 128 * 1024;
  conflict.nic_queue_id = 2;
  conflict.git_workspace_id = 2;
  kassert(ai_cell_create(1, &conflict) == OSAI_OK);
  kassert(ai_cell_prepare(1) == OSAI_ERR_INVALID);

  osai_ai_cell_manifest_t nic_conflict;
  nic_conflict.name = "nic-conflict-agent";
  nic_conflict.core_mask = 0x4;
  nic_conflict.model_arena_id = 2;
  nic_conflict.kv_cache_bytes = 64 * 1024;
  nic_conflict.source_index_bytes = 128 * 1024;
  nic_conflict.nic_queue_id = 1;
  nic_conflict.git_workspace_id = 2;
  kassert(ai_cell_create(2, &nic_conflict) == OSAI_OK);
  kassert(ai_cell_prepare(2) == OSAI_ERR_INVALID);

  kassert(ai_cell_stop(0) == OSAI_OK);
  klog("ai-cell: lifecycle self-test passed\n");
}
