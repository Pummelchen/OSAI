#include <osai/assert.h>
#include <osai/klog.h>
#include <osai/mutable_fs.h>
#include <osai/persistence.h>
#include <osai/security.h>
#include <osai/syscall.h>
#include <osai/update.h>

#define UPDATE_TARGET_MAX 32U
#define UPDATE_LABEL_MAX 32U

typedef enum osai_update_state {
  OSAI_UPDATE_IDLE = 0,
  OSAI_UPDATE_PENDING = 1,
  OSAI_UPDATE_STAGED = 2,
  OSAI_UPDATE_COMMITTED = 3,
  OSAI_UPDATE_FAILED = 4,
  OSAI_UPDATE_RECOVERED = 5,
  OSAI_UPDATE_ROLLED_BACK = 6,
} osai_update_state_t;

typedef struct osai_update_transaction {
  uint32_t active;
  uint32_t generation;
  osai_update_state_t state;
  char target[UPDATE_TARGET_MAX];
  char rollback_label[UPDATE_LABEL_MAX];
} osai_update_transaction_t;

static osai_update_transaction_t g_update;
static uint64_t g_transactions;
static uint64_t g_stages;
static uint64_t g_commits;
static uint64_t g_failures;
static uint64_t g_recoveries;
static uint64_t g_rollbacks;
static uint64_t g_boot_fallbacks;
static uint64_t g_records_persisted;
static uint64_t g_rollback_points;
static uint64_t g_rejects;

static void bytes_zero(void *buffer, uint64_t size) {
  uint8_t *bytes = (uint8_t *)buffer;
  for (uint64_t i = 0; i < size; ++i) {
    bytes[i] = 0;
  }
}

static uint64_t cstr_len(const char *value) {
  uint64_t len = 0;
  if (value == 0) {
    return 0;
  }
  while (value[len] != '\0') {
    ++len;
  }
  return len;
}

static int token_valid(const char *value, uint64_t max_len) {
  uint64_t len = cstr_len(value);
  if (len == 0 || len >= max_len) {
    return 0;
  }
  for (uint64_t i = 0; i < len; ++i) {
    char ch = value[i];
    if (ch < '!' || ch > '~' || ch == ':' || ch == '*' || ch == '?' ||
        ch == '"' || ch == '<' || ch == '>' || ch == '|') {
      return 0;
    }
  }
  return 1;
}

static int target_valid(const char *target) {
  uint64_t len = cstr_len(target);
  if (!token_valid(target, UPDATE_TARGET_MAX) || len < 2U ||
      target[0] != '/' || target[len - 1U] == '/') {
    return 0;
  }
  for (uint64_t i = 0; i < len; ++i) {
    if (target[i] == '/' && target[i + 1U] == '/') {
      return 0;
    }
    if (target[i] == '.' && target[i + 1U] == '.' &&
        (i == 0 || target[i - 1U] == '/') &&
        (target[i + 2U] == '/' || target[i + 2U] == '\0')) {
      return 0;
    }
  }
  return 1;
}

static void reset_transaction(void) { bytes_zero(&g_update, sizeof(g_update)); }

static void copy_token(char *dst, uint64_t capacity, const char *src) {
  uint64_t i = 0;
  while (i + 1U < capacity && src[i] != '\0') {
    dst[i] = src[i];
    ++i;
  }
  dst[i] = '\0';
}

static const char *state_name(osai_update_state_t state) {
  switch (state) {
    case OSAI_UPDATE_IDLE:
      return "idle";
    case OSAI_UPDATE_PENDING:
      return "pending";
    case OSAI_UPDATE_STAGED:
      return "staged";
    case OSAI_UPDATE_COMMITTED:
      return "committed";
    case OSAI_UPDATE_FAILED:
      return "failed";
    case OSAI_UPDATE_RECOVERED:
      return "recovered";
    case OSAI_UPDATE_ROLLED_BACK:
      return "rolled-back";
  }
  return "invalid";
}

