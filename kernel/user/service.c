#include <osai/assert.h>
#include <osai/klog.h>
#include <osai/service.h>

static osai_service_t g_init_service;

static int str_eq(const char *a, const char *b) {
  while (*a != '\0' && *b != '\0') {
    if (*a != *b) {
      return 0;
    }
    ++a;
    ++b;
  }
  return *a == '\0' && *b == '\0';
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
  }

  return "unknown";
}

void service_supervisor_init(void) {
  g_init_service.name = "/init";
  g_init_service.restart_policy = "unset";
  g_init_service.log_policy = "unset";
  g_init_service.state = OSAI_SERVICE_STOPPED;
  g_init_service.exit_code = 0;
  g_init_service.starts = 0;
  g_init_service.restart_attempts = 0;
  g_init_service.log_records = 0;
  klog("service: supervisor initialized\n");
}

osai_status_t service_start_init(void) {
  if (g_init_service.state != OSAI_SERVICE_STOPPED &&
      g_init_service.state != OSAI_SERVICE_EXITED) {
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
  g_init_service.state = exit_code == 0 ? OSAI_SERVICE_EXITED : OSAI_SERVICE_FAILED;
  klog("service: %s state=%s exit_code=%u\n", name,
       service_state_name(g_init_service.state), (unsigned)exit_code);
  return OSAI_OK;
}

osai_status_t osctl_execute(const char *command) {
  if (str_eq(command, "service status /init")) {
    klog("osctl: /init state=%s starts=%lu restarts=%lu logs=%lu restart_policy=%s log_policy=%s exit_code=%u\n",
         service_state_name(g_init_service.state), g_init_service.starts,
         g_init_service.restart_attempts, g_init_service.log_records,
         g_init_service.restart_policy, g_init_service.log_policy,
         (unsigned)g_init_service.exit_code);
    return OSAI_OK;
  }
  if (str_eq(command,
             "service configure /init restart=never log=serial max_restarts=0")) {
    g_init_service.restart_policy = "never";
    g_init_service.log_policy = "serial";
    klog("service-manager: configured /init restart=never log=serial max_restarts=0\n");
    return OSAI_OK;
  }
  if (str_eq(command, "service log /init manager-ready")) {
    ++g_init_service.log_records;
    klog("service-manager: log /init manager-ready records=%lu\n",
         g_init_service.log_records);
    return OSAI_OK;
  }
  if (str_eq(command, "service restart /init")) {
    ++g_init_service.restart_attempts;
    klog("service-manager: restart denied /init policy=%s attempts=%lu\n",
         g_init_service.restart_policy, g_init_service.restart_attempts);
    return OSAI_ERR_INVALID;
  }
  if (str_eq(command, "service start /init")) {
    return service_start_init();
  }

  klog("osctl: rejected command='%s'\n", command);
  return OSAI_ERR_INVALID;
}

void service_supervisor_self_test(void) {
  service_supervisor_init();
  kassert(osctl_execute("service status /init") == OSAI_OK);
  kassert(osctl_execute("service configure /init restart=never log=serial max_restarts=0") == OSAI_OK);
  kassert(osctl_execute("service log /init manager-ready") == OSAI_OK);
  kassert(osctl_execute("service restart /init") == OSAI_ERR_INVALID);
  kassert(osctl_execute("service start /init") == OSAI_OK);
  kassert(osctl_execute("service status /init") == OSAI_OK);
  kassert(service_exit("/init", 0) == OSAI_OK);
  kassert(osctl_execute("service status /init") == OSAI_OK);
  kassert(osctl_execute("service destroy /init") == OSAI_ERR_INVALID);
  klog("service: supervisor self-test passed\n");
}
