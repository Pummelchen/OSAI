#include <osai/assert.h>
#include <osai/klog.h>
#include <osai/mutable_fs.h>
#include <osai/security.h>
#include <osai/service.h>
#include <osai/syscall.h>
#include <osai/user.h>

#define OSAI_CMD_TOKEN_BUFFER 160U
#define OSAI_OSCTL_MAX_TOKENS 8U
#define OSAI_POLICY_COPY_SIZE 16U

static const char k_policy_never[] = "never";
static const char k_policy_always[] = "always";
static const char k_policy_default[] = "unset";
static const char k_log_serial[] = "serial";
static const char k_log_off[] = "off";
static const char k_init_service_name[] = "/init";
static const char k_manager_service_name[] = "/bin/service-manager";
static const char k_child_service_name[] = "/svc/source-index";
static const char k_child_parent_name[] = "/init";

static osai_service_t g_init_service;
static osai_service_t g_manager_service;
static osai_service_t g_child_service;
static uint64_t g_child_descriptor_count;
static uint64_t g_service_tree_edge_count;
static uint64_t g_service_transition_count;
static uint64_t g_service_restart_count;
static uint64_t g_service_crash_count;
static uint64_t g_service_cleanup_count;
static uint64_t g_service_log_record_count;

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

static uint8_t token_length(const char *token) {
  uint8_t len = 0;
  if (token == 0) {
    return 0;
  }
  while (token[len] != '\0' && len < UINT8_MAX) {
    ++len;
  }
  return len;
}

static void copy_str(char dst[OSAI_POLICY_COPY_SIZE], const char *src) {
  if (dst == 0) {
    return;
  }
  uint8_t i = 0;
  while (i + 1U < OSAI_POLICY_COPY_SIZE && src != 0 && src[i] != '\0') {
    dst[i] = src[i];
    ++i;
  }
  dst[i] = '\0';
}

static const char *service_state_name(osai_service_state_t state) {
  switch (state) {
  case OSAI_SERVICE_STOPPED:
    return "stopped";
  case OSAI_SERVICE_STARTING:
    return "starting";
  case OSAI_SERVICE_RUNNING:
    return "running";
  case OSAI_SERVICE_EXITED:
    return "exited";
  case OSAI_SERVICE_FAILED:
    return "failed";
  default:
    return "unknown";
  }
}

static osai_status_t require_service_capability(uint64_t capability) {
  if (user_current_process() == 0) {
    return OSAI_ERR_INVALID;
  }
  return user_process_has_capability(capability);
}

static osai_status_t parse_u32(const char *value, uint32_t *out) {
  uint32_t parsed = 0;
  const char *cursor = value;
  if (out == 0 || value == 0 || *value == '\0') {
    return OSAI_ERR_INVALID;
  }

  while (*cursor != '\0') {
    if (*cursor < '0' || *cursor > '9') {
      return OSAI_ERR_INVALID;
    }
    if (parsed > (UINT32_MAX - (uint32_t)(*cursor - '0')) / 10U) {
      return OSAI_ERR_INVALID;
    }
    parsed = (parsed * 10U) + (uint32_t)(*cursor - '0');
    ++cursor;
  }

  *out = parsed;
  return OSAI_OK;
}

static void service_snapshot_capture(osai_service_t *service) {
  copy_str(service->restart_policy_snapshot, service->restart_policy);
  copy_str(service->log_policy_snapshot, service->log_policy);
  service->max_restarts_snapshot = service->max_restarts;
  service->starts_snapshot = service->starts;
  service->restart_attempts_snapshot = service->restart_attempts;
  service->log_records_snapshot = service->log_records;
}

static void reset_service(osai_service_t *service, const char *name) {
  service->name = name;
  service->parent_name = 0;
  service->restart_policy = k_policy_default;
  service->log_policy = k_log_off;
  service->max_restarts = UINT32_C(0xffffffff);
  service->state = OSAI_SERVICE_STOPPED;
  service->exit_code = 0;
  service->starts = 0;
  service->restart_attempts = 0;
  service->log_records = 0;
  service->child_count = 0;
  service->crash_count = 0;
  service->cleanup_count = 0;
  service->update_attempts = 0;
  service->update_rejections = 0;
  service->rollback_count = 0;
  copy_str(service->restart_policy_snapshot, k_policy_default);
  copy_str(service->log_policy_snapshot, k_log_off);
  service->max_restarts_snapshot = service->max_restarts;
  service->starts_snapshot = 0;
  service->restart_attempts_snapshot = 0;
  service->log_records_snapshot = 0;
}