static osai_status_t persist_update_state(void) {
  if (mutable_fs_record_update_transaction(
          g_update.generation, state_name(g_update.state), g_update.target,
          g_update.rollback_label) != OSAI_OK) {
    ++g_rejects;
    return OSAI_ERR_IO;
  }
  ++g_records_persisted;
  return OSAI_OK;
}

void update_runtime_init(void) {
  bytes_zero(&g_update, sizeof(g_update));
  g_transactions = 0;
  g_stages = 0;
  g_commits = 0;
  g_failures = 0;
  g_recoveries = 0;
  g_rollbacks = 0;
  g_boot_fallbacks = 0;
  g_records_persisted = 0;
  g_rollback_points = 0;
  g_rejects = 0;
  klog("update: runtime initialized\n");
}

osai_status_t update_begin(uint32_t generation, const char *target,
                           const char *signature) {
  if (g_update.active != 0 || generation == 0 ||
      !target_valid(target) ||
      security_authorize_update_signature(
          signature, OSAI_CAP_UPDATE | OSAI_CAP_ADMIN) != OSAI_OK) {
    ++g_rejects;
    return OSAI_ERR_INVALID;
  }

  g_update.active = 1;
  g_update.generation = generation;
  g_update.state = OSAI_UPDATE_PENDING;
  copy_token(g_update.target, sizeof(g_update.target), target);
  copy_token(g_update.rollback_label, sizeof(g_update.rollback_label),
             "update-rp");
  if (persistence_snapshot_create(OSAI_SNAPSHOT_UPDATE, 0,
                                  g_update.rollback_label) != OSAI_OK) {
    reset_transaction();
    ++g_rejects;
    return OSAI_ERR_IO;
  }
  if (persist_update_state() != OSAI_OK) {
    reset_transaction();
    return OSAI_ERR_IO;
  }

  ++g_transactions;
  ++g_rollback_points;
  klog("update: transaction begin generation=%u target=%s rollback=%s\n",
       generation, g_update.target, g_update.rollback_label);
  return OSAI_OK;
}

osai_status_t update_stage(void) {
  if (g_update.active == 0 || g_update.state != OSAI_UPDATE_PENDING) {
    ++g_rejects;
    return OSAI_ERR_INVALID;
  }
  g_update.state = OSAI_UPDATE_STAGED;
  if (persist_update_state() != OSAI_OK ||
      mutable_fs_commit("update-stage") != OSAI_OK) {
    ++g_rejects;
    return OSAI_ERR_IO;
  }
  ++g_stages;
  klog("update: staged generation=%u target=%s\n", g_update.generation,
       g_update.target);
  return OSAI_OK;
}

osai_status_t update_commit(void) {
  if (g_update.active == 0 || g_update.state != OSAI_UPDATE_STAGED) {
    ++g_rejects;
    return OSAI_ERR_INVALID;
  }
  g_update.state = OSAI_UPDATE_COMMITTED;
  if (persist_update_state() != OSAI_OK ||
      mutable_fs_commit("update-commit") != OSAI_OK) {
    ++g_rejects;
    return OSAI_ERR_IO;
  }
  ++g_commits;
  klog("update: committed generation=%u target=%s\n", g_update.generation,
       g_update.target);
  return OSAI_OK;
}

osai_status_t update_fail(void) {
  if (g_update.active == 0 || g_update.state != OSAI_UPDATE_STAGED) {
    ++g_rejects;
    return OSAI_ERR_INVALID;
  }
  g_update.state = OSAI_UPDATE_FAILED;
  if (persist_update_state() != OSAI_OK) {
    ++g_rejects;
    return OSAI_ERR_IO;
  }
  ++g_failures;
  klog("update: failed generation=%u target=%s\n", g_update.generation,
       g_update.target);
  return OSAI_OK;
}

