#ifndef XAIOS_ARENA_H
#define XAIOS_ARENA_H

#include <xaios/status.h>
#include <xaios/types.h>

#define XAIOS_ARENA_NAME_MAX 32U

#define XAIOS_ARENA_FLAG_READ_ONLY UINT32_C(1)
#define XAIOS_ARENA_FLAG_SHARED UINT32_C(1 << 1)
#define XAIOS_ARENA_FLAG_PREFaultED UINT32_C(1 << 2)
#define XAIOS_ARENA_FLAG_USER_VISIBLE UINT32_C(1 << 3)

typedef enum xaios_arena_kind {
  XAIOS_ARENA_MODEL_WEIGHTS = 1,
  XAIOS_ARENA_KV_CACHE = 2,
  XAIOS_ARENA_SOURCE_INDEX = 3,
  XAIOS_ARENA_BUILD_OUTPUT = 4,
  XAIOS_ARENA_LOG = 5,
  XAIOS_ARENA_TELEMETRY = 6,
} xaios_arena_kind_t;

typedef enum xaios_arena_state {
  XAIOS_ARENA_EMPTY = 0,
  XAIOS_ARENA_READY = 1,
  XAIOS_ARENA_DESTROYED = 2,
} xaios_arena_state_t;

typedef struct xaios_arena {
  uint32_t arena_id;
  xaios_arena_kind_t kind;
  xaios_arena_state_t state;
  uint32_t owner_id;
  char name[XAIOS_ARENA_NAME_MAX];
  uint64_t base;
  uint64_t size;
  uint64_t page_count;
  uint32_t flags;
  uint32_t ref_count;
  uint64_t fault_count;
  void **pages;
} xaios_arena_t;

void arena_manager_init(void);
xaios_status_t arena_create(uint32_t arena_id, xaios_arena_kind_t kind,
                           uint32_t owner_id, const char *name, uint64_t size,
                           uint32_t flags, const xaios_arena_t **arena);
xaios_status_t arena_acquire(uint32_t arena_id, const xaios_arena_t **arena);
xaios_status_t arena_release(uint32_t arena_id);
xaios_status_t arena_destroy(uint32_t arena_id);
xaios_status_t arena_record_fault(uint32_t arena_id);
uint64_t arena_committed_pages(void);
uint64_t arena_active_count(void);
void arena_self_test(void);

#endif
