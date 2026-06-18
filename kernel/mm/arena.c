#include <xaios/arena.h>
#include <xaios/assert.h>
#include <xaios/kheap.h>
#include <xaios/klog.h>
#include <xaios/pmm.h>
#include <xaios/vmm.h>

#define PAGE_SIZE UINT64_C(4096)
#define MAX_ARENAS 32U
#define ARENA_VA_BASE UINT64_C(0x4c000000)
#define ARENA_VA_STRIDE UINT64_C(0x00200000)
#define ARENA_VA_LIMIT UINT64_C(0x50000000)

static xaios_arena_t g_arenas[MAX_ARENAS];
static uint64_t g_committed_pages;
static uint64_t g_active_count;

static uint64_t align_up(uint64_t value, uint64_t align) {
  return (value + align - 1) & ~(align - 1);
}

static int str_nonempty(const char *value) {
  return value != 0 && value[0] != '\0';
}

static void copy_name(char *dst, const char *src) {
  uint32_t i = 0;
  for (; i + 1U < XAIOS_ARENA_NAME_MAX && src[i] != '\0'; ++i) {
    dst[i] = src[i];
  }
  dst[i] = '\0';
}

static uint32_t vmm_flags_from_arena(uint32_t arena_flags) {
  uint32_t flags = XAIOS_VMM_PRESENT;
  if ((arena_flags & XAIOS_ARENA_FLAG_READ_ONLY) == 0) {
    flags |= XAIOS_VMM_WRITABLE;
  }
  if ((arena_flags & XAIOS_ARENA_FLAG_USER_VISIBLE) != 0) {
    flags |= XAIOS_VMM_USER;
  }
  return flags;
}

void arena_manager_init(void) {
  for (uint32_t i = 0; i < MAX_ARENAS; ++i) {
    g_arenas[i].arena_id = i;
    g_arenas[i].kind = 0;
    g_arenas[i].state = XAIOS_ARENA_EMPTY;
    g_arenas[i].owner_id = 0;
    g_arenas[i].name[0] = '\0';
    g_arenas[i].base = 0;
    g_arenas[i].size = 0;
    g_arenas[i].page_count = 0;
    g_arenas[i].flags = 0;
    g_arenas[i].ref_count = 0;
    g_arenas[i].fault_count = 0;
    g_arenas[i].pages = 0;
  }
  g_committed_pages = 0;
  g_active_count = 0;
  klog("arena: manager initialized\n");
}

static xaios_status_t arena_validate_create(uint32_t arena_id,
                                           xaios_arena_kind_t kind,
                                           const char *name, uint64_t size) {
  if (arena_id >= MAX_ARENAS || kind == 0 || !str_nonempty(name) ||
      size == 0 || g_arenas[arena_id].state == XAIOS_ARENA_READY) {
    return XAIOS_ERR_INVALID;
  }
  uint64_t base = ARENA_VA_BASE + ((uint64_t)arena_id * ARENA_VA_STRIDE);
  if (base + align_up(size, PAGE_SIZE) > ARENA_VA_LIMIT) {
    return XAIOS_ERR_NO_MEMORY;
  }
  return XAIOS_OK;
}

static void arena_zero(void *buffer, uint64_t size) {
  uint8_t *bytes = (uint8_t *)buffer;
  for (uint64_t i = 0; i < size; ++i) {
    bytes[i] = 0;
  }
}

static void arena_unmap_pages(xaios_arena_t *arena) {
  if (arena->pages == 0) {
    return;
  }

  for (uint64_t i = 0; i < arena->page_count; ++i) {
    uint64_t va = arena->base + (i * PAGE_SIZE);
    kassert(vmm_unmap_page(va) == XAIOS_OK);
    if (arena->pages[i] != 0) {
      pmm_free_page(arena->pages[i]);
      --g_committed_pages;
      arena->pages[i] = 0;
    }
  }
}

xaios_status_t arena_create(uint32_t arena_id, xaios_arena_kind_t kind,
                           uint32_t owner_id, const char *name, uint64_t size,
                           uint32_t flags, const xaios_arena_t **arena) {
  if (arena_validate_create(arena_id, kind, name, size) != XAIOS_OK) {
    return XAIOS_ERR_INVALID;
  }

  xaios_arena_t *entry = &g_arenas[arena_id];
  uint64_t mapped_size = align_up(size, PAGE_SIZE);
  uint64_t page_count = mapped_size / PAGE_SIZE;
  void **pages = (void **)kheap_calloc(sizeof(void *) * page_count, 16);
  if (pages == 0) {
    return XAIOS_ERR_NO_MEMORY;
  }

  entry->kind = kind;
  entry->owner_id = owner_id;
  copy_name(entry->name, name);
  entry->base = ARENA_VA_BASE + ((uint64_t)arena_id * ARENA_VA_STRIDE);
  entry->size = size;
  entry->page_count = page_count;
  entry->flags = flags | XAIOS_ARENA_FLAG_PREFaultED;
  entry->ref_count = 0;
  entry->fault_count = 0;
  entry->pages = pages;

  for (uint64_t i = 0; i < page_count; ++i) {
    pages[i] = pmm_alloc_page();
    if (pages[i] == 0) {
      arena_unmap_pages(entry);
      entry->state = XAIOS_ARENA_EMPTY;
      return XAIOS_ERR_NO_MEMORY;
    }
    arena_zero(pages[i], PAGE_SIZE);
    if (vmm_map_page(entry->base + (i * PAGE_SIZE),
                     (uint64_t)(uintptr_t)pages[i],
                     vmm_flags_from_arena(entry->flags)) != XAIOS_OK) {
      arena_unmap_pages(entry);
      entry->state = XAIOS_ARENA_EMPTY;
      return XAIOS_ERR_INVALID;
    }
    ++g_committed_pages;
  }

  entry->state = XAIOS_ARENA_READY;
  ++g_active_count;
  if (arena != 0) {
    *arena = entry;
  }
  klog("arena: created id=%u kind=%u owner=%u name=%s base=0x%lx size=%lu pages=%lu flags=0x%x\n",
       arena_id, kind, owner_id, entry->name, entry->base, entry->size,
       entry->page_count, entry->flags);
  return XAIOS_OK;
}