osai_status_t update_recover_boot(void) {
  if (g_update.active == 0 || g_update.state != OSAI_UPDATE_FAILED) {
    ++g_rejects;
    return OSAI_ERR_INVALID;
  }
  if (persistence_rollback(OSAI_SNAPSHOT_UPDATE, 0) != OSAI_OK ||
      mutable_fs_rollback() != OSAI_OK) {
    ++g_rejects;
    return OSAI_ERR_IO;
  }
  g_update.state = OSAI_UPDATE_RECOVERED;
  if (persist_update_state() != OSAI_OK) {
    ++g_rejects;
    return OSAI_ERR_IO;
  }
  ++g_recoveries;
  ++g_boot_fallbacks;
  klog("update: boot fallback recovered generation=%u rollback=%s\n",
       g_update.generation, g_update.rollback_label);
  g_update.active = 0;
  return OSAI_OK;
}

osai_status_t update_rollback(void) {
  if (g_update.active == 0 || g_update.state != OSAI_UPDATE_COMMITTED ||
      security_authorize_rollback(g_update.target, 1) != OSAI_OK) {
    ++g_rejects;
    return OSAI_ERR_INVALID;
  }
  if (persistence_rollback(OSAI_SNAPSHOT_UPDATE, 0) != OSAI_OK ||
      mutable_fs_rollback() != OSAI_OK) {
    ++g_rejects;
    return OSAI_ERR_IO;
  }
  g_update.state = OSAI_UPDATE_ROLLED_BACK;
  if (persist_update_state() != OSAI_OK) {
    ++g_rejects;
    return OSAI_ERR_IO;
  }
  ++g_rollbacks;
  klog("update: rollback complete generation=%u target=%s\n",
       g_update.generation, g_update.target);
  g_update.active = 0;
  return OSAI_OK;
}

uint64_t update_transaction_count(void) { return g_transactions; }
uint64_t update_stage_count(void) { return g_stages; }
uint64_t update_commit_count(void) { return g_commits; }
uint64_t update_failure_count(void) { return g_failures; }
uint64_t update_recovery_count(void) { return g_recoveries; }
uint64_t update_rollback_count(void) { return g_rollbacks; }
uint64_t update_boot_fallback_count(void) { return g_boot_fallbacks; }
uint64_t update_record_persist_count(void) { return g_records_persisted; }
uint64_t update_rollback_point_count(void) { return g_rollback_points; }
uint64_t update_reject_count(void) { return g_rejects; }

void update_self_test(void) {
  static const char k_sig2[] =
      "osai-update:v1:gen=2:sha256=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef:key=OSAI-QEMU-DEV-PUBKEY:sig=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
  static const char k_sig3[] =
      "osai-update:v1:gen=3:sha256=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef:key=OSAI-QEMU-DEV-PUBKEY:sig=abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789";

  update_runtime_init();
  kassert(update_stage() == OSAI_ERR_INVALID);
  kassert(update_begin(2, "/", k_sig2) == OSAI_ERR_INVALID);
  kassert(update_begin(2, "/system/osai", k_sig2) == OSAI_OK);
  kassert(update_stage() == OSAI_OK);
  kassert(update_fail() == OSAI_OK);
  kassert(update_recover_boot() == OSAI_OK);
  kassert(update_begin(3, "/system/osai", k_sig3) == OSAI_OK);
  kassert(update_stage() == OSAI_OK);
  kassert(update_commit() == OSAI_OK);
  kassert(update_rollback() == OSAI_OK);

  kassert(g_transactions == 2);
  kassert(g_stages == 2);
  kassert(g_commits == 1);
  kassert(g_failures == 1);
  kassert(g_recoveries == 1);
  kassert(g_rollbacks == 1);
  kassert(g_boot_fallbacks == 1);
  kassert(g_records_persisted == 8);
  kassert(g_rollback_points == 2);
  kassert(g_rejects == 2);
  klog("update: self-test passed transactions=%lu staged=%lu committed=%lu failed=%lu recovered=%lu rollbacks=%lu boot_fallbacks=%lu records=%lu rollback_points=%lu rejects=%lu\n",
       g_transactions, g_stages, g_commits, g_failures, g_recoveries,
       g_rollbacks, g_boot_fallbacks, g_records_persisted,
       g_rollback_points, g_rejects);
}
