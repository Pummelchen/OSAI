#include <osai/assert.h>
#include <osai/klog.h>
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

static osai_service_t g_init_service;

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

static void service_snapshot_capture(void) {
  copy_str(g_init_service.restart_policy_snapshot, g_init_service.restart_policy);
  copy_str(g_init_service.log_policy_snapshot, g_init_service.log_policy);
  g_init_service.max_restarts_snapshot = g_init_service.max_restarts;
  g_init_service.starts_snapshot = g_init_service.starts;
  g_init_service.restart_attempts_snapshot = g_init_service.restart_attempts;
  g_init_service.log_records_snapshot = g_init_service.log_records;
}

static void service_snapshot_restore(void) {
  g_init_service.restart_policy = g_init_service.restart_policy_snapshot;
  g_init_service.log_policy = g_init_service.log_policy_snapshot;

  if (g_init_service.restart_policy[0] == '\0') {
    g_init_service.restart_policy = k_policy_default;
  }
  if (g_init_service.log_policy[0] == '\0') {
    g_init_service.log_policy = k_log_off;
  }

  g_init_service.max_restarts = g_init_service.max_restarts_snapshot;
  g_init_service.starts = g_init_service.starts_snapshot;
  g_init_service.restart_attempts = g_init_service.restart_attempts_snapshot;
  g_init_service.log_records = g_init_service.log_records_snapshot;
}

static osai_status_t set_restart_policy(const char *value) {
  if (str_eq(value, k_policy_never)) {
    g_init_service.restart_policy = k_policy_never;
    return OSAI_OK;
  }

  if (str_eq(value, k_policy_always)) {
    g_init_service.restart_policy = k_policy_always;
    return OSAI_OK;
  }

  return OSAI_ERR_INVALID;
}

static osai_status_t set_log_policy(const char *value) {
  if (str_eq(value, k_log_serial)) {
    g_init_service.log_policy = k_log_serial;
    return OSAI_OK;
  }

  if (str_eq(value, k_log_off)) {
    g_init_service.log_policy = k_log_off;
    return OSAI_OK;
  }

  return OSAI_ERR_INVALID;
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
    klog("service-manager: invalid restart token='%s'\n", token);
    return OSAI_ERR_INVALID;
  }
  if (set_restart_policy(value) != OSAI_OK) {
    return OSAI_ERR_INVALID;
  }
  config->restart_policy = value;
  config->seen_fields |= 1U;
  return OSAI_OK;
}

static osai_status_t parse_log_token(const char *token, service_config_t *config) {
  const char *value = 0;
  if (parse_key_value(token, "log", &value) != OSAI_OK) {
    klog("service-manager: invalid log token='%s'\n", token);
    return OSAI_ERR_INVALID;
  }
  if (set_log_policy(value) != OSAI_OK) {
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
    klog("service-manager: invalid max_restarts token='%s'\n", token);
    return OSAI_ERR_INVALID;
  }
  if (parse_u32(value, &config->max_restarts) != OSAI_OK) {
    klog("service-manager: invalid max_restarts value='%s'\n", value);
    return OSAI_ERR_INVALID;
  }
  config->seen_fields |= 4U;
  return OSAI_OK;
}

static osai_status_t apply_service_config(const service_config_t *config) {
  if (config == 0 ||
      (config->seen_fields & 7U) != 7U ||
      config->restart_policy == 0 ||
      config->log_policy == 0) {
    return OSAI_ERR_INVALID;
  }

  g_init_service.restart_policy = k_policy_default;
  g_init_service.log_policy = k_log_off;
  g_init_service.max_restarts = config->max_restarts;

  if (str_eq(config->restart_policy, k_policy_never)) {
    g_init_service.restart_policy = k_policy_never;
  } else if (str_eq(config->restart_policy, k_policy_always)) {
    g_init_service.restart_policy = k_policy_always;
  } else {
    return OSAI_ERR_INVALID;
  }

  if (str_eq(config->log_policy, k_log_serial)) {
    g_init_service.log_policy = k_log_serial;
  } else if (str_eq(config->log_policy, k_log_off)) {
    g_init_service.log_policy = k_log_off;
  } else {
    return OSAI_ERR_INVALID;
  }

  service_snapshot_capture();
  klog(
      "service-manager: configured /init restart=%s log=%s max_restarts=%lu\n",
      g_init_service.restart_policy, g_init_service.log_policy,
      (unsigned long)g_init_service.max_restarts);
  return OSAI_OK;
}

