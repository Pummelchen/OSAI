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
  osai_service_state_t state;
  int exit_code;
  uint64_t starts;
} osai_service_t;

void service_supervisor_init(void);
osai_status_t service_start_init(void);
osai_status_t service_exit(const char *name, int exit_code);
osai_status_t osctl_execute(const char *command);
void service_supervisor_self_test(void);

#endif
