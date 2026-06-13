#include <osai/assert.h>
#include <osai/klog.h>
#include <osai/persistence.h>

#define MAX_SNAPSHOTS 8U
#define SNAPSHOT_LABEL_MAX 32U

typedef struct osai_snapshot_record {
  uint8_t active;
  osai_snapshot_kind_t kind;
  uint32_t owner_id;
  uint64_t generation;
  char label[SNAPSHOT_LABEL_MAX];
} osai_snapshot_record_t;

static osai_snapshot_record_t g_snapshots[MAX_SNAPSHOTS];
static uint64_t g_snapshot_count;
static uint64_t g_rollback_count;
static uint64_t g_reject_count;
static uint64_t g_next_generation;

static int label_valid(const char *label) {
  if (label == 0 || label[0] == '\0') {
    return 0;
  }
  for (uint32_t i = 0; label[i] != '\0'; ++i) {
    if (i + 1U >= SNAPSHOT_LABEL_MAX ||
        label[i] < ' ' || label[i] > '~') {
      return 0;
    }
  }
  return 1;
}

static int kind_valid(osai_snapshot_kind_t kind) {
  return kind == OSAI_SNAPSHOT_BOOT_CONFIG ||
         kind == OSAI_SNAPSHOT_SERVICE ||
         kind == OSAI_SNAPSHOT_WORKSPACE ||
         kind == OSAI_SNAPSHOT_SANDBOX;
}

static void copy_label(char dst[SNAPSHOT_LABEL_MAX], const char *src) {
  uint32_t i = 0;
  while (i + 1U < SNAPSHOT_LABEL_MAX && src[i] != '\0') {
    dst[i] = src[i];
    ++i;
  }
  dst[i] = '\0';
}

static osai_snapshot_record_t *find_snapshot(osai_snapshot_kind_t kind,
                                             uint32_t owner_id) {
  osai_snapshot_record_t *best = 0;
  for (uint32_t i = 0; i < MAX_SNAPSHOTS; ++i) {
    if (g_snapshots[i].active != 0 &&
        g_snapshots[i].kind == kind &&
        g_snapshots[i].owner_id == owner_id &&
        (best == 0 || g_snapshots[i].generation > best->generation)) {
      best = &g_snapshots[i];
    }
  }
  return best;
}

void persistence_runtime_init(void) {
  for (uint32_t i = 0; i < MAX_SNAPSHOTS; ++i) {
    g_snapshots[i].active = 0;
    g_snapshots[i].kind = OSAI_SNAPSHOT_BOOT_CONFIG;
    g_snapshots[i].owner_id = 0;
    g_snapshots[i].generation = 0;
    g_snapshots[i].label[0] = '\0';
  }
  g_snapshot_count = 0;
  g_rollback_count = 0;
  g_reject_count = 0;
  g_next_generation = 1;
  klog("persistence: runtime initialized slots=%u\n", MAX_SNAPSHOTS);
}

osai_status_t persistence_snapshot_create(osai_snapshot_kind_t kind,
                                          uint32_t owner_id,
                                          const char *label) {
  if (!kind_valid(kind) || !label_valid(label)) {
    ++g_reject_count;
    return OSAI_ERR_INVALID;
  }

  for (uint32_t i = 0; i < MAX_SNAPSHOTS; ++i) {
    if (g_snapshots[i].active == 0) {
      g_snapshots[i].active = 1;
      g_snapshots[i].kind = kind;
      g_snapshots[i].owner_id = owner_id;
      g_snapshots[i].generation = g_next_generation;
      ++g_next_generation;
      copy_label(g_snapshots[i].label, label);
      ++g_snapshot_count;
      klog("persistence: snapshot kind=%u owner=%u generation=%lu label=%s\n",
           (unsigned)kind, owner_id, g_snapshots[i].generation,
           g_snapshots[i].label);
      return OSAI_OK;
    }
  }

  ++g_reject_count;
  return OSAI_ERR_NO_MEMORY;
}

osai_status_t persistence_rollback(osai_snapshot_kind_t kind,
                                   uint32_t owner_id) {
  if (!kind_valid(kind)) {
    ++g_reject_count;
    return OSAI_ERR_INVALID;
  }

  osai_snapshot_record_t *snapshot = find_snapshot(kind, owner_id);
  if (snapshot == 0) {
    ++g_reject_count;
    return OSAI_ERR_NOT_FOUND;
  }

  ++g_rollback_count;
  klog("persistence: rollback kind=%u owner=%u generation=%lu label=%s\n",
       (unsigned)kind, owner_id, snapshot->generation, snapshot->label);
  return OSAI_OK;
}

uint64_t persistence_snapshot_count(void) {
  return g_snapshot_count;
}

uint64_t persistence_rollback_count(void) {
  return g_rollback_count;
}

uint64_t persistence_reject_count(void) {
  return g_reject_count;
}

void persistence_self_test(void) {
  persistence_runtime_init();

  kassert(persistence_snapshot_create(OSAI_SNAPSHOT_BOOT_CONFIG, 0,
                                      "boot-config-v1") == OSAI_OK);
  kassert(persistence_snapshot_create(OSAI_SNAPSHOT_SERVICE, 1,
                                      "init-service") == OSAI_OK);
  kassert(persistence_snapshot_create(OSAI_SNAPSHOT_WORKSPACE, 1,
                                      "workspace-r4") == OSAI_OK);
  kassert(persistence_snapshot_create(OSAI_SNAPSHOT_SANDBOX, 0,
                                      "sandbox-rev-good") == OSAI_OK);

  kassert(persistence_rollback(OSAI_SNAPSHOT_BOOT_CONFIG, 0) == OSAI_OK);
  kassert(persistence_rollback(OSAI_SNAPSHOT_SERVICE, 1) == OSAI_OK);
  kassert(persistence_rollback(OSAI_SNAPSHOT_WORKSPACE, 1) == OSAI_OK);
  kassert(persistence_rollback(OSAI_SNAPSHOT_SANDBOX, 0) == OSAI_OK);
  kassert(persistence_rollback(OSAI_SNAPSHOT_WORKSPACE, 99) ==
          OSAI_ERR_NOT_FOUND);
  kassert(persistence_snapshot_create((osai_snapshot_kind_t)99, 0,
                                      "bad") == OSAI_ERR_INVALID);

  kassert(g_snapshot_count == 4);
  kassert(g_rollback_count == 4);
  kassert(g_reject_count == 2);
  klog("persistence: self-test passed snapshots=%lu rollbacks=%lu rejects=%lu\n",
       g_snapshot_count, g_rollback_count, g_reject_count);
}
