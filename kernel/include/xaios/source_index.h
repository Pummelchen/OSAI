#ifndef XAIOS_SOURCE_INDEX_H
#define XAIOS_SOURCE_INDEX_H

#include <xaios/status.h>
#include <xaios/types.h>

#define XAIOS_SOURCE_INDEX_PATH_MAX 64U
#define XAIOS_SOURCE_INDEX_SYMBOL_NAME_MAX 32U

typedef enum xaios_source_index_state {
  XAIOS_SOURCE_INDEX_EMPTY = 0,
  XAIOS_SOURCE_INDEX_CREATED = 1,
} xaios_source_index_state_t;

typedef enum xaios_source_index_language {
  XAIOS_SOURCE_INDEX_LANG_UNKNOWN = 0,
  XAIOS_SOURCE_INDEX_LANG_C = 1,
  XAIOS_SOURCE_INDEX_LANG_CPP = 2,
  XAIOS_SOURCE_INDEX_LANG_ASM = 3,
  XAIOS_SOURCE_INDEX_LANG_RUST = 4,
} xaios_source_index_language_t;

typedef enum xaios_source_index_symbol_kind {
  XAIOS_SOURCE_INDEX_SYMBOL_UNKNOWN = 0,
  XAIOS_SOURCE_INDEX_SYMBOL_FUNCTION = 1,
  XAIOS_SOURCE_INDEX_SYMBOL_TYPE = 2,
  XAIOS_SOURCE_INDEX_SYMBOL_VARIABLE = 3,
} xaios_source_index_symbol_kind_t;

typedef struct xaios_source_index_manifest {
  uint32_t index_id;
  uint32_t cell_id;
  const char *repo_path;
  const char *revision;
  uint64_t source_arena_bytes;
} xaios_source_index_manifest_t;

void source_index_runtime_init(void);
xaios_status_t source_index_create(const xaios_source_index_manifest_t *manifest);
xaios_status_t source_index_add_file(uint32_t index_id, const char *path,
                                   uint32_t language, uint64_t bytes,
                                   uint64_t content_hash);
xaios_status_t source_index_add_symbol(uint32_t index_id, uint32_t file_id,
                                     const char *name, uint32_t kind,
                                     uint32_t line);
xaios_status_t source_index_incremental_update(uint32_t index_id,
                                            const char *revision);
xaios_status_t source_index_scan_source(uint32_t index_id, uint32_t file_id,
                                       const char *source,
                                       uint64_t source_bytes);
uint64_t source_index_query_symbol_count(uint32_t index_id, uint32_t file_id,
                                         uint32_t kind);
uint64_t source_index_scan_count(void);
uint64_t source_index_active_count(void);
uint64_t source_index_total_file_records(void);
uint64_t source_index_total_symbol_records(void);
uint64_t source_index_total_updates(void);
void source_index_self_test(void);

#endif