static osai_service_t *find_service(const char *name) {
  if (str_eq(name, g_init_service.name)) {
    return &g_init_service;
  }
  if (str_eq(name, g_manager_service.name)) {
    return &g_manager_service;
  }
  if (g_child_service.name != 0 && str_eq(name, g_child_service.name)) {
    return &g_child_service;
  }
  return 0;
}

static void persist_service_state(osai_service_t *service) {
  if (service == 0 || !str_eq(service->name, k_child_service_name)) {
    return;
  }
  osai_status_t status = mutable_fs_record_service_state(
      service->name, service_state_name(service->state));
  if (status == OSAI_OK) {
    klog("service: %s mutable-state persisted state=%s\n", service->name,
         service_state_name(service->state));
  } else {
    klog("service: %s mutable-state persist failed state=%s status=%u\n",
         service->name, service_state_name(service->state),
         (unsigned)status);
  }
}

static void service_snapshot_restore(osai_service_t *service) {
  service->restart_policy = service->restart_policy_snapshot;
  service->log_policy = service->log_policy_snapshot;

  if (service->restart_policy[0] == '\0') {
    service->restart_policy = k_policy_default;
  }
  if (service->log_policy[0] == '\0') {
    service->log_policy = k_log_off;
  }

  service->max_restarts = service->max_restarts_snapshot;
  service->starts = service->starts_snapshot;
  service->restart_attempts = service->restart_attempts_snapshot;
  service->log_records = service->log_records_snapshot;
}

typedef struct service_config {
  const char *restart_policy;
  const char *log_policy;
  uint32_t max_restarts;
  uint32_t seen_fields;
} service_config_t;

static osai_status_t parse_key_value(const char *token, const char *expected_key,
                                    const char **value_out) {
  if (token == 0) {
    return OSAI_ERR_INVALID;
  }

  const char *sep = token;
  while (*sep != '\0' && *sep != '=') {
    ++sep;
  }
  if (*sep != '=' || sep == token) {
    return OSAI_ERR_INVALID;
  }

  uint8_t key_len = token_length(token);
  if (expected_key != 0) {
    uint8_t expect_len = token_length(expected_key);
    uint8_t actual_len = (uint8_t)(sep - token);
    if (actual_len != expect_len) {
      return OSAI_ERR_INVALID;
    }
    for (uint8_t i = 0; i < actual_len; ++i) {
      if (token[i] != expected_key[i]) {
        return OSAI_ERR_INVALID;
      }
    }
  }

  if (sep[1] == '\0' || key_len >= OSAI_CMD_TOKEN_BUFFER) {
    return OSAI_ERR_INVALID;
  }

  if (value_out != 0) {
    value_out[0] = sep + 1U;
  }
  return OSAI_OK;
}

static osai_status_t parse_restart_token(const char *token, service_config_t *config) {
  const char *value = 0;
  if (parse_key_value(token, "restart", &value) != OSAI_OK) {
    klog("service-manager: invalid restart field='%s'\n", token);
    return OSAI_ERR_INVALID;
  }
  if (!str_eq(value, k_policy_never) && !str_eq(value, k_policy_always)) {
    return OSAI_ERR_INVALID;
  }
  config->restart_policy = value;
  config->seen_fields |= 1U;
  return OSAI_OK;
}

static osai_status_t parse_log_token(const char *token, service_config_t *config) {
  const char *value = 0;
  if (parse_key_value(token, "log", &value) != OSAI_OK) {
    klog("service-manager: invalid log field='%s'\n", token);
    return OSAI_ERR_INVALID;
  }
  if (!str_eq(value, k_log_serial) && !str_eq(value, k_log_off)) {
    return OSAI_ERR_INVALID;
  }
  config->log_policy = value;
  config->seen_fields |= 2U;
  return OSAI_OK;
}

static osai_status_t parse_max_restarts_token(const char *token,
                                             service_config_t *config) {
  const char *value = 0;
  if (parse_key_value(token, "max_restarts", &value) != OSAI_OK) {
    klog("service-manager: invalid max_restarts field='%s'\n", token);
    return OSAI_ERR_INVALID;
  }
  if (parse_u32(value, &config->max_restarts) != OSAI_OK) {
    klog("service-manager: invalid max_restarts value='%s'\n", value);
    return OSAI_ERR_INVALID;
  }
  config->seen_fields |= 4U;
  return OSAI_OK;
}

