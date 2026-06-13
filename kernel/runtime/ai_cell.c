#include <osai/ai_cell.h>
#include <osai/assert.h>
#include <osai/core_lease.h>
#include <osai/kheap.h>
#include <osai/klog.h>
#include <osai/model_arena.h>
#include <osai/pmm.h>
#include <osai/vmm.h>

#define MAX_AI_CELLS 4U
#define PAGE_SIZE UINT64_C(4096)
#define MAX_NIC_QUEUES 4U
#define MAX_WORKSPACES 2U
#define CELL_ARENA_BASE UINT64_C(0x49000000)
#define CELL_ARENA_STRIDE UINT64_C(0x00400000)

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

static uint64_t align_up(uint64_t value, uint64_t align) {
  return (value + align - 1) & ~(align - 1);
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

static void release_memory_reservation(osai_ai_cell_t *cell) {
  if (cell->reserved_pages == 0) {
    return;
  }
  uint64_t base = CELL_ARENA_BASE + ((uint64_t)cell->cell_id * CELL_ARENA_STRIDE);
  for (uint64_t i = 0; i < cell->reserved_page_count; ++i) {
    uint64_t va = base + (i * PAGE_SIZE);
    kassert(vmm_unmap_page(va) == OSAI_OK);
    if (cell->reserved_pages[i] != 0) {
      pmm_free_page(cell->reserved_pages[i]);
    }
  }
  cell->reserved_pages = 0;
  cell->reserved_page_count = 0;
  cell->kv_cache_base = 0;
  cell->source_index_base = 0;
}

static osai_status_t reserve_memory_arenas(osai_ai_cell_t *cell) {
  uint64_t kv_bytes = align_up(cell->manifest.kv_cache_bytes, PAGE_SIZE);
  uint64_t source_bytes = align_up(cell->manifest.source_index_bytes, PAGE_SIZE);
  uint64_t page_count = (kv_bytes + source_bytes) / PAGE_SIZE;
  void **pages = (void **)kheap_calloc(sizeof(void *) * page_count, 16);
  if (pages == 0) {
    return OSAI_ERR_NO_MEMORY;
  }

  uint64_t base = CELL_ARENA_BASE + ((uint64_t)cell->cell_id * CELL_ARENA_STRIDE);
  cell->reserved_pages = pages;
  cell->reserved_page_count = page_count;
  cell->kv_cache_base = base;
  cell->source_index_base = base + kv_bytes;

  for (uint64_t i = 0; i < page_count; ++i) {
    pages[i] = pmm_alloc_page();
    if (pages[i] == 0 ||
        vmm_map_page(base + (i * PAGE_SIZE), (uint64_t)(uintptr_t)pages[i],
                     OSAI_VMM_PRESENT | OSAI_VMM_WRITABLE) != OSAI_OK) {
      release_memory_reservation(cell);
      return OSAI_ERR_NO_MEMORY;
    }
  }

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
    g_ai_cells[i].reserved_pages = 0;
    g_ai_cells[i].reserved_page_count = 0;
    g_ai_cells[i].kv_cache_base = 0;
    g_ai_cells[i].source_index_base = 0;
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

  cell->state = OSAI_AI_CELL_READY;
  ++g_transition_count;
  klog("ai-cell: %u ready shared_model=%s refs=%u kv=0x%lx source=0x%lx pages=%lu nic_queue=%u workspace=%u\n",
       cell_id, arena->name, arena->ref_count, cell->kv_cache_base,
       cell->source_index_base, cell->reserved_page_count,
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

  kassert(model_arena_release(cell->manifest.model_arena_id) == OSAI_OK);
  kassert(core_lease_release(cell_id) == OSAI_OK);
  release_nic_queue(cell_id, cell->manifest.nic_queue_id);
  release_workspace(cell_id, cell->manifest.git_workspace_id);
  release_memory_reservation(cell);
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
  invalid.model_arena_id = 1;
  invalid.kv_cache_bytes = 4096;
  invalid.source_index_bytes = 4096;
  invalid.nic_queue_id = 1;
  invalid.git_workspace_id = 1;
  kassert(ai_cell_create(0, &invalid) == OSAI_ERR_INVALID);

  invalid.core_mask = 0x8;
  invalid.model_arena_id = 3;
  invalid.git_workspace_id = 1;
  kassert(ai_cell_create(3, &invalid) == OSAI_ERR_INVALID);

  invalid.model_arena_id = 1;
  invalid.git_workspace_id = 99;
  kassert(ai_cell_create(3, &invalid) == OSAI_ERR_INVALID);

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

  osai_ai_cell_manifest_t nic_conflict;
  nic_conflict.name = "nic-conflict-agent";
  nic_conflict.core_mask = 0x4;
  nic_conflict.model_arena_id = 1;
  nic_conflict.kv_cache_bytes = 64 * 1024;
  nic_conflict.source_index_bytes = 128 * 1024;
  nic_conflict.nic_queue_id = 1;
  nic_conflict.git_workspace_id = 2;
  kassert(ai_cell_create(2, &nic_conflict) == OSAI_OK);
  kassert(ai_cell_prepare(2) == OSAI_ERR_INVALID);

  kassert(ai_cell_stop(0) == OSAI_OK);
  klog("ai-cell: lifecycle self-test passed\n");
}
