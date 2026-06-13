#include <osai/ai_cell.h>
#include <osai/assert.h>
#include <osai/core_lease.h>
#include <osai/klog.h>
#include <osai/model_arena.h>

#define MAX_AI_CELLS 4U

static osai_ai_cell_t g_ai_cells[MAX_AI_CELLS];

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
  if (manifest->git_workspace_id == 0) {
    return OSAI_ERR_INVALID;
  }

  const osai_model_arena_t *arena = 0;
  if (model_arena_acquire(manifest->model_arena_id, &arena) != OSAI_OK) {
    return OSAI_ERR_INVALID;
  }
  kassert(model_arena_release(manifest->model_arena_id) == OSAI_OK);
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
  for (uint32_t i = 0; i < MAX_AI_CELLS; ++i) {
    g_ai_cells[i].cell_id = i;
    g_ai_cells[i].state = OSAI_AI_CELL_EMPTY;
    g_ai_cells[i].lifecycle_generation = 0;
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
    return OSAI_ERR_INVALID;
  }
  if (core_lease_acquire(cell_id, cell->manifest.core_mask) != OSAI_OK) {
    kassert(model_arena_release(cell->manifest.model_arena_id) == OSAI_OK);
    cell->state = OSAI_AI_CELL_FAILED;
    return OSAI_ERR_INVALID;
  }

  cell->state = OSAI_AI_CELL_READY;
  klog("ai-cell: %u ready shared_model=%s refs=%u\n",
       cell_id, arena->name, arena->ref_count);
  return OSAI_OK;
}

osai_status_t ai_cell_start(uint32_t cell_id) {
  osai_ai_cell_t *cell = cell_by_id(cell_id);
  if (cell == 0 || cell->state != OSAI_AI_CELL_READY) {
    return OSAI_ERR_INVALID;
  }

  cell->state = OSAI_AI_CELL_RUNNING;
  klog("ai-cell: %u running\n", cell_id);
  return OSAI_OK;
}

osai_status_t ai_cell_stop(uint32_t cell_id) {
  osai_ai_cell_t *cell = cell_by_id(cell_id);
  if (cell == 0 || cell->state != OSAI_AI_CELL_RUNNING) {
    return OSAI_ERR_INVALID;
  }

  kassert(model_arena_release(cell->manifest.model_arena_id) == OSAI_OK);
  kassert(core_lease_release(cell_id) == OSAI_OK);
  cell->state = OSAI_AI_CELL_STOPPED;
  klog("ai-cell: %u stopped\n", cell_id);
  return OSAI_OK;
}

void ai_cell_self_test(void) {
  ai_cell_runtime_init();

  osai_ai_cell_manifest_t invalid;
  invalid.name = "invalid";
  invalid.core_mask = 0;
  invalid.model_arena_id = 1;
  invalid.kv_cache_bytes = 4096;
  invalid.source_index_bytes = 4096;
  invalid.nic_queue_id = 1;
  invalid.git_workspace_id = 1;
  kassert(ai_cell_create(0, &invalid) == OSAI_ERR_INVALID);

  osai_ai_cell_manifest_t manifest;
  manifest.name = "codex-app-agent";
  manifest.core_mask = 0x2;
  manifest.model_arena_id = 1;
  manifest.kv_cache_bytes = 64 * 1024;
  manifest.source_index_bytes = 128 * 1024;
  manifest.nic_queue_id = 1;
  manifest.git_workspace_id = 1;
  kassert(ai_cell_create(0, &manifest) == OSAI_OK);
  kassert(ai_cell_prepare(0) == OSAI_OK);
  kassert(ai_cell_start(0) == OSAI_OK);

  osai_ai_cell_manifest_t conflict;
  conflict.name = "conflict-agent";
  conflict.core_mask = 0x2;
  conflict.model_arena_id = 1;
  conflict.kv_cache_bytes = 64 * 1024;
  conflict.source_index_bytes = 128 * 1024;
  conflict.nic_queue_id = 2;
  conflict.git_workspace_id = 2;
  kassert(ai_cell_create(1, &conflict) == OSAI_OK);
  kassert(ai_cell_prepare(1) == OSAI_ERR_INVALID);

  kassert(ai_cell_stop(0) == OSAI_OK);
  klog("ai-cell: lifecycle self-test passed\n");
}