static osai_status_t apply_service_config(osai_service_t *service,
                                          const service_config_t *config) {
  if (service == 0 || config == 0 ||
      (config->seen_fields & 7U) != 7U ||
      config->restart_policy == 0 ||
      config->log_policy == 0) {
    return OSAI_ERR_INVALID;
  }

  service->restart_policy = k_policy_default;
  service->log_policy = k_log_off;
  service->max_restarts = config->max_restarts;

  if (str_eq(config->restart_policy, k_policy_never)) {
    service->restart_policy = k_policy_never;
  } else if (str_eq(config->restart_policy, k_policy_always)) {
    service->restart_policy = k_policy_always;
  } else {
    return OSAI_ERR_INVALID;
  }

  if (str_eq(config->log_policy, k_log_serial)) {
    service->log_policy = k_log_serial;
  } else if (str_eq(config->log_policy, k_log_off)) {
    service->log_policy = k_log_off;
  } else {
    return OSAI_ERR_INVALID;
  }

  service_snapshot_capture(service);
  klog(
      "service-manager: configured %s restart=%s log=%s max_restarts=%lu\n",
      service->name, service->restart_policy, service->log_policy,
      (unsigned long)service->max_restarts);
  return OSAI_OK;
}

static osai_status_t configure_service(const char *service_name,
                                       const char *token3, const char *token4,
                                       const char *token5) {
  osai_service_t *service = find_service(service_name);
  service_config_t config;
  config.restart_policy = 0;
  config.log_policy = 0;
  config.max_restarts = 0;
  config.seen_fields = 0;

  if (service == 0 || token3 == 0 || token4 == 0 || token5 == 0) {
    return OSAI_ERR_INVALID;
  }

  if (parse_restart_token(token3, &config) != OSAI_OK) {
    return OSAI_ERR_INVALID;
  }
  if (parse_log_token(token4, &config) != OSAI_OK) {
    return OSAI_ERR_INVALID;
  }
  if (parse_max_restarts_token(token5, &config) != OSAI_OK) {
    return OSAI_ERR_INVALID;
  }

  return apply_service_config(service, &config);
}

static int token_safe(const char *token) {
  if (token == 0 || *token == '\0') {
    return 0;
  }
  for (uint32_t i = 0; token[i] != '\0'; ++i) {
    if (token[i] < ' ' || token[i] > '~') {
      return 0;
    }
  }
  return 1;
}

static osai_status_t handle_status(const char *service_name) {
  osai_service_t *service = find_service(service_name);
  if (service == 0) {
    return OSAI_ERR_INVALID;
  }
  klog(
      "osctl: %s state=%s starts=%lu restarts=%lu logs=%lu restart_policy=%s "
      "log_policy=%s max_restarts=%lu exit_code=%u\n",
      service->name, service_state_name(service->state), service->starts,
      service->restart_attempts, service->log_records,
      service->restart_policy, service->log_policy,
      (unsigned long)service->max_restarts, (unsigned)service->exit_code);
  return OSAI_OK;
}

static osai_status_t handle_log(const char *service_name, const char *message) {
  osai_service_t *service = find_service(service_name);
  if (service == 0 || !token_safe(message)) {
    return OSAI_ERR_INVALID;
  }
  if (security_reject_credential_material(message) != OSAI_OK) {
    return OSAI_ERR_INVALID;
  }
  ++service->log_records;
  ++g_service_log_record_count;
  klog("service-manager: log %s %s records=%lu\n", service_name, message,
       service->log_records);
  return OSAI_OK;
}

static osai_status_t start_service(osai_service_t *service) {
  if (service == 0) {
    return OSAI_ERR_INVALID;
  }
  if (service->state != OSAI_SERVICE_STOPPED &&
      service->state != OSAI_SERVICE_EXITED &&
      service->state != OSAI_SERVICE_FAILED) {
    return OSAI_ERR_INVALID;
  }
  if (service->state != OSAI_SERVICE_STOPPED &&
      service->max_restarts != UINT32_C(0xffffffff) &&
      service->starts >= service->max_restarts) {
    return OSAI_ERR_INVALID;
  }

  service->state = OSAI_SERVICE_STARTING;
  ++service->starts;
  ++g_service_transition_count;
  klog("service: %s state=starting parent=%s\n", service->name,
       service->parent_name != 0 ? service->parent_name : "(root)");
  service->state = OSAI_SERVICE_RUNNING;
  ++g_service_transition_count;
  klog("service: %s state=running parent=%s\n", service->name,
       service->parent_name != 0 ? service->parent_name : "(root)");
  persist_service_state(service);
  return OSAI_OK;
}