xaios_status_t arena_acquire(uint32_t arena_id, const xaios_arena_t **arena) {
  if (arena_id >= MAX_ARENAS || g_arenas[arena_id].state != XAIOS_ARENA_READY) {
    return XAIOS_ERR_INVALID;
  }
  ++g_arenas[arena_id].ref_count;
  if (arena != 0) {
    *arena = &g_arenas[arena_id];
  }
  return XAIOS_OK;
}

xaios_status_t arena_release(uint32_t arena_id) {
  if (arena_id >= MAX_ARENAS || g_arenas[arena_id].state != XAIOS_ARENA_READY ||
      g_arenas[arena_id].ref_count == 0) {
    return XAIOS_ERR_INVALID;
  }
  --g_arenas[arena_id].ref_count;
  return XAIOS_OK;
}

xaios_status_t arena_destroy(uint32_t arena_id) {
  if (arena_id >= MAX_ARENAS || g_arenas[arena_id].state != XAIOS_ARENA_READY ||
      g_arenas[arena_id].ref_count != 0) {
    return XAIOS_ERR_INVALID;
  }

  xaios_arena_t *entry = &g_arenas[arena_id];
  arena_unmap_pages(entry);
  entry->state = XAIOS_ARENA_DESTROYED;
  entry->base = 0;
  entry->size = 0;
  entry->page_count = 0;
  entry->flags = 0;
  entry->pages = 0;
  --g_active_count;
  klog("arena: destroyed id=%u\n", arena_id);
  return XAIOS_OK;
}

xaios_status_t arena_record_fault(uint32_t arena_id) {
  if (arena_id >= MAX_ARENAS || g_arenas[arena_id].state != XAIOS_ARENA_READY) {
    return XAIOS_ERR_INVALID;
  }
  ++g_arenas[arena_id].fault_count;
  return XAIOS_OK;
}

uint64_t arena_committed_pages(void) {
  return g_committed_pages;
}

uint64_t arena_active_count(void) {
  return g_active_count;
}

void arena_self_test(void) {
  const xaios_arena_t *model = 0;
  const xaios_arena_t *kv = 0;
  const xaios_arena_t *build = 0;
  const xaios_arena_t *log = 0;
  const xaios_arena_t *telemetry = 0;

  kassert(arena_create(20, XAIOS_ARENA_MODEL_WEIGHTS, 0, "arena-test-model",
                       4096, XAIOS_ARENA_FLAG_READ_ONLY |
                                 XAIOS_ARENA_FLAG_SHARED,
                       &model) == XAIOS_OK);
  kassert(arena_create(21, XAIOS_ARENA_KV_CACHE, 7, "arena-test-kv", 8192, 0,
                       &kv) == XAIOS_OK);
  kassert(arena_create(22, XAIOS_ARENA_BUILD_OUTPUT, 7, "arena-test-build",
                       4096, 0, &build) == XAIOS_OK);
  kassert(arena_create(23, XAIOS_ARENA_LOG, 7, "arena-test-log", 4096, 0,
                       &log) == XAIOS_OK);
  kassert(arena_create(24, XAIOS_ARENA_TELEMETRY, 7, "arena-test-telemetry",
                       4096, 0, &telemetry) == XAIOS_OK);
  kassert(model != 0 && kv != 0 && build != 0 && log != 0 && telemetry != 0);
  kassert((model->flags & XAIOS_ARENA_FLAG_READ_ONLY) != 0);
  kassert((model->flags & XAIOS_ARENA_FLAG_PREFaultED) != 0);
  kassert(model->page_count == 1);
  kassert(kv->page_count == 2);

  uint64_t physical = 0;
  uint32_t vmm_flags = 0;
  kassert(vmm_translate(model->base, &physical, &vmm_flags) == XAIOS_OK);
  kassert((vmm_flags & XAIOS_VMM_WRITABLE) == 0);
  kassert(vmm_translate(kv->base, &physical, &vmm_flags) == XAIOS_OK);
  kassert((vmm_flags & XAIOS_VMM_WRITABLE) != 0);

  kassert(arena_acquire(20, &model) == XAIOS_OK);
  kassert(arena_destroy(20) == XAIOS_ERR_INVALID);
  kassert(arena_release(20) == XAIOS_OK);
  kassert(arena_record_fault(21) == XAIOS_OK);
  kassert(arena_destroy(20) == XAIOS_OK);
  kassert(arena_destroy(21) == XAIOS_OK);
  kassert(arena_destroy(22) == XAIOS_OK);
  kassert(arena_destroy(23) == XAIOS_OK);
  kassert(arena_destroy(24) == XAIOS_OK);
  klog("arena: self-test passed active=%lu committed_pages=%lu\n",
       arena_active_count(), arena_committed_pages());
}
