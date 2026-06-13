#include <osai/assert.h>
#include <osai/klog.h>
#include <osai/security.h>

#define OSAI_UPDATE_SIGNATURE_PREFIX "osai-update:v1:"
#define OSAI_UPDATE_SIGNATURE_KEY "sig=OSAI-QEMU-DEV-KEY"

static uint64_t g_denied_operations;
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

static osai_status_t reject_security_operation(const char *reason) {
  ++g_denied_operations;
  klog("security: denied operation reason=%s\n", reason);
  return OSAI_ERR_INVALID;
}

void security_policy_init(void) {
  g_denied_operations = 0;
  g_signature_accepts = 0;
  g_signature_rejects = 0;
  g_credential_rejects = 0;
  klog("security: policy initialized mode=qemu-dev signed_updates=stub\n");
}

void security_record_denied_operation(void) {
  ++g_denied_operations;
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

osai_status_t security_validate_update_signature(const char *signature) {
  if (security_reject_credential_material(signature) != OSAI_OK) {
    ++g_signature_rejects;
    return OSAI_ERR_INVALID;
  }

  if (!starts_with(signature, OSAI_UPDATE_SIGNATURE_PREFIX) ||
      !contains(signature, "sha256=") ||
      !contains(signature, OSAI_UPDATE_SIGNATURE_KEY)) {
    ++g_signature_rejects;
    return reject_security_operation("bad-update-signature");
  }

  ++g_signature_accepts;
  klog("security: update signature accepted policy=qemu-dev\n");
  return OSAI_OK;
}

uint64_t security_denied_operation_count(void) {
  return g_denied_operations;
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
  kassert(security_validate_update_signature(
              "osai-update:v1:sha256=abc123:sig=OSAI-QEMU-DEV-KEY") ==
          OSAI_OK);
  kassert(g_credential_rejects == 1);
  kassert(g_signature_rejects == 1);
  kassert(g_signature_accepts == 1);
  kassert(g_denied_operations == 2);
  klog("security: self-test passed denied=%lu credential_rejects=%lu signature_accepts=%lu signature_rejects=%lu\n",
       g_denied_operations, g_credential_rejects, g_signature_accepts,
       g_signature_rejects);
}