static void cleanup_service_runtime(osai_service_t *service,
                                    const char *reason) {
  if (service == 0) {
    return;
  }
  ++service->cleanup_count;
  ++g_service_cleanup_count;
  klog("service-supervisor: cleanup %s reason=%s cleanups=%lu\n",
       service->name, reason, service->cleanup_count);
}

static osai_status_t supervisor_restart_failed_child(osai_service_t *service) {
  if (service == 0) {
    return OSAI_ERR_INVALID;
  }
  if (!str_eq(service->restart_policy, k_policy_always)) {
    klog("service-supervisor: restart skipped %s policy=%s\n",
         service->name, service->restart_policy);
    return OSAI_ERR_INVALID;
  }
  ++service->restart_attempts;
  if (service->max_restarts != UINT32_C(0xffffffff) &&
      service->restart_attempts > service->max_restarts) {
    klog("service-supervisor: restart blocked %s max_restarts=%lu attempts=%lu\n",
         service->name, (unsigned long)service->max_restarts,
         service->restart_attempts);
    return OSAI_ERR_INVALID;
  }

  cleanup_service_runtime(service, "crash");
  service->state = OSAI_SERVICE_STOPPED;
  ++g_service_transition_count;
  klog("service-supervisor: restarting child %s parent=%s attempt=%lu\n",
       service->name, service->parent_name != 0 ? service->parent_name : "(root)",
       service->restart_attempts);
  if (start_service(service) != OSAI_OK) {
    return OSAI_ERR_INVALID;
  }
  ++g_service_restart_count;
  return OSAI_OK;
}

static osai_status_t mark_service_exit(osai_service_t *service, int exit_code,
                                       uint32_t supervise) {
  if (service == 0 || service->state != OSAI_SERVICE_RUNNING) {
    return OSAI_ERR_INVALID;
  }

  service->exit_code = exit_code;
  service->state =
      exit_code == 0 ? OSAI_SERVICE_EXITED : OSAI_SERVICE_FAILED;
  ++g_service_transition_count;
  if (exit_code != 0) {
    ++service->crash_count;
    ++g_service_crash_count;
  }
  klog("service: %s state=%s exit_code=%u\n", service->name,
       service_state_name(service->state), (unsigned)exit_code);
  persist_service_state(service);

  if (supervise != 0 && exit_code != 0) {
    return supervisor_restart_failed_child(service);
  }
  if (exit_code == 0) {
    cleanup_service_runtime(service, "exit");
  }
  return OSAI_OK;
}

static osai_status_t handle_restart(const char *service_name) {
  osai_service_t *service = find_service(service_name);
  if (service == 0) {
    return OSAI_ERR_INVALID;
  }

  ++service->restart_attempts;
  if (str_eq(service->restart_policy, k_policy_never)) {
    klog("service-manager: restart denied %s policy=%s attempts=%lu\n",
         service->name, service->restart_policy, service->restart_attempts);
    return OSAI_ERR_INVALID;
  }

  if (service->max_restarts != UINT32_C(0xffffffff) &&
      service->restart_attempts > service->max_restarts) {
    klog("service-manager: restart denied %s max_restarts=%lu attempts=%lu\n",
         service->name, (unsigned long)service->max_restarts,
         service->restart_attempts);
    return OSAI_ERR_INVALID;
  }

  service->state = OSAI_SERVICE_STOPPED;
  ++g_service_transition_count;
  klog("service-manager: restart allowed %s attempts=%lu\n",
       service->name, service->restart_attempts);
  if (start_service(service) != OSAI_OK) {
    return OSAI_ERR_INVALID;
  }
  ++g_service_restart_count;
  return OSAI_OK;
}

static osai_status_t handle_start(const char *service_name) {
  osai_service_t *service = find_service(service_name);
  if (service == 0) {
    return OSAI_ERR_INVALID;
  }
  return start_service(service);
}

