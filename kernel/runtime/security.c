#include <osai/assert.h>
#include <osai/klog.h>
#include <osai/security.h>

#define OSAI_UPDATE_SIGNATURE_PREFIX "osai-update:v1:"
#define OSAI_UPDATE_SIGNATURE_KEY "sig=OSAI-QEMU-DEV-KEY"
#define OSAI_UPDATE_SIGNATURE_SHA_FIELD "sha256="

static uint64_t g_denied_operations;
static uint64_t g_capability_denials;
static uint64_t g_fs_denials;
static uint64_t g_workspace_denials;
static uint64_t g_sandbox_denials;
static uint64_t g_rollback_denials;
static uint64_t g_update_policy_rejects;
static uint64_t g_signature_accepts;
static uint64_t g_signature_rejects;
static uint64_t g_credential_rejects;

static const char k_pat_credential_pattern[] = {
    'g', 'i', 't', 'h', 'u', 'b', '_', 'p', 'a', 't', '_', '\0'};
static const char k_short_credential_pattern[] = {'g', 'h', 'p', '_', '\0'};
static const char k_pass_field_pattern[] = {
    'p', 'a', 's', 's', 'w', 'o', 'r', 'd', '=', '\0'};
static const char k_token_field_pattern[] = {
    't', 'o', 'k', 'e', 'n', '=', '\0'};
static const char k_secret_field_pattern[] = {
    's', 'e', 'c', 'r', 'e', 't', '=', '\0'};
static const char k_private_begin_pattern[] = {
    'B', 'E', 'G', 'I', 'N', ' ', '\0'};
static const char k_private_key_pattern[] = {
    'P', 'R', 'I', 'V', 'A', 'T', 'E', ' ', 'K', 'E', 'Y', '\0'};

static int starts_with(const char *text, const char *prefix) {
  if (text == 0 || prefix == 0) {
    return 0;
  }
  while (*prefix != '\0') {
    if (*text != *prefix) {
      return 0;
    }
    ++text;
    ++prefix;
  }
  return 1;
}

static int contains(const char *text, const char *needle) {
  if (text == 0 || needle == 0 || *needle == '\0') {
    return 0;
  }

  for (const char *cursor = text; *cursor != '\0'; ++cursor) {
    const char *hay = cursor;
    const char *pat = needle;
    while (*hay != '\0' && *pat != '\0' && *hay == *pat) {
      ++hay;
      ++pat;
    }
    if (*pat == '\0') {
      return 1;
    }
  }

  return 0;
}

static uint64_t cstr_length(const char *text) {
  uint64_t len = 0;
  if (text == 0) {
    return 0;
  }
  while (text[len] != '\0') {
    ++len;
  }
  return len;
}

static int contains_buffer(const char *text, uint64_t length,
                           const char *needle) {
  uint64_t needle_len = cstr_length(needle);
  if (text == 0 || needle == 0 || needle_len == 0 || length < needle_len) {
    return 0;
  }

  for (uint64_t cursor = 0; cursor <= length - needle_len; ++cursor) {
    uint64_t i = 0;
    while (i < needle_len && text[cursor + i] == needle[i]) {
      ++i;
    }
    if (i == needle_len) {
      return 1;
    }
  }

  return 0;
}

static int str_eq(const char *lhs, const char *rhs) {
  if (lhs == 0 || rhs == 0) {
    return 0;
  }
  while (*lhs != '\0' && *rhs != '\0') {
    if (*lhs != *rhs) {
      return 0;
    }
    ++lhs;
    ++rhs;
  }
  return *lhs == *rhs;
}

static osai_status_t reject_security_operation(const char *reason) {
  ++g_denied_operations;
  klog("security: denied operation reason=%s\n", reason);
  return OSAI_ERR_INVALID;
}

static int is_hex_char(char ch) {
  return (ch >= '0' && ch <= '9') ||
         (ch >= 'a' && ch <= 'f') ||
         (ch >= 'A' && ch <= 'F');
}

static osai_status_t reject_update_signature(const char *reason) {
  ++g_signature_rejects;
  ++g_update_policy_rejects;
  return reject_security_operation(reason);
}

void security_policy_init(void) {
  g_denied_operations = 0;
  g_capability_denials = 0;
  g_fs_denials = 0;
  g_workspace_denials = 0;
  g_sandbox_denials = 0;
  g_rollback_denials = 0;
  g_update_policy_rejects = 0;
  g_signature_accepts = 0;
  g_signature_rejects = 0;
  g_credential_rejects = 0;
  klog("security: policy initialized mode=qemu-dev signed_updates=strict-dev-key\n");
}

void security_record_denied_operation(void) {
  ++g_denied_operations;
}

