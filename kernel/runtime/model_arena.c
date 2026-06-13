#include <osai/assert.h>
#include <osai/klog.h>
#include <osai/model_arena.h>

#define MAX_MODEL_ARENAS 4U

static uint8_t g_tiny_model_weights[4096] __attribute__((aligned(4096)));
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

  for (uint32_t i = 0; i < sizeof(g_tiny_model_weights); ++i) {
    g_tiny_model_weights[i] = (uint8_t)(i & 0xffU);
  }

  klog("model-arena: registry initialized\n");
}

osai_status_t model_arena_register(uint32_t arena_id, const char *name,
                                   const void *base, uint64_t size) {
  if (arena_id >= MAX_MODEL_ARENAS || !str_nonempty(name) || base == 0 ||
      size == 0) {
    return OSAI_ERR_INVALID;
  }

  g_model_arenas[arena_id].name = name;
  g_model_arenas[arena_id].base = base;
  g_model_arenas[arena_id].size = size;
  g_model_arenas[arena_id].ref_count = 0;
  g_model_arenas[arena_id].read_only = 1;
  klog("model-arena: registered id=%u name=%s base=0x%lx size=%lu\n",
       arena_id, name, (uint64_t)(uintptr_t)base, size);
  return OSAI_OK;
}

osai_status_t model_arena_acquire(uint32_t arena_id,
                                  const osai_model_arena_t **arena) {
  if (arena_id >= MAX_MODEL_ARENAS || g_model_arenas[arena_id].base == 0) {
    return OSAI_ERR_INVALID;
  }

  ++g_model_arenas[arena_id].ref_count;
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
  return OSAI_OK;
}

void model_arena_self_test(void) {
  model_arena_init();
  kassert(model_arena_register(1, "tiny-shared-weights", g_tiny_model_weights,
                               sizeof(g_tiny_model_weights)) == OSAI_OK);
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