static osai_status_t handle_update(const char *signature) {
  if (signature == 0 || signature[0] == '\0') {
    security_record_denied_operation();
    return OSAI_ERR_INVALID;
  }
  if (security_reject_credential_material(signature) != OSAI_OK) {
    ++g_init_service.update_rejections;
    return OSAI_ERR_INVALID;
  }
  if (require_service_capability(OSAI_CAP_UPDATE) != OSAI_OK) {
    ++g_init_service.update_rejections;
    const osai_user_process_t *process = user_current_process();
    uint64_t granted = process != 0 ? process->capability_mask : 0;
    (void)security_authorize_capability("service.update", granted,
                                        OSAI_CAP_UPDATE);
    return OSAI_ERR_INVALID;
  }

  if (token_safe(signature) == 0 ||
      security_validate_update_signature(signature) != OSAI_OK) {
    ++g_init_service.update_rejections;
    return OSAI_ERR_INVALID;
  }

  ++g_init_service.update_attempts;
  klog("service-manager: update token accepted length=%lu\n",
       (unsigned long)token_length(signature));
  return OSAI_OK;
}

static osai_status_t handle_rollback(const char *service_name) {
  if (!str_eq(service_name, g_init_service.name)) {
    return OSAI_ERR_INVALID;
  }
  if (require_service_capability(OSAI_CAP_SERVICE_ROLLBACK) != OSAI_OK) {
    ++g_init_service.rollback_count;
    (void)security_authorize_rollback(service_name, 0);
    return OSAI_ERR_INVALID;
  }
  if (security_authorize_rollback(service_name, 1) != OSAI_OK) {
    ++g_init_service.rollback_count;
    return OSAI_ERR_INVALID;
  }

  service_snapshot_restore(&g_init_service);
  ++g_init_service.rollback_count;
  klog("service-manager: rollback /init restart=%s log=%s max_restarts=%lu\n",
       g_init_service.restart_policy, g_init_service.log_policy,
       (unsigned long)g_init_service.max_restarts);
  return OSAI_OK;
}

static osai_status_t handle_stop(const char *service_name) {
  osai_service_t *service = find_service(service_name);
  return mark_service_exit(service, 0, 0);
}

static osai_status_t parse_exit_code_token(const char *token, int *exit_code) {
  const char *value = 0;
  uint32_t parsed = 0;
  if (exit_code == 0 || parse_key_value(token, "code", &value) != OSAI_OK ||
      parse_u32(value, &parsed) != OSAI_OK || parsed == 0 ||
      parsed > INT32_MAX) {
    return OSAI_ERR_INVALID;
  }
  *exit_code = (int)parsed;
  return OSAI_OK;
}

static osai_status_t handle_crash(const char *service_name,
                                  const char *code_token) {
  osai_service_t *service = find_service(service_name);
  int exit_code = 0;
  if (service == 0 ||
      parse_exit_code_token(code_token, &exit_code) != OSAI_OK) {
    return OSAI_ERR_INVALID;
  }
  klog("service-supervisor: observed crash %s code=%u parent=%s\n",
       service->name, (unsigned)exit_code,
       service->parent_name != 0 ? service->parent_name : "(root)");
  return mark_service_exit(service, exit_code, 1);
}

static osai_status_t handle_define(const char *service_name,
                                   const char *parent_token,
                                   const char *restart_token) {
  const char *parent = 0;
  const char *restart = 0;
  osai_service_t *parent_service = 0;
  if (!str_eq(service_name, k_child_service_name) ||
      parse_key_value(parent_token, "parent", &parent) != OSAI_OK ||
      parse_key_value(restart_token, "restart", &restart) != OSAI_OK ||
      !str_eq(parent, k_child_parent_name) ||
      (!str_eq(restart, k_policy_never) && !str_eq(restart, k_policy_always))) {
    return OSAI_ERR_INVALID;
  }
  parent_service = find_service(parent);
  if (parent_service == 0) {
    return OSAI_ERR_INVALID;
  }

  reset_service(&g_child_service, k_child_service_name);
  g_child_service.parent_name = k_child_parent_name;
  g_child_service.restart_policy =
      str_eq(restart, k_policy_always) ? k_policy_always : k_policy_never;
  ++g_child_descriptor_count;
  ++g_service_tree_edge_count;
  ++parent_service->child_count;
  klog("service-manager: defined child %s parent=%s restart=%s descriptors=%lu\n",
       g_child_service.name, k_child_parent_name, g_child_service.restart_policy,
       g_child_descriptor_count);
  klog("service-supervisor: tree parent=%s child=%s children=%lu edges=%lu\n",
       parent_service->name, g_child_service.name, parent_service->child_count,
       g_service_tree_edge_count);
  return OSAI_OK;
}

