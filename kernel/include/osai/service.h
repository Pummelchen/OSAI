#ifndef OSAI_SERVICE_H
#define OSAI_SERVICE_H

#include <osai/status.h>
#include <osai/types.h>

typedef enum osai_service_state {
  OSAI_SERVICE_STOPPED = 0,
  OSAI_SERVICE_STARTING = 1,
  OSAI_SERVICE_RUNNING = 2,
  OSAI_SERVICE_EXITED = 3,
  OSAI_SERVICE_FAILED = 4,
} osai_service_state_t;

#define OSAI_CRASH_DUMP_MAX 8U
#define OSAI_BACKOFF_BASE_NS UINT64_C(1000000000)
#define OSAI_BACKOFF_CAP_NS UINT64_C(60000000000)
#define OSAI_WATCHDOG_TIMEOUT_NS UINT64_C(30000000000)

typedef struct osai_crash_record {
  const char *service_name;
  int exit_code;
  uint64_t crash_timestamp_ns;
  uint64_t restart_count;
  uint64_t uptime_ns;
} osai_crash_record_t;

typedef struct osai_service {
  const char *name;
  const char *parent_name;
  const char *restart_policy;
  const char *log_policy;
  uint32_t max_restarts;
  osai_service_state_t state;
  int exit_code;
  uint64_t starts;
  uint64_t restart_attempts;
  uint64_t log_records;
  uint64_t child_count;
  uint64_t crash_count;
  uint64_t cleanup_count;
  uint64_t update_attempts;
  uint64_t update_rejections;
  uint64_t rollback_count;

  /* Tier 2: backoff, heartbeat, watchdog */
  uint64_t backoff_ns;
  uint64_t last_start_ns;
  uint64_t last_heartbeat_ns;
  uint32_t watchdog_enabled;

  char restart_policy_snapshot[16];
  char log_policy_snapshot[16];
  uint32_t max_restarts_snapshot;
  uint64_t starts_snapshot;
  uint64_t restart_attempts_snapshot;
  uint64_t log_records_snapshot;
} osai_service_t;

void service_supervisor_init(void);
osai_status_t service_start_init(void);
osai_status_t service_status(const char *name);
osai_status_t service_start(const char *name);
osai_status_t service_stop(const char *name);
osai_status_t service_restart(const char *name);
osai_status_t service_rollback(const char *name);
osai_status_t service_update(const char *signature);
osai_status_t service_exit(const char *name, int exit_code);
osai_status_t osctl_execute(const char *command);
void service_supervisor_self_test(void);
osai_status_t service_heartbeat(const char *name);
void service_watchdog_check(void);
uint32_t service_crash_dump_count(void);
const osai_crash_record_t *service_crash_dump_get(uint32_t index);
uint64_t service_child_descriptor_count(void);
uint64_t service_tree_edge_count(void);
uint64_t service_transition_count(void);
uint64_t service_restart_count(void);
uint64_t service_crash_count(void);
uint64_t service_cleanup_count(void);
uint64_t service_log_record_count(void);
uint64_t service_admin_policy_export_count(void);
uint64_t service_admin_status_export_count(void);
uint64_t service_admin_log_read_count(void);
uint64_t service_admin_remote_safe_accept_count(void);
uint64_t service_admin_remote_safe_reject_count(void);
uint64_t service_admin_command_denial_count(void);

#endif