osai_status_t security_authorize_capability(const char *operation,
                                            uint64_t granted,
                                            uint64_t required) {
  (void)operation;
  if ((granted & required) == required) {
    return OSAI_OK;
  }
  ++g_capability_denials;
  return reject_security_operation("missing-capability");
}

osai_status_t security_authorize_fs_read(const char *path) {
  if (security_reject_credential_material(path) != OSAI_OK) {
    ++g_fs_denials;
    return OSAI_ERR_INVALID;
  }
  if (starts_with(path, "/etc/")) {
    return OSAI_OK;
  }
  ++g_fs_denials;
  return reject_security_operation("fs-read-denied");
}

osai_status_t security_authorize_fs_write(const char *path) {
  if (security_reject_credential_material(path) != OSAI_OK) {
    ++g_fs_denials;
    return OSAI_ERR_INVALID;
  }
  ++g_fs_denials;
  return reject_security_operation("fs-write-denied");
}

osai_status_t security_authorize_git_workspace(uint32_t workspace_id,
                                               uint32_t owner_cell_id,
                                               uint32_t actor_cell_id,
                                               const char *operation) {
  (void)workspace_id;
  if (security_reject_credential_material(operation) != OSAI_OK) {
    ++g_workspace_denials;
    return OSAI_ERR_INVALID;
  }
  if (actor_cell_id == owner_cell_id) {
    return OSAI_OK;
  }
  ++g_workspace_denials;
  return reject_security_operation("git-workspace-owner-mismatch");
}

osai_status_t security_authorize_sandbox(uint32_t sandbox_id,
                                         uint32_t owner_cell_id,
                                         uint32_t actor_cell_id,
                                         const char *operation) {
  (void)sandbox_id;
  if (security_reject_credential_material(operation) != OSAI_OK) {
    ++g_sandbox_denials;
    return OSAI_ERR_INVALID;
  }
  if (actor_cell_id == owner_cell_id) {
    return OSAI_OK;
  }
  ++g_sandbox_denials;
  return reject_security_operation("sandbox-owner-mismatch");
}

osai_status_t security_authorize_rollback(const char *target,
                                          uint32_t authorized) {
  if (security_reject_credential_material(target) != OSAI_OK) {
    ++g_rollback_denials;
    return OSAI_ERR_INVALID;
  }
  if (authorized != 0) {
    return OSAI_OK;
  }
  ++g_rollback_denials;
  return reject_security_operation("rollback-denied");
}

osai_status_t security_reject_credential_material(const char *text) {
  if (text == 0) {
    ++g_credential_rejects;
    return reject_security_operation("null-input");
  }

  if (contains(text, k_pat_credential_pattern) ||
      contains(text, k_short_credential_pattern) ||
      contains(text, k_private_begin_pattern) ||
      contains(text, k_private_key_pattern) ||
      contains(text, k_pass_field_pattern) ||
      contains(text, k_token_field_pattern) ||
      contains(text, k_secret_field_pattern)) {
    ++g_credential_rejects;
    return reject_security_operation("credential-material");
  }

  return OSAI_OK;
}

osai_status_t security_reject_credential_material_buffer(const char *text,
                                                         uint64_t length) {
  if (text == 0) {
    ++g_credential_rejects;
    return reject_security_operation("null-input");
  }
  if (contains_buffer(text, length, k_pat_credential_pattern) ||
      contains_buffer(text, length, k_short_credential_pattern) ||
      contains_buffer(text, length, k_private_begin_pattern) ||
      contains_buffer(text, length, k_private_key_pattern) ||
      contains_buffer(text, length, k_pass_field_pattern) ||
      contains_buffer(text, length, k_token_field_pattern) ||
      contains_buffer(text, length, k_secret_field_pattern)) {
    ++g_credential_rejects;
    return reject_security_operation("credential-material");
  }
  return OSAI_OK;
}

