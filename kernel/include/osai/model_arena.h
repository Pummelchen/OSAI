#ifndef OSAI_MODEL_ARENA_H
#define OSAI_MODEL_ARENA_H

#include <osai/status.h>
#include <osai/types.h>

#define OSAI_MODEL_ARENA_NAME_MAX 32U

typedef struct osai_model_arena {
  uint32_t arena_id;
  const char *name;
  const void *base;
  uint64_t size;
  uint32_t ref_count;
  uint32_t read_only;
} osai_model_arena_t;

void model_arena_init(void);
osai_status_t model_arena_register(uint32_t arena_id, const char *name,
                                   const void *base, uint64_t size);
osai_status_t model_arena_unregister(uint32_t arena_id);
osai_status_t model_arena_acquire(uint32_t arena_id,
                                  const osai_model_arena_t **arena);
osai_status_t model_arena_release(uint32_t arena_id);
void model_arena_self_test(void);

#endif
