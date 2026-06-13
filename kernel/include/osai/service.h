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

typedef struct osai_service {
  const char *name;
  const char *restart_policy;
  const char *log_policy;
  uint32_t max_restarts;
  osai_service_state_t state;
  int exit_code;
  uint64_t starts;
  uint64_t restart_attempts;
  uint64_t log_records;
  uint64_t update_attempts;
  uint64_t update_rejections;
  uint64_t rollback_count;

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
uint64_t service_child_descriptor_count(void);
uint64_t service_transition_count(void);

#endif
