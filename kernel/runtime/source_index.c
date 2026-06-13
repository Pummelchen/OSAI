#include <osai/arena.h>
#include <osai/assert.h>
#include <osai/klog.h>
#include <osai/source_index.h>

#define MAX_SOURCE_INDEXES 3U
#define SOURCE_INDEX_ARENA_BASE 29U
#define SOURCE_INDEX_MAX_FILES 8U
#define SOURCE_INDEX_MAX_SYMBOLS 16U

#define SOURCE_INDEX_REVISION_MAX 32U

typedef enum {
  OSAI_SOURCE_INDEX_SLOT_EMPTY = 0,
  OSAI_SOURCE_INDEX_SLOT_USED = 1,
} osai_source_index_record_state_t;

typedef struct {
  osai_source_index_record_state_t state;
  uint32_t language;
  uint32_t file_id;
  uint64_t bytes;
  uint64_t content_hash;
  char path[OSAI_SOURCE_INDEX_PATH_MAX];
} osai_source_index_file_record_t;

typedef struct {
  osai_source_index_record_state_t state;
  uint32_t file_id;
  uint32_t kind;
  uint32_t line;
  char name[OSAI_SOURCE_INDEX_SYMBOL_NAME_MAX];
} osai_source_index_symbol_record_t;

typedef struct {
  uint32_t revision_len;
  uint32_t file_count;
  uint32_t symbol_count;
  uint32_t update_count;
  char revision[SOURCE_INDEX_REVISION_MAX];
  osai_source_index_file_record_t files[SOURCE_INDEX_MAX_FILES];
  osai_source_index_symbol_record_t symbols[SOURCE_INDEX_MAX_SYMBOLS];
} osai_source_index_storage_t;

typedef struct {
  osai_source_index_state_t state;
  osai_source_index_manifest_t manifest;
  uint32_t arena_id;
  uint64_t arena_base;
} osai_source_index_t;

static osai_source_index_t g_source_indexes[MAX_SOURCE_INDEXES];
static uint64_t g_active_count;
static uint64_t g_total_files;
static uint64_t g_total_symbols;
static uint64_t g_total_updates;

static int str_nonempty(const char *value) {
  return value != 0 && value[0] != '\0';
}

static int str_prefix(const char *value, const char *prefix) {
  if (!str_nonempty(value) || !str_nonempty(prefix)) {
    return 0;
  }
  while (*prefix != '\0') {
    if (*value != *prefix) {
      return 0;
    }
    ++value;
    ++prefix;
  }
  return 1;
}

static uint32_t string_length(const char *value) {
  uint32_t len = 0;
  while (value[len] != '\0') {
    ++len;
  }
  return len;
}

static int copy_if_fits(char *dst, uint32_t dst_size, const char *src,
                        uint32_t *out_len) {
  uint32_t len = 0;
  if (dst_size == 0) {
    return 0;
  }
  while (src[len] != '\0' && (len + 1U) < dst_size) {
    dst[len] = src[len];
    ++len;
  }
  dst[len] = '\0';
  if (src[len] != '\0') {
    return 0;
  }
  if (out_len != 0) {
    *out_len = len;
  }
  return 1;
}

static int paths_equal(const char *lhs, const char *rhs) {
  uint32_t i = 0;
  while (lhs[i] != '\0' && rhs[i] != '\0') {
    if (lhs[i] != rhs[i]) {
      return 0;
    }
    ++i;
  }
  return lhs[i] == rhs[i];
}

static osai_source_index_t *lookup_index(uint32_t index_id) {
  if (index_id >= MAX_SOURCE_INDEXES) {
    return 0;
  }
  return &g_source_indexes[index_id];
}

static osai_source_index_storage_t *storage_for(uint32_t index_id) {
  osai_source_index_t *index = lookup_index(index_id);
  if (index == 0 || index->state == OSAI_SOURCE_INDEX_EMPTY ||
      index->arena_base == 0) {
    return 0;
  }
  return (osai_source_index_storage_t *)(uintptr_t)index->arena_base;
}

static void zero_storage(osai_source_index_storage_t *storage) {
  uint8_t *bytes = (uint8_t *)storage;
  for (uint32_t i = 0; i < sizeof(osai_source_index_storage_t); ++i) {
    bytes[i] = 0;
  }
}

static osai_status_t validate_manifest(
    const osai_source_index_manifest_t *manifest) {
  if (manifest == 0 || manifest->index_id >= MAX_SOURCE_INDEXES ||
      !str_prefix(manifest->repo_path, "/repo/") ||
      !str_nonempty(manifest->revision) ||
      string_length(manifest->revision) >= SOURCE_INDEX_REVISION_MAX ||
      manifest->source_arena_bytes < sizeof(osai_source_index_storage_t)) {
    return OSAI_ERR_INVALID;
  }
  return OSAI_OK;
}

