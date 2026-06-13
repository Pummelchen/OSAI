#include <osai/arena.h>
#include <osai/assert.h>
#include <osai/klog.h>
#include <osai/model_arena.h>

#define MAX_MODEL_ARENAS 4U

static osai_model_arena_t g_model_arenas[MAX_MODEL_ARENAS];

static int str_nonempty(const char *value) {
  return value != 0 && value[0] != '\0';
}

void model_arena_init(void) {
  for (uint32_t i = 0; i < MAX_MODEL_ARENAS; ++i) {
    g_model_arenas[i].arena_id = i;
    g_model_arenas[i].name = 0;
    g_model_arenas[i].base = 0;
    g_model_arenas[i].size = 0;
    g_model_arenas[i].ref_count = 0;
    g_model_arenas[i].read_only = 1;
  }

  klog("model-arena: registry initialized\n");
}

osai_status_t model_arena_register(uint32_t arena_id, const char *name,
                                   const void *base, uint64_t size) {
  if (arena_id >= MAX_MODEL_ARENAS || !str_nonempty(name) || base == 0 ||
      size == 0) {
    return OSAI_ERR_INVALID;
  }

  const osai_arena_t *arena = 0;
  if (arena_create(arena_id, OSAI_ARENA_MODEL_WEIGHTS, 0, name, size,
                   OSAI_ARENA_FLAG_READ_ONLY | OSAI_ARENA_FLAG_SHARED,
                   &arena) != OSAI_OK) {
    return OSAI_ERR_INVALID;
  }

  g_model_arenas[arena_id].name = name;
  g_model_arenas[arena_id].base = (const void *)(uintptr_t)arena->base;
  g_model_arenas[arena_id].size = arena->size;
  g_model_arenas[arena_id].ref_count = 0;
  g_model_arenas[arena_id].read_only = 1;
  klog("model-arena: registered id=%u name=%s base=0x%lx size=%lu\n",
       arena_id, name, (uint64_t)(uintptr_t)g_model_arenas[arena_id].base,
       g_model_arenas[arena_id].size);
  return OSAI_OK;
}

osai_status_t model_arena_acquire(uint32_t arena_id,
                                  const osai_model_arena_t **arena) {
  if (arena_id >= MAX_MODEL_ARENAS || g_model_arenas[arena_id].base == 0) {
    return OSAI_ERR_INVALID;
  }

  ++g_model_arenas[arena_id].ref_count;
  kassert(arena_acquire(arena_id, 0) == OSAI_OK);
  if (arena != 0) {
    *arena = &g_model_arenas[arena_id];
  }
  return OSAI_OK;
}

osai_status_t model_arena_release(uint32_t arena_id) {
  if (arena_id >= MAX_MODEL_ARENAS || g_model_arenas[arena_id].ref_count == 0) {
    return OSAI_ERR_INVALID;
  }

  --g_model_arenas[arena_id].ref_count;
  kassert(arena_release(arena_id) == OSAI_OK);
  return OSAI_OK;
}

void model_arena_self_test(void) {
  model_arena_init();
  static const uint8_t tiny_model_seed = 0xaa;
  kassert(model_arena_register(1, "tiny-shared-weights", &tiny_model_seed,
                               4096) == OSAI_OK);
  const osai_model_arena_t *a = 0;
  const osai_model_arena_t *b = 0;
  kassert(model_arena_acquire(1, &a) == OSAI_OK);
  kassert(model_arena_acquire(1, &b) == OSAI_OK);
  kassert(a == b);
  kassert(a->read_only != 0);
  kassert(a->ref_count == 2);
  kassert(model_arena_release(1) == OSAI_OK);
  kassert(model_arena_release(1) == OSAI_OK);
  klog("model-arena: shared read-only arena self-test passed\n");
}
