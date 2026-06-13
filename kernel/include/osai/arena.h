#ifndef OSAI_ARENA_H
#define OSAI_ARENA_H

#include <osai/status.h>
#include <osai/types.h>

#define OSAI_ARENA_NAME_MAX 32U

#define OSAI_ARENA_FLAG_READ_ONLY UINT32_C(1)
#define OSAI_ARENA_FLAG_SHARED UINT32_C(1 << 1)
#define OSAI_ARENA_FLAG_PREFaultED UINT32_C(1 << 2)
#define OSAI_ARENA_FLAG_USER_VISIBLE UINT32_C(1 << 3)

typedef enum osai_arena_kind {
  OSAI_ARENA_MODEL_WEIGHTS = 1,
  OSAI_ARENA_KV_CACHE = 2,
  OSAI_ARENA_SOURCE_INDEX = 3,
  OSAI_ARENA_BUILD_OUTPUT = 4,
  OSAI_ARENA_LOG = 5,
  OSAI_ARENA_TELEMETRY = 6,
} osai_arena_kind_t;

typedef enum osai_arena_state {
  OSAI_ARENA_EMPTY = 0,
  OSAI_ARENA_READY = 1,
  OSAI_ARENA_DESTROYED = 2,
} osai_arena_state_t;

typedef struct osai_arena {
  uint32_t arena_id;
  osai_arena_kind_t kind;
  osai_arena_state_t state;
  uint32_t owner_id;
  char name[OSAI_ARENA_NAME_MAX];
  uint64_t base;
  uint64_t size;
  uint64_t page_count;
  uint32_t flags;
  uint32_t ref_count;
  uint64_t fault_count;
  void **pages;
} osai_arena_t;

void arena_manager_init(void);
osai_status_t arena_create(uint32_t arena_id, osai_arena_kind_t kind,
                           uint32_t owner_id, const char *name, uint64_t size,
                           uint32_t flags, const osai_arena_t **arena);
osai_status_t arena_acquire(uint32_t arena_id, const osai_arena_t **arena);
osai_status_t arena_release(uint32_t arena_id);
osai_status_t arena_destroy(uint32_t arena_id);
osai_status_t arena_record_fault(uint32_t arena_id);
uint64_t arena_committed_pages(void);
uint64_t arena_active_count(void);
void arena_self_test(void);

#endif