static osai_status_t tokenize_command(char *command, uint32_t *argc,
                                     const char *tokens[OSAI_OSCTL_MAX_TOKENS]) {
  uint32_t count = 0;
  char *cursor = command;
  while (*cursor != '\0') {
    while (*cursor == ' ' || *cursor == '\t') {
      ++cursor;
    }
    if (*cursor == '\0') {
      break;
    }
    if (count >= OSAI_OSCTL_MAX_TOKENS) {
      return OSAI_ERR_INVALID;
    }

    tokens[count++] = cursor;

    while (*cursor != '\0' && *cursor != ' ' && *cursor != '\t') {
      ++cursor;
    }
    if (*cursor == ' ' || *cursor == '\t') {
      *cursor = '\0';
      ++cursor;
    }
  }

  if (count == 0) {
    return OSAI_ERR_INVALID;
  }
  if (argc != 0) {
    argc[0] = count;
  }
  return OSAI_OK;
}

void service_supervisor_init(void) {
  reset_service(&g_init_service, k_init_service_name);
  reset_service(&g_manager_service, k_manager_service_name);
  reset_service(&g_child_service, 0);
  g_child_descriptor_count = 0;
  g_service_tree_edge_count = 0;
  g_service_transition_count = 0;
  g_service_restart_count = 0;
  g_service_crash_count = 0;
  g_service_cleanup_count = 0;
  g_service_log_record_count = 0;
  service_snapshot_capture(&g_init_service);
  klog("service: supervisor initialized\n");
}

osai_status_t service_start_init(void) {
  return service_start(k_init_service_name);
}

osai_status_t service_status(const char *name) {
  return handle_status(name);
}

osai_status_t service_start(const char *name) {
  osai_service_t *service = find_service(name);
  if (service == 0) {
    return OSAI_ERR_INVALID;
  }
  return start_service(service);
}

osai_status_t service_stop(const char *name) {
  return handle_stop(name);
}

osai_status_t service_restart(const char *name) {
  return handle_restart(name);
}

osai_status_t service_rollback(const char *name) {
  return handle_rollback(name);
}

osai_status_t service_update(const char *signature) {
  return handle_update(signature);
}

osai_status_t service_exit(const char *name, int exit_code) {
  osai_service_t *service = find_service(name);
  return mark_service_exit(service, exit_code, 0);
}