static osai_status_t configure_service(const char *token3, const char *token4,
                                      const char *token5) {
  service_config_t config;
  config.restart_policy = 0;
  config.log_policy = 0;
  config.max_restarts = 0;
  config.seen_fields = 0;

  if (token3 == 0 || token4 == 0 || token5 == 0) {
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

  return apply_service_config(&config);
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
  if (!str_eq(service_name, g_init_service.name)) {
    return OSAI_ERR_INVALID;
  }
  klog(
      "osctl: /init state=%s starts=%lu restarts=%lu logs=%lu restart_policy=%s "
      "log_policy=%s max_restarts=%lu exit_code=%u\n",
      service_state_name(g_init_service.state), g_init_service.starts,
      g_init_service.restart_attempts, g_init_service.log_records,
      g_init_service.restart_policy, g_init_service.log_policy,
      (unsigned long)g_init_service.max_restarts, (unsigned)g_init_service.exit_code);
  return OSAI_OK;
}

static osai_status_t handle_log(const char *service_name, const char *message) {
  if (!str_eq(service_name, g_init_service.name) || !token_safe(message)) {
    return OSAI_ERR_INVALID;
  }
  ++g_init_service.log_records;
  klog("service-manager: log %s %s records=%lu\n", service_name, message,
       g_init_service.log_records);
  return OSAI_OK;
}

static osai_status_t handle_restart(const char *service_name) {
  if (!str_eq(service_name, g_init_service.name)) {
    return OSAI_ERR_INVALID;
  }

  ++g_init_service.restart_attempts;
  if (str_eq(g_init_service.restart_policy, k_policy_never)) {
    klog("service-manager: restart denied /init policy=%s attempts=%lu\n",
         g_init_service.restart_policy, g_init_service.restart_attempts);
    return OSAI_ERR_INVALID;
  }

  if (g_init_service.max_restarts != UINT32_C(0xffffffff) &&
      g_init_service.restart_attempts > g_init_service.max_restarts) {
    klog("service-manager: restart denied /init max_restarts=%lu attempts=%lu\n",
         (unsigned long)g_init_service.max_restarts,
         g_init_service.restart_attempts);
    return OSAI_ERR_INVALID;
  }

  g_init_service.state = OSAI_SERVICE_STOPPED;
  klog("service-manager: restart allowed /init attempts=%lu\n",
       g_init_service.restart_attempts);
  return OSAI_OK;
}

static osai_status_t handle_start(const char *service_name) {
  if (!str_eq(service_name, g_init_service.name)) {
    return OSAI_ERR_INVALID;
  }
  return service_start_init();
}

static osai_status_t handle_update(const char *signature) {
  if (signature == 0 || signature[0] == '\0') {
    return OSAI_ERR_INVALID;
  }
  if (require_service_capability(OSAI_CAP_UPDATE) != OSAI_OK) {
    ++g_init_service.update_rejections;
    return OSAI_ERR_INVALID;
  }

  if (token_safe(signature) == 0) {
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
    return OSAI_ERR_INVALID;
  }

  service_snapshot_restore();
  ++g_init_service.rollback_count;
  klog("service-manager: rollback /init restart=%s log=%s max_restarts=%lu\n",
       g_init_service.restart_policy, g_init_service.log_policy,
       (unsigned long)g_init_service.max_restarts);
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
  g_init_service.name = "/init";
  g_init_service.restart_policy = k_policy_default;
  g_init_service.log_policy = k_log_off;
  g_init_service.max_restarts = UINT32_C(0xffffffff);
  g_init_service.state = OSAI_SERVICE_STOPPED;
  g_init_service.exit_code = 0;
  g_init_service.starts = 0;
  g_init_service.restart_attempts = 0;
  g_init_service.log_records = 0;
  g_init_service.update_attempts = 0;
  g_init_service.update_rejections = 0;
  g_init_service.rollback_count = 0;
  service_snapshot_capture();
  klog("service: supervisor initialized\n");
}

osai_status_t service_start_init(void) {
  if (g_init_service.state != OSAI_SERVICE_STOPPED &&
      g_init_service.state != OSAI_SERVICE_EXITED) {
    return OSAI_ERR_INVALID;
  }

  if (g_init_service.state == OSAI_SERVICE_EXITED &&
      g_init_service.max_restarts != UINT32_C(0xffffffff) &&
      g_init_service.starts >= g_init_service.max_restarts) {
    return OSAI_ERR_INVALID;
  }

  g_init_service.state = OSAI_SERVICE_STARTING;
  ++g_init_service.starts;
  klog("service: /init state=starting\n");
  g_init_service.state = OSAI_SERVICE_RUNNING;
  klog("service: /init state=running\n");
  return OSAI_OK;
}

osai_status_t service_exit(const char *name, int exit_code) {
  if (!str_eq(name, g_init_service.name)) {
    return OSAI_ERR_INVALID;
  }
  if (g_init_service.state != OSAI_SERVICE_RUNNING) {
    return OSAI_ERR_INVALID;
  }

  g_init_service.exit_code = exit_code;
  g_init_service.state =
      exit_code == 0 ? OSAI_SERVICE_EXITED : OSAI_SERVICE_FAILED;
  klog("service: %s state=%s exit_code=%u\n", name,
       service_state_name(g_init_service.state), (unsigned)exit_code);
  return OSAI_OK;
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

  if (!token_safe(action) || !str_eq(service, g_init_service.name)) {
    klog("service: osctl wrong target service='%s' expected='%s' action='%s'\n",
         service, g_init_service.name, action);
    return OSAI_ERR_INVALID;
  }

  if (str_eq(action, "status") && argc == 3U) {
    return handle_status(service);
  }
  if (str_eq(action, "configure") && argc == 6U) {
    klog("service: osctl command argc=%lu token3=%s token4=%s token5=%s\n",
         (unsigned long)argc, tokens[3], tokens[4], tokens[5]);
    return configure_service(tokens[3], tokens[4], tokens[5]);
  }
  if (str_eq(action, "log") && argc == 4U) {
    return handle_log(service, tokens[3]);
  }
  if (str_eq(action, "restart") && argc == 3U) {
    return handle_restart(service);
  }
  if (str_eq(action, "start") && argc == 3U) {
    return handle_start(service);
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
  kassert(service_exit("/init", 0) == OSAI_OK);
  kassert(osctl_execute("service status /init") == OSAI_OK);
  kassert(osctl_execute("service rollback /init") == OSAI_ERR_INVALID);
  kassert(osctl_execute("service destroy /init") == OSAI_ERR_INVALID);
  kassert(osctl_execute("service update /init test") == OSAI_ERR_INVALID);
  klog("service: supervisor self-test passed\n");
}
