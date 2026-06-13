#ifndef OSAI_SOURCE_INDEX_H
#define OSAI_SOURCE_INDEX_H

#include <osai/status.h>
#include <osai/types.h>

#define OSAI_SOURCE_INDEX_PATH_MAX 64U
#define OSAI_SOURCE_INDEX_SYMBOL_NAME_MAX 32U

typedef enum osai_source_index_state {
  OSAI_SOURCE_INDEX_EMPTY = 0,
  OSAI_SOURCE_INDEX_CREATED = 1,
} osai_source_index_state_t;

typedef enum osai_source_index_language {
  OSAI_SOURCE_INDEX_LANG_UNKNOWN = 0,
  OSAI_SOURCE_INDEX_LANG_C = 1,
  OSAI_SOURCE_INDEX_LANG_CPP = 2,
  OSAI_SOURCE_INDEX_LANG_ASM = 3,
  OSAI_SOURCE_INDEX_LANG_RUST = 4,
} osai_source_index_language_t;

typedef enum osai_source_index_symbol_kind {
  OSAI_SOURCE_INDEX_SYMBOL_UNKNOWN = 0,
  OSAI_SOURCE_INDEX_SYMBOL_FUNCTION = 1,
  OSAI_SOURCE_INDEX_SYMBOL_TYPE = 2,
  OSAI_SOURCE_INDEX_SYMBOL_VARIABLE = 3,
} osai_source_index_symbol_kind_t;

typedef struct osai_source_index_manifest {
  uint32_t index_id;
  uint32_t cell_id;
  const char *repo_path;
  const char *revision;
  uint64_t source_arena_bytes;
} osai_source_index_manifest_t;

void source_index_runtime_init(void);
osai_status_t source_index_create(const osai_source_index_manifest_t *manifest);
osai_status_t source_index_add_file(uint32_t index_id, const char *path,
                                   uint32_t language, uint64_t bytes,
                                   uint64_t content_hash);
osai_status_t source_index_add_symbol(uint32_t index_id, uint32_t file_id,
                                     const char *name, uint32_t kind,
                                     uint32_t line);
osai_status_t source_index_incremental_update(uint32_t index_id,
                                            const char *revision);
uint64_t source_index_active_count(void);
uint64_t source_index_total_file_records(void);
uint64_t source_index_total_symbol_records(void);
uint64_t source_index_total_updates(void);
void source_index_self_test(void);

#endif