osai_status_t osctl_execute(const char *command) {
  if (command == 0 || command[0] == '\0') {
    klog("service: osctl rejected command: empty\n");
    return OSAI_ERR_INVALID;
  }

  char copy[OSAI_CMD_TOKEN_BUFFER];
  for (uint32_t i = 0; i < OSAI_CMD_TOKEN_BUFFER; ++i) {
    copy[i] = '\0';
  }
  uint32_t i = 0;
  for (; i < OSAI_CMD_TOKEN_BUFFER; ++i) {
    copy[i] = command[i];
    if (command[i] == '\0') {
      break;
    }
  }
  if (i == OSAI_CMD_TOKEN_BUFFER) {
    klog("service: osctl command too long\n");
    return OSAI_ERR_INVALID;
  }

  const char *tokens[OSAI_OSCTL_MAX_TOKENS];
  for (uint32_t j = 0; j < OSAI_OSCTL_MAX_TOKENS; ++j) {
    tokens[j] = 0;
  }
  uint32_t argc = 0;
  if (tokenize_command(copy, &argc, tokens) != OSAI_OK || argc < 3U) {
    klog("service: osctl parse failed argc=%lu command='%s'\n",
         (unsigned long)argc, copy);
    return OSAI_ERR_INVALID;
  }

  if (!token_safe(tokens[0]) || !str_eq(tokens[0], "service")) {
    klog("service: osctl expected 'service' got token0='%s'\n", tokens[0]);
    return OSAI_ERR_INVALID;
  }
  if (!token_safe(tokens[1]) || !token_safe(tokens[2])) {
    klog("service: osctl invalid token token1='%s' token2='%s'\n", tokens[1],
         tokens[2]);
    return OSAI_ERR_INVALID;
  }

  const char *action = tokens[1];
  const char *service = tokens[2];
  if (tokens[1][0] == '/') {
    action = tokens[2];
    service = tokens[1];
  }

  if (!token_safe(action)) {
    return OSAI_ERR_INVALID;
  }

  if (str_eq(action, "define") && argc == 5U) {
    return handle_define(service, tokens[3], tokens[4]);
  }
  if (str_eq(action, "status") && argc == 3U) {
    return handle_status(service);
  }
  if (str_eq(action, "configure") && argc == 6U) {
    klog("service: osctl command argc=%lu token3=%s token4=%s token5=%s\n",
         (unsigned long)argc, tokens[3], tokens[4], tokens[5]);
    return configure_service(service, tokens[3], tokens[4], tokens[5]);
  }
  if (str_eq(action, "log") && argc == 4U) {
    return handle_log(service, tokens[3]);
  }
  if (str_eq(action, "restart") && argc == 3U) {
    return handle_restart(service);
  }
  if (str_eq(action, "crash") && argc == 4U) {
    return handle_crash(service, tokens[3]);
  }
  if (str_eq(action, "start") && argc == 3U) {
    return handle_start(service);
  }
  if (str_eq(action, "stop") && argc == 3U) {
    return handle_stop(service);
  }
  if (str_eq(action, "rollback") && argc == 3U) {
    return handle_rollback(service);
  }
  if (str_eq(action, "update") && argc == 4U) {
    return handle_update(tokens[3]);
  }

  klog("service: osctl unsupported command name='%s' argc=%lu\n", action,
       (unsigned long)argc);
  return OSAI_ERR_INVALID;
}

void service_supervisor_self_test(void) {
  service_supervisor_init();
  kassert(osctl_execute("service status /init") == OSAI_OK);
  kassert(osctl_execute(
             "service configure /init restart=never log=serial max_restarts=0") ==
         OSAI_OK);
  kassert(osctl_execute("service log /init manager-ready") == OSAI_OK);
  kassert(osctl_execute("service restart /init") == OSAI_ERR_INVALID);
  kassert(osctl_execute("service start /init") == OSAI_OK);
  kassert(osctl_execute("service status /init") == OSAI_OK);
  kassert(osctl_execute(
              "service define /svc/source-index parent=/init restart=never") ==
          OSAI_OK);
  kassert(osctl_execute("service start /svc/source-index") == OSAI_OK);
  kassert(osctl_execute("service status /svc/source-index") == OSAI_OK);
  kassert(osctl_execute(
              "service configure /svc/source-index restart=always log=serial max_restarts=2") ==
          OSAI_OK);
  kassert(osctl_execute("service log /svc/source-index crash-test") == OSAI_OK);
  kassert(osctl_execute("service crash /svc/source-index code=7") == OSAI_OK);
  kassert(osctl_execute("service status /svc/source-index") == OSAI_OK);
  kassert(service_exit("/init", 0) == OSAI_OK);
  kassert(osctl_execute("service status /init") == OSAI_OK);
  kassert(osctl_execute("service rollback /init") == OSAI_ERR_INVALID);
  kassert(osctl_execute("service destroy /init") == OSAI_ERR_INVALID);
  kassert(osctl_execute("service update /init test") == OSAI_ERR_INVALID);
  kassert(service_tree_edge_count() == 1);
  kassert(service_restart_count() == 1);
  kassert(service_crash_count() == 1);
  kassert(service_cleanup_count() >= 1);
  kassert(service_log_record_count() >= 2);
  klog("service: supervisor self-test passed\n");
}

uint64_t service_child_descriptor_count(void) {
  return g_child_descriptor_count;
}

uint64_t service_tree_edge_count(void) {
  return g_service_tree_edge_count;
}

uint64_t service_transition_count(void) {
  return g_service_transition_count;
}

uint64_t service_restart_count(void) {
  return g_service_restart_count;
}

uint64_t service_crash_count(void) {
  return g_service_crash_count;
}

uint64_t service_cleanup_count(void) {
  return g_service_cleanup_count;
}

uint64_t service_log_record_count(void) {
  return g_service_log_record_count;
}