void source_index_runtime_init(void) {
  for (uint32_t i = 0; i < MAX_SOURCE_INDEXES; ++i) {
    g_source_indexes[i].state = OSAI_SOURCE_INDEX_EMPTY;
    g_source_indexes[i].manifest.index_id = 0;
    g_source_indexes[i].manifest.cell_id = 0;
    g_source_indexes[i].manifest.repo_path = 0;
    g_source_indexes[i].manifest.revision = 0;
    g_source_indexes[i].manifest.source_arena_bytes = 0;
    g_source_indexes[i].arena_id = 0;
    g_source_indexes[i].arena_base = 0;
  }
  g_active_count = 0;
  g_total_files = 0;
  g_total_symbols = 0;
  g_total_updates = 0;
  klog("source-index: runtime initialized\n");
}

osai_status_t source_index_create(const osai_source_index_manifest_t *manifest) {
  if (validate_manifest(manifest) != OSAI_OK) {
    return OSAI_ERR_INVALID;
  }

  osai_source_index_t *index = lookup_index(manifest->index_id);
  if (index->state != OSAI_SOURCE_INDEX_EMPTY) {
    return OSAI_ERR_BUSY;
  }

  uint32_t arena_id = SOURCE_INDEX_ARENA_BASE + manifest->index_id;
  const osai_arena_t *arena = 0;
  if (arena_create(arena_id, OSAI_ARENA_SOURCE_INDEX, manifest->index_id,
                   "source-index", manifest->source_arena_bytes, 0,
                   &arena) != OSAI_OK) {
    return OSAI_ERR_NO_MEMORY;
  }

  index->state = OSAI_SOURCE_INDEX_CREATED;
  index->manifest.index_id = manifest->index_id;
  index->manifest.cell_id = manifest->cell_id;
  index->manifest.repo_path = manifest->repo_path;
  index->manifest.revision = manifest->revision;
  index->manifest.source_arena_bytes = manifest->source_arena_bytes;
  index->arena_id = arena_id;
  index->arena_base = arena->base;

  osai_source_index_storage_t *storage = storage_for(manifest->index_id);
  if (storage == 0) {
    kassert(arena_destroy(arena_id) == OSAI_OK);
    index->state = OSAI_SOURCE_INDEX_EMPTY;
    index->arena_base = 0;
    return OSAI_ERR_INVALID;
  }

  zero_storage(storage);
  if (copy_if_fits(storage->revision, sizeof(storage->revision),
                   manifest->revision, &storage->revision_len) == 0) {
    kassert(arena_destroy(arena_id) == OSAI_OK);
    index->state = OSAI_SOURCE_INDEX_EMPTY;
    index->arena_base = 0;
    index->arena_id = 0;
    return OSAI_ERR_INVALID;
  }

  ++g_active_count;
  klog("source-index: created id=%u cell=%u repo=%s arena_id=%u revision=%s\n",
       manifest->index_id, manifest->cell_id, manifest->repo_path,
       arena_id, storage->revision);
  return OSAI_OK;
}

osai_status_t source_index_add_file(uint32_t index_id, const char *path,
                                   uint32_t language, uint64_t bytes,
                                   uint64_t content_hash) {
  osai_source_index_t *index = lookup_index(index_id);
  if (index == 0 || index->state != OSAI_SOURCE_INDEX_CREATED ||
      !str_prefix(path, "/repo/") || bytes == 0 || content_hash == 0 ||
      language > OSAI_SOURCE_INDEX_LANG_RUST) {
    return OSAI_ERR_INVALID;
  }

  if (string_length(path) >= OSAI_SOURCE_INDEX_PATH_MAX) {
    return OSAI_ERR_INVALID;
  }

  osai_source_index_storage_t *storage = storage_for(index_id);
  if (storage == 0 || storage->file_count >= SOURCE_INDEX_MAX_FILES) {
    return OSAI_ERR_NO_MEMORY;
  }

  for (uint32_t i = 0; i < storage->file_count; ++i) {
    if (paths_equal(storage->files[i].path, path)) {
      return OSAI_ERR_BUSY;
    }
  }

  uint32_t slot = storage->file_count;
  storage->files[slot].state = OSAI_SOURCE_INDEX_SLOT_USED;
  storage->files[slot].file_id = slot;
  storage->files[slot].language = language;
  storage->files[slot].bytes = bytes;
  storage->files[slot].content_hash = content_hash;
  copy_if_fits(storage->files[slot].path, OSAI_SOURCE_INDEX_PATH_MAX, path, 0);
  ++storage->file_count;
  ++g_total_files;

  klog("source-index: %u add file id=%u path=%s language=%u bytes=%lu\n",
       index_id, storage->files[slot].file_id, storage->files[slot].path,
       language, bytes);
  return OSAI_OK;
}