osai_status_t security_validate_update_signature(const char *signature) {
  if (security_reject_credential_material(signature) != OSAI_OK) {
    ++g_signature_rejects;
    ++g_update_policy_rejects;
    return OSAI_ERR_INVALID;
  }

  if (!starts_with(signature, OSAI_UPDATE_SIGNATURE_PREFIX)) {
    return reject_update_signature("bad-update-signature-prefix");
  }

  const char *cursor = signature;
  while (*cursor != '\0' && *cursor != ':') {
    ++cursor;
  }
  if (*cursor != ':') {
    return reject_update_signature("bad-update-signature-format");
  }
  ++cursor;
  while (*cursor != '\0' && *cursor != ':') {
    ++cursor;
  }
  if (*cursor != ':') {
    return reject_update_signature("bad-update-signature-format");
  }
  ++cursor;

  if (!starts_with(cursor, OSAI_UPDATE_SIGNATURE_SHA_FIELD)) {
    return reject_update_signature("missing-update-sha256");
  }
  cursor += sizeof(OSAI_UPDATE_SIGNATURE_SHA_FIELD) - 1U;
  for (uint32_t i = 0; i < 64U; ++i) {
    if (!is_hex_char(cursor[i])) {
      return reject_update_signature("bad-update-sha256");
    }
  }
  cursor += 64U;
  if (*cursor != ':') {
    return reject_update_signature("bad-update-signature-format");
  }
  ++cursor;

  if (!str_eq(cursor, OSAI_UPDATE_SIGNATURE_KEY)) {
    return reject_update_signature("bad-update-signature-key");
  }

  ++g_signature_accepts;
  klog("security: update signature accepted policy=qemu-dev\n");
  return OSAI_OK;
}

osai_status_t security_validate_benchmark_record(const char *record) {
  if (security_reject_credential_material(record) != OSAI_OK) {
    return OSAI_ERR_INVALID;
  }
  if (record == 0 || !contains(record, "\"design_targets\":true")) {
    return reject_security_operation("benchmark-record-policy");
  }
  return OSAI_OK;
}

uint64_t security_denied_operation_count(void) {
  return g_denied_operations;
}

uint64_t security_capability_denial_count(void) {
  return g_capability_denials;
}

uint64_t security_fs_denial_count(void) {
  return g_fs_denials;
}

uint64_t security_workspace_denial_count(void) {
  return g_workspace_denials;
}

uint64_t security_sandbox_denial_count(void) {
  return g_sandbox_denials;
}

uint64_t security_rollback_denial_count(void) {
  return g_rollback_denials;
}

uint64_t security_update_policy_reject_count(void) {
  return g_update_policy_rejects;
}

uint64_t security_signature_accept_count(void) {
  return g_signature_accepts;
}

uint64_t security_signature_reject_count(void) {
  return g_signature_rejects;
}

uint64_t security_credential_reject_count(void) {
  return g_credential_rejects;
}

void security_self_test(void) {
  security_policy_init();
  const char credential_fixture[] = {
      'g', 'i', 't', 'h', 'u', 'b', '_', 'p', 'a', 't', '_',
      'e', 'x', 'a', 'm', 'p', 'l', 'e', '\0'};
  kassert(security_reject_credential_material("normal-update-request") ==
          OSAI_OK);
  kassert(security_reject_credential_material(credential_fixture) ==
          OSAI_ERR_INVALID);
  kassert(security_validate_update_signature("unsigned-update") ==
          OSAI_ERR_INVALID);
  kassert(security_authorize_capability("service.update", 0U, 16U) ==
          OSAI_ERR_INVALID);
  kassert(security_authorize_fs_read("/etc/services/source-index.svc") ==
          OSAI_OK);
  kassert(security_authorize_fs_write("/etc/services/source-index.svc") ==
          OSAI_ERR_INVALID);
  kassert(security_authorize_git_workspace(0, 1, 2, "patch") ==
          OSAI_ERR_INVALID);
  kassert(security_authorize_sandbox(0, 1, 2, "build") ==
          OSAI_ERR_INVALID);
  kassert(security_authorize_rollback("/init", 0) == OSAI_ERR_INVALID);
  kassert(security_validate_benchmark_record(
              "{\"design_targets\":true,\"latency\":\"target\"}") ==
          OSAI_OK);
  kassert(security_validate_benchmark_record("token=bad") ==
          OSAI_ERR_INVALID);
  kassert(security_validate_update_signature(
              "osai-update:v1:sha256=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef:sig=OSAI-QEMU-DEV-KEY") ==
          OSAI_OK);
  kassert(g_credential_rejects == 2);
  kassert(g_signature_rejects == 1);
  kassert(g_signature_accepts == 1);
  kassert(g_capability_denials == 1);
  kassert(g_fs_denials == 1);
  kassert(g_workspace_denials == 1);
  kassert(g_sandbox_denials == 1);
  kassert(g_rollback_denials == 1);
  kassert(g_update_policy_rejects == 1);
  kassert(g_denied_operations == 8);
  klog("security: self-test passed denied=%lu capability_denials=%lu fs_denials=%lu workspace_denials=%lu sandbox_denials=%lu rollback_denials=%lu update_policy_rejects=%lu credential_rejects=%lu signature_accepts=%lu signature_rejects=%lu\n",
       g_denied_operations, g_capability_denials, g_fs_denials,
       g_workspace_denials, g_sandbox_denials, g_rollback_denials,
       g_update_policy_rejects, g_credential_rejects, g_signature_accepts,
       g_signature_rejects);
}