osai_status_t source_index_add_symbol(uint32_t index_id, uint32_t file_id,
                                     const char *name,
                                     uint32_t kind, uint32_t line) {
  osai_source_index_t *index = lookup_index(index_id);
  if (index == 0 || index->state != OSAI_SOURCE_INDEX_CREATED ||
      !str_nonempty(name) ||
      string_length(name) >= OSAI_SOURCE_INDEX_SYMBOL_NAME_MAX ||
      kind > OSAI_SOURCE_INDEX_SYMBOL_VARIABLE || line == 0) {
    return OSAI_ERR_INVALID;
  }

  osai_source_index_storage_t *storage = storage_for(index_id);
  if (storage == 0 || storage->symbol_count >= SOURCE_INDEX_MAX_SYMBOLS ||
      file_id >= storage->file_count ||
      storage->files[file_id].state != OSAI_SOURCE_INDEX_SLOT_USED) {
    return OSAI_ERR_INVALID;
  }

  for (uint32_t i = 0; i < storage->symbol_count; ++i) {
    if (storage->symbols[i].state == OSAI_SOURCE_INDEX_SLOT_USED &&
        storage->symbols[i].file_id == file_id &&
        storage->symbols[i].kind == kind &&
        storage->symbols[i].line == line &&
        paths_equal(storage->symbols[i].name, name)) {
      return OSAI_ERR_BUSY;
    }
  }

  uint32_t slot = storage->symbol_count;
  storage->symbols[slot].state = OSAI_SOURCE_INDEX_SLOT_USED;
  storage->symbols[slot].file_id = file_id;
  storage->symbols[slot].kind = kind;
  storage->symbols[slot].line = line;
  copy_if_fits(storage->symbols[slot].name,
               OSAI_SOURCE_INDEX_SYMBOL_NAME_MAX, name, 0);
  ++storage->symbol_count;
  ++g_total_symbols;

  klog("source-index: %u add symbol id=%u file=%u kind=%u line=%u\n",
       index_id, slot, file_id, kind, line);
  return OSAI_OK;
}

osai_status_t source_index_incremental_update(uint32_t index_id,
                                            const char *revision) {
  osai_source_index_t *index = lookup_index(index_id);
  if (index == 0 || index->state != OSAI_SOURCE_INDEX_CREATED ||
      !str_nonempty(revision) ||
      string_length(revision) >= SOURCE_INDEX_REVISION_MAX) {
    return OSAI_ERR_INVALID;
  }

  osai_source_index_storage_t *storage = storage_for(index_id);
  if (storage == 0) {
    return OSAI_ERR_INVALID;
  }

  if (paths_equal(storage->revision, revision)) {
    return OSAI_ERR_INVALID;
  }

  if (copy_if_fits(storage->revision, sizeof(storage->revision), revision,
                   &storage->revision_len) == 0) {
    return OSAI_ERR_INVALID;
  }

  ++storage->update_count;
  ++g_total_updates;
  klog("source-index: %u update revision=%s count=%u\n", index_id,
       storage->revision, storage->update_count);
  return OSAI_OK;
}

uint64_t source_index_active_count(void) {
  return g_active_count;
}

uint64_t source_index_total_file_records(void) {
  return g_total_files;
}

uint64_t source_index_total_symbol_records(void) {
  return g_total_symbols;
}

uint64_t source_index_total_updates(void) {
  return g_total_updates;
}

void source_index_self_test(void) {
  source_index_runtime_init();

  osai_source_index_manifest_t invalid;
  invalid.index_id = 0;
  invalid.cell_id = 1;
  invalid.repo_path = "repo/app/main";
  invalid.revision = "r1";
  invalid.source_arena_bytes = sizeof(osai_source_index_storage_t);
  kassert(source_index_create(&invalid) == OSAI_ERR_INVALID);

  invalid.repo_path = "/repo/app";
  invalid.revision = "r1";
  invalid.source_arena_bytes = sizeof(osai_source_index_storage_t);
  kassert(source_index_create(&invalid) == OSAI_OK);
  kassert(source_index_create(&invalid) == OSAI_ERR_BUSY);

  kassert(source_index_add_file(0, "/repo/app/main.c", OSAI_SOURCE_INDEX_LANG_C,
                               2048, 0x111111ULL) == OSAI_OK);
  kassert(source_index_add_file(0, "/repo/app/main.c", OSAI_SOURCE_INDEX_LANG_C,
                               512, 0x222222ULL) == OSAI_ERR_BUSY);
  kassert(source_index_add_file(0, "/repo/app/osai-agent.c", OSAI_SOURCE_INDEX_LANG_C,
                               3072, 0x333333ULL) == OSAI_OK);

  kassert(source_index_add_symbol(0, 0, "init", OSAI_SOURCE_INDEX_SYMBOL_FUNCTION, 12) ==
          OSAI_OK);
  kassert(source_index_add_symbol(0, 1, "build_weights",
                                 OSAI_SOURCE_INDEX_SYMBOL_FUNCTION, 88) ==
          OSAI_OK);
  kassert(source_index_add_symbol(0, 99, "oops", OSAI_SOURCE_INDEX_SYMBOL_FUNCTION,
                                 1) == OSAI_ERR_INVALID);

  kassert(source_index_incremental_update(0, "r2") == OSAI_OK);
  kassert(source_index_incremental_update(0, "") == OSAI_ERR_INVALID);

  kassert(source_index_active_count() == 1);
  kassert(source_index_total_file_records() == 2);
  kassert(source_index_total_symbol_records() == 2);
  kassert(source_index_total_updates() == 1);
  klog("source-index: fixture loaded files=%lu symbols=%lu updates=%lu\n",
       source_index_total_file_records(), source_index_total_symbol_records(),
       source_index_total_updates());
}
