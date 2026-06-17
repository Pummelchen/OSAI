#include <osai/agent_protocol.h>
#include <osai/arena.h>
#include <osai/assert.h>
#include <osai/cpu_ai_runtime.h>
#include <osai/initramfs.h>
#include <osai/klog.h>
#include <osai/mutable_fs.h>
#include <osai/network_stack.h>
#include <osai/remote_login.h>
#include <osai/security.h>
#include <osai/service.h>
#include <osai/smp.h>
#include <osai/syscall.h>
#include <osai/timer.h>
#include <osai/user.h>
#include <osai/vmm.h>

typedef struct osai_syscall_entry {
  uint64_t number;
  const char *name;
  uint64_t required_capability;
} osai_syscall_entry_t;

static const osai_syscall_entry_t g_syscall_table[] = {
    {OSAI_SYSCALL_LOG, "log", OSAI_CAP_LOG},
    {OSAI_SYSCALL_EXIT, "exit", OSAI_CAP_EXIT},
    {OSAI_SYSCALL_OSCTL, "osctl", OSAI_CAP_OSCTL},
    {OSAI_SYSCALL_READ_SERVICE_DESCRIPTOR, "read_service_descriptor",
     OSAI_CAP_FS_READ},
    {OSAI_SYSCALL_SERVICE_STATUS, "service_status",
     OSAI_CAP_SERVICE_CONTROL},
    {OSAI_SYSCALL_SERVICE_START, "service_start", OSAI_CAP_SERVICE_CONTROL},
    {OSAI_SYSCALL_SERVICE_STOP, "service_stop", OSAI_CAP_SERVICE_CONTROL},
    {OSAI_SYSCALL_SERVICE_RESTART, "service_restart",
     OSAI_CAP_SERVICE_CONTROL},
    {OSAI_SYSCALL_SERVICE_ROLLBACK, "service_rollback",
     OSAI_CAP_SERVICE_ROLLBACK},
    {OSAI_SYSCALL_SERVICE_UPDATE, "service_update", OSAI_CAP_UPDATE},
    {OSAI_SYSCALL_FS_OPEN, "fs_open", OSAI_CAP_FS_READ},
    {OSAI_SYSCALL_FS_READ, "fs_read", OSAI_CAP_FS_READ},
    {OSAI_SYSCALL_FS_WRITE, "fs_write", OSAI_CAP_FS_WRITE},
    {OSAI_SYSCALL_FS_CLOSE, "fs_close", OSAI_CAP_FS_READ},
    {OSAI_SYSCALL_FS_STAT, "fs_stat", OSAI_CAP_FS_READ},
    {OSAI_SYSCALL_FS_MKDIR, "fs_mkdir", OSAI_CAP_FS_WRITE},
    {OSAI_SYSCALL_FS_DELETE, "fs_delete", OSAI_CAP_FS_WRITE},
    {OSAI_SYSCALL_FS_RENAME, "fs_rename", OSAI_CAP_FS_WRITE},
    {OSAI_SYSCALL_FS_LIST, "fs_list", OSAI_CAP_FS_READ},
    {OSAI_SYSCALL_CLOCK_NANOS, "clock_nanos", OSAI_CAP_TIME},
    {OSAI_SYSCALL_NET_UDP_ECHO, "net_udp_echo", OSAI_CAP_NET},
    {OSAI_SYSCALL_NET_TCP_CONNECT, "net_tcp_connect", OSAI_CAP_NET},
    {OSAI_SYSCALL_SMP_RUN, "smp_run", OSAI_CAP_SMP},
    {OSAI_SYSCALL_CPU_AI_DECODE, "cpu_ai_decode", OSAI_CAP_CPU_AI},
    {OSAI_SYSCALL_REMOTE_LOGIN, "remote_login", OSAI_CAP_REMOTE_LOGIN},
    {OSAI_SYSCALL_NET_EXTERNAL_SESSION, "net_external_session", OSAI_CAP_NET},
    {OSAI_SYSCALL_THREAD_GROUP_RUN, "thread_group_run", OSAI_CAP_THREADS},
    {OSAI_SYSCALL_ML_RUN, "ml_run", OSAI_CAP_ML},
    {OSAI_SYSCALL_NET_LISTEN, "net_listen", OSAI_CAP_NET_SOCKET},
    {OSAI_SYSCALL_NET_ACCEPT, "net_accept", OSAI_CAP_NET_SOCKET},
    {OSAI_SYSCALL_NET_RECV, "net_recv", OSAI_CAP_NET_SOCKET},
    {OSAI_SYSCALL_NET_SEND, "net_send", OSAI_CAP_NET_SOCKET},
    {OSAI_SYSCALL_NET_CLOSE, "net_close", OSAI_CAP_NET_SOCKET},
    {OSAI_SYSCALL_AGENT_DISPATCH, "agent_dispatch", OSAI_CAP_AGENT},
};

static uint64_t g_control_plane_syscall_count;
static uint64_t g_control_plane_denial_count;
static uint64_t g_service_descriptor_read_count;
static uint32_t g_cpu_ai_app_bound;

/* ---- Kernel socket table (for sshd) ---- */
#define KERNEL_SOCK_LISTEN UINT32_C(1)
#define KERNEL_SOCK_CONNECTED UINT32_C(2)
#define KERNEL_SOCK_MAX UINT32_C(16)

typedef struct kernel_socket {
  uint32_t state;   /* 0=free, KERNEL_SOCK_LISTEN, KERNEL_SOCK_CONNECTED */
  uint16_t port;
} kernel_socket_t;

static kernel_socket_t g_kernel_sockets[KERNEL_SOCK_MAX];
static uint64_t g_socket_next_id = 1;

static uint64_t kernel_socket_alloc(uint32_t type, uint16_t port) {
  for (uint32_t i = 0; i < KERNEL_SOCK_MAX; ++i) {
    if (g_kernel_sockets[i].state == 0) {
      g_kernel_sockets[i].state = type;
      g_kernel_sockets[i].port = port;
      return g_socket_next_id++;
    }
  }
  return 0; /* no free slots */
}

static void kernel_socket_free(uint64_t sockfd) {
  /* Find by ID heuristic: free the most recent match or just mark first match free */
  (void)sockfd;
  for (uint32_t i = 0; i < KERNEL_SOCK_MAX; ++i) {
    if (g_kernel_sockets[i].state != 0) {
      g_kernel_sockets[i].state = 0;
      g_kernel_sockets[i].port = 0;
      return;
    }
  }
}

static const osai_syscall_entry_t *lookup_syscall(uint64_t number) {
  for (uint32_t i = 0; i < sizeof(g_syscall_table) / sizeof(g_syscall_table[0]);
       ++i) {
    if (g_syscall_table[i].number == number) {
      return &g_syscall_table[i];
    }
  }
  return 0;
}

static osai_status_t copy_user_string(uint64_t user_ptr, uint64_t length,
                                      char *buffer, uint64_t buffer_size) {
  if (length == 0 || length >= buffer_size ||
      vmm_validate_user_buffer(user_ptr, length, 0) != OSAI_OK) {
    return OSAI_ERR_INVALID;
  }
  const char *src = (const char *)(uintptr_t)user_ptr;
  for (uint64_t i = 0; i < length; ++i) {
    buffer[i] = src[i];
  }
  buffer[length] = '\0';
  return OSAI_OK;
}

static void bytes_copy(void *dst, const void *src, uint64_t size) {
  uint8_t *out = (uint8_t *)dst;
  const uint8_t *in = (const uint8_t *)src;
  for (uint64_t i = 0; i < size; ++i) {
    out[i] = in[i];
  }
}

static int is_control_plane_syscall(uint64_t syscall) {
  return syscall == OSAI_SYSCALL_OSCTL ||
         (syscall >= OSAI_SYSCALL_READ_SERVICE_DESCRIPTOR &&
          syscall <= OSAI_SYSCALL_AGENT_DISPATCH);
}

static uint64_t reject_syscall(uint64_t syscall, uint64_t arg0, uint64_t arg1,
                               const char *reason) {
  user_process_note_syscall(1);
  if (is_control_plane_syscall(syscall)) {
    ++g_control_plane_denial_count;
  }
  klog("user: rejected syscall=%lu arg0=0x%lx arg1=0x%lx reason=%s\n",
       syscall, arg0, arg1, reason);
  return UINT64_C(-1);
}

static uint64_t complete_control_syscall(uint64_t value) {
  ++g_control_plane_syscall_count;
  user_process_note_syscall(0);
  return value;
}

static osai_status_t ensure_app_cpu_ai_binding(void) {
  if (g_cpu_ai_app_bound != 0) {
    return OSAI_OK;
  }

  const osai_arena_t *kv = 0;
  osai_status_t status =
      arena_create(30, OSAI_ARENA_KV_CACHE, 3, "cpu-ai-user-kv", 4096, 0,
                   &kv);
  if (status != OSAI_OK || kv == 0) {
    return status;
  }
  status = cpu_ai_runtime_bind_model_with_kv(3, 2, kv->base, kv->size);
  if (status != OSAI_OK) {
    (void)arena_destroy(30);
    return status;
  }
  g_cpu_ai_app_bound = 1;
  return OSAI_OK;
}

uint64_t syscall_dispatch(uint64_t syscall, uint64_t arg0, uint64_t arg1,
                          uint64_t arg2) {
  (void)arg2;

  const osai_syscall_entry_t *entry = lookup_syscall(syscall);
  if (entry == 0) {
    return reject_syscall(syscall, arg0, arg1, "unknown");
  }
  if (user_process_has_capability(entry->required_capability) != OSAI_OK) {
    const osai_user_process_t *process = user_current_process();
    uint64_t granted = process != 0 ? process->capability_mask : 0;
    (void)security_authorize_capability(entry->name, granted,
                                        entry->required_capability);
    return reject_syscall(syscall, arg0, arg1, "missing-capability");
  }

  if (syscall == OSAI_SYSCALL_LOG) {
    if (vmm_validate_user_buffer(arg0, arg1, 0) != OSAI_OK) {
      return reject_syscall(syscall, arg0, arg1, "bad-user-buffer");
    }
    const char *log_src = (const char *)(uintptr_t)arg0;
    if (security_reject_credential_material_buffer(log_src, arg1) !=
        OSAI_OK) {
      return reject_syscall(syscall, arg0, arg1, "log-secret-denied");
    }
    user_process_note_syscall(0);
    klog_write((const char *)(uintptr_t)arg0, arg1);
    return 0;
  }

  if (syscall == OSAI_SYSCALL_EXIT) {
    const osai_user_process_t *process = user_current_process();
    const char *name = process != 0 && process->name != 0 ? process->name : "";
    if (service_exit(name, (int)arg0) != OSAI_OK) {
      klog("user: %s exit without service record status=%u\n",
           name, (unsigned)arg0);
    }
    user_process_note_syscall(0);
    klog("user: %s exited status=%u syscalls=%lu rejected=%lu\n",
         name, (unsigned)arg0, process != 0 ? process->syscall_count : 0,
         process != 0 ? process->rejected_syscall_count : 0);
    return user_process_note_exit((int)arg0);
  }

  if (syscall == OSAI_SYSCALL_CLOCK_NANOS) {
    return complete_control_syscall(timer_now_ns());
  }

  if (syscall == OSAI_SYSCALL_READ_SERVICE_DESCRIPTOR) {
    const osai_initramfs_config_t *config = initramfs_config();
    const osai_initramfs_file_t *file = 0;
    if (config == 0 ||
        security_authorize_fs_read(config->service_descriptor_path) !=
            OSAI_OK ||
        initramfs_lookup(config->service_descriptor_path, &file) != OSAI_OK ||
        file == 0 || file->base == 0 || file->size == 0 ||
        arg1 < file->size ||
        vmm_validate_user_buffer(arg0, file->size, OSAI_VMM_WRITABLE) !=
            OSAI_OK) {
      return reject_syscall(syscall, arg0, arg1, "descriptor-read-denied");
    }
    bytes_copy((void *)(uintptr_t)arg0, file->base, file->size);
    ++g_service_descriptor_read_count;
    klog("user: service descriptor read path=%s bytes=%lu\n",
         config->service_descriptor_path, file->size);
    return complete_control_syscall(file->size);
  }

  if (syscall == OSAI_SYSCALL_OSCTL) {
    char command[128];
    if (copy_user_string(arg0, arg1, command, sizeof(command)) != OSAI_OK) {
      return reject_syscall(syscall, arg0, arg1, "bad-user-string");
    }
    if (security_reject_credential_material(command) != OSAI_OK) {
      return reject_syscall(syscall, arg0, arg1, "osctl-secret-denied");
    }
    klog("user: osctl command='%s'\n", command);
    if (osctl_execute(command) != OSAI_OK) {
      return reject_syscall(syscall, arg0, arg1, "osctl-denied");
    }
    return complete_control_syscall(0);
  }

  if (syscall >= OSAI_SYSCALL_SERVICE_STATUS &&
      syscall <= OSAI_SYSCALL_SERVICE_ROLLBACK) {
    char service_name[64];
    if (copy_user_string(arg0, arg1, service_name, sizeof(service_name)) !=
        OSAI_OK) {
      return reject_syscall(syscall, arg0, arg1, "bad-service-name");
    }

    osai_status_t status = OSAI_ERR_INVALID;
    if (syscall == OSAI_SYSCALL_SERVICE_STATUS) {
      status = service_status(service_name);
    } else if (syscall == OSAI_SYSCALL_SERVICE_START) {
      status = service_start(service_name);
    } else if (syscall == OSAI_SYSCALL_SERVICE_STOP) {
      status = service_stop(service_name);
    } else if (syscall == OSAI_SYSCALL_SERVICE_RESTART) {
      status = service_restart(service_name);
    } else if (syscall == OSAI_SYSCALL_SERVICE_ROLLBACK) {
      status = service_rollback(service_name);
    }

    if (status != OSAI_OK) {
      return reject_syscall(syscall, arg0, arg1, "service-control-denied");
    }
    return complete_control_syscall(0);
  }

  if (syscall == OSAI_SYSCALL_SERVICE_UPDATE) {
    char signature[128];
    if (copy_user_string(arg0, arg1, signature, sizeof(signature)) != OSAI_OK) {
      return reject_syscall(syscall, arg0, arg1, "bad-update-signature");
    }
    if (service_update(signature) != OSAI_OK) {
      return reject_syscall(syscall, arg0, arg1, "update-denied");
    }
    return complete_control_syscall(0);
  }

  if (syscall == OSAI_SYSCALL_FS_OPEN) {
    char path[OSAI_MFS_PATH_MAX];
    if (copy_user_string(arg0, arg1, path, sizeof(path)) != OSAI_OK) {
      return reject_syscall(syscall, arg0, arg1, "bad-fs-path");
    }
    if ((arg2 & OSAI_MFS_OPEN_WRITE) != 0 &&
        user_process_has_capability(OSAI_CAP_FS_WRITE) != OSAI_OK) {
      const osai_user_process_t *process = user_current_process();
      uint64_t granted = process != 0 ? process->capability_mask : 0;
      (void)security_authorize_capability("fs.open.write", granted,
                                          OSAI_CAP_FS_WRITE);
      return reject_syscall(syscall, arg0, arg1, "missing-fs-write");
    }
    if ((arg2 & OSAI_MFS_OPEN_WRITE) != 0 &&
        security_authorize_fs_write(path) != OSAI_OK) {
      return reject_syscall(syscall, arg0, arg1, "fs-open-write-denied");
    }
    if ((arg2 & OSAI_MFS_OPEN_WRITE) == 0 &&
        security_authorize_fs_read(path) != OSAI_OK) {
      return reject_syscall(syscall, arg0, arg1, "fs-open-read-denied");
    }
    int64_t fd = mutable_fs_open(path, (uint32_t)arg2);
    if (fd < 0) {
      return reject_syscall(syscall, arg0, arg1, "fs-open-denied");
    }
    return complete_control_syscall((uint64_t)fd);
  }

  if (syscall == OSAI_SYSCALL_FS_READ) {
    if (vmm_validate_user_buffer(arg1, arg2, OSAI_VMM_WRITABLE) != OSAI_OK) {
      return reject_syscall(syscall, arg0, arg1, "bad-fs-read-buffer");
    }
    int64_t bytes = mutable_fs_read_fd((uint32_t)arg0,
                                       (void *)(uintptr_t)arg1, arg2);
    if (bytes < 0) {
      return reject_syscall(syscall, arg0, arg1, "fs-read-denied");
    }
    return complete_control_syscall((uint64_t)bytes);
  }

  if (syscall == OSAI_SYSCALL_FS_WRITE) {
    if (vmm_validate_user_buffer(arg1, arg2, 0) != OSAI_OK) {
      return reject_syscall(syscall, arg0, arg1, "bad-fs-write-buffer");
    }
    if (security_reject_credential_material_buffer((const char *)(uintptr_t)arg1,
                                                   arg2) != OSAI_OK) {
      return reject_syscall(syscall, arg0, arg1, "fs-write-secret-denied");
    }
    int64_t bytes = mutable_fs_write_fd((uint32_t)arg0,
                                        (const void *)(uintptr_t)arg1, arg2);
    if (bytes < 0) {
      return reject_syscall(syscall, arg0, arg1, "fs-write-denied");
    }
    return complete_control_syscall((uint64_t)bytes);
  }

  if (syscall == OSAI_SYSCALL_FS_CLOSE) {
    if (mutable_fs_close((uint32_t)arg0) != OSAI_OK) {
      return reject_syscall(syscall, arg0, arg1, "fs-close-denied");
    }
    return complete_control_syscall(0);
  }

  if (syscall == OSAI_SYSCALL_FS_STAT) {
    char path[OSAI_MFS_PATH_MAX];
    if (copy_user_string(arg0, arg1, path, sizeof(path)) != OSAI_OK ||
        vmm_validate_user_buffer(arg2, sizeof(osai_mfs_stat_t),
                                 OSAI_VMM_WRITABLE) != OSAI_OK) {
      return reject_syscall(syscall, arg0, arg1, "bad-fs-stat");
    }
    if (mutable_fs_stat(path, (osai_mfs_stat_t *)(uintptr_t)arg2) != OSAI_OK) {
      return reject_syscall(syscall, arg0, arg1, "fs-stat-denied");
    }
    return complete_control_syscall(sizeof(osai_mfs_stat_t));
  }

  if (syscall == OSAI_SYSCALL_FS_MKDIR) {
    char path[OSAI_MFS_PATH_MAX];
    if (copy_user_string(arg0, arg1, path, sizeof(path)) != OSAI_OK ||
        security_authorize_fs_write(path) != OSAI_OK) {
      return reject_syscall(syscall, arg0, arg1, "fs-mkdir-denied");
    }
    if (mutable_fs_mkdir(path) != OSAI_OK) {
      return reject_syscall(syscall, arg0, arg1, "fs-mkdir-failed");
    }
    return complete_control_syscall(0);
  }

  if (syscall == OSAI_SYSCALL_FS_DELETE) {
    char path[OSAI_MFS_PATH_MAX];
    if (copy_user_string(arg0, arg1, path, sizeof(path)) != OSAI_OK ||
        security_authorize_fs_write(path) != OSAI_OK) {
      return reject_syscall(syscall, arg0, arg1, "fs-delete-denied");
    }
    if (mutable_fs_delete(path) != OSAI_OK) {
      return reject_syscall(syscall, arg0, arg1, "fs-delete-failed");
    }
    return complete_control_syscall(0);
  }

  if (syscall == OSAI_SYSCALL_FS_RENAME) {
    osai_syscall_rename_request_t request;
    char old_path[OSAI_MFS_PATH_MAX];
    char new_path[OSAI_MFS_PATH_MAX];
    if (arg1 != sizeof(request) ||
        vmm_validate_user_buffer(arg0, sizeof(request), 0) != OSAI_OK) {
      return reject_syscall(syscall, arg0, arg1, "bad-fs-rename-request");
    }
    bytes_copy(&request, (const void *)(uintptr_t)arg0, sizeof(request));
    if (copy_user_string(request.old_path, request.old_path_len, old_path,
                         sizeof(old_path)) != OSAI_OK ||
        copy_user_string(request.new_path, request.new_path_len, new_path,
                         sizeof(new_path)) != OSAI_OK ||
        security_authorize_fs_write(old_path) != OSAI_OK ||
        security_authorize_fs_write(new_path) != OSAI_OK) {
      return reject_syscall(syscall, arg0, arg1, "fs-rename-denied");
    }
    if (mutable_fs_rename(old_path, new_path) != OSAI_OK) {
      return reject_syscall(syscall, arg0, arg1, "fs-rename-failed");
    }
    return complete_control_syscall(0);
  }

  if (syscall == OSAI_SYSCALL_FS_LIST) {
    osai_syscall_list_request_t request;
    char path[OSAI_MFS_PATH_MAX];
    uint64_t out_size = 0;
    if (copy_user_string(arg0, arg1, path, sizeof(path)) != OSAI_OK ||
        vmm_validate_user_buffer(arg2, sizeof(request), 0) != OSAI_OK) {
      return reject_syscall(syscall, arg0, arg1, "bad-fs-list-request");
    }
    bytes_copy(&request, (const void *)(uintptr_t)arg2, sizeof(request));
    if (request.buffer_size == 0 ||
        vmm_validate_user_buffer(request.buffer, request.buffer_size,
                                 OSAI_VMM_WRITABLE) != OSAI_OK ||
        vmm_validate_user_buffer(request.out_size, sizeof(out_size),
                                 OSAI_VMM_WRITABLE) != OSAI_OK ||
        security_authorize_fs_read(path) != OSAI_OK) {
      return reject_syscall(syscall, arg0, arg1, "fs-list-denied");
    }
    if (mutable_fs_list(path, (char *)(uintptr_t)request.buffer,
                        request.buffer_size, &out_size) != OSAI_OK) {
      return reject_syscall(syscall, arg0, arg1, "fs-list-failed");
    }
    bytes_copy((void *)(uintptr_t)request.out_size, &out_size,
               sizeof(out_size));
    return complete_control_syscall(out_size);
  }

  if (syscall == OSAI_SYSCALL_NET_UDP_ECHO) {
    osai_syscall_net_request_t request;
    uint8_t payload[64];
    uint64_t echoed = 0;
    if (arg1 != sizeof(request) ||
        vmm_validate_user_buffer(arg0, sizeof(request), 0) != OSAI_OK) {
      return reject_syscall(syscall, arg0, arg1, "bad-net-udp-request");
    }
    bytes_copy(&request, (const void *)(uintptr_t)arg0, sizeof(request));
    if (request.payload_size == 0 || request.payload_size > sizeof(payload) ||
        vmm_validate_user_buffer(request.payload, request.payload_size, 0) !=
            OSAI_OK ||
        vmm_validate_user_buffer(request.out_value, sizeof(echoed),
                                 OSAI_VMM_WRITABLE) != OSAI_OK) {
      return reject_syscall(syscall, arg0, arg1, "net-udp-denied");
    }
    bytes_copy(payload, (const void *)(uintptr_t)request.payload,
               request.payload_size);
    if (network_stack_app_udp_echo(payload, request.payload_size, &echoed) !=
        OSAI_OK) {
      return reject_syscall(syscall, arg0, arg1, "net-udp-failed");
    }
    bytes_copy((void *)(uintptr_t)request.out_value, &echoed, sizeof(echoed));
    return complete_control_syscall(echoed);
  }

  if (syscall == OSAI_SYSCALL_NET_TCP_CONNECT) {
    osai_syscall_net_request_t request;
    uint64_t round_trips = 0;
    if (arg1 != sizeof(request) ||
        vmm_validate_user_buffer(arg0, sizeof(request), 0) != OSAI_OK) {
      return reject_syscall(syscall, arg0, arg1, "bad-net-tcp-request");
    }
    bytes_copy(&request, (const void *)(uintptr_t)arg0, sizeof(request));
    if (vmm_validate_user_buffer(request.out_value, sizeof(round_trips),
                                 OSAI_VMM_WRITABLE) != OSAI_OK) {
      return reject_syscall(syscall, arg0, arg1, "net-tcp-denied");
    }
    if (network_stack_app_tcp_connect(&round_trips) != OSAI_OK) {
      return reject_syscall(syscall, arg0, arg1, "net-tcp-failed");
    }
    bytes_copy((void *)(uintptr_t)request.out_value, &round_trips,
               sizeof(round_trips));
    return complete_control_syscall(round_trips);
  }

  if (syscall == OSAI_SYSCALL_SMP_RUN) {
    osai_syscall_smp_request_t request;
    uint64_t ran_workers = 0;
    uint64_t checksum = 0;
    if (arg1 != sizeof(request) ||
        vmm_validate_user_buffer(arg0, sizeof(request), 0) != OSAI_OK) {
      return reject_syscall(syscall, arg0, arg1, "bad-smp-request");
    }
    bytes_copy(&request, (const void *)(uintptr_t)arg0, sizeof(request));
    if (vmm_validate_user_buffer(request.out_workers, sizeof(ran_workers),
                                 OSAI_VMM_WRITABLE) != OSAI_OK ||
        vmm_validate_user_buffer(request.out_checksum, sizeof(checksum),
                                 OSAI_VMM_WRITABLE) != OSAI_OK) {
      return reject_syscall(syscall, arg0, arg1, "smp-run-denied");
    }
    if (smp_run_user_task_set(request.worker_count, request.iterations,
                              &ran_workers, &checksum) != OSAI_OK) {
      return reject_syscall(syscall, arg0, arg1, "smp-run-failed");
    }
    bytes_copy((void *)(uintptr_t)request.out_workers, &ran_workers,
               sizeof(ran_workers));
    bytes_copy((void *)(uintptr_t)request.out_checksum, &checksum,
               sizeof(checksum));
    return complete_control_syscall(ran_workers);
  }

  if (syscall == OSAI_SYSCALL_CPU_AI_DECODE) {
    osai_syscall_cpu_ai_decode_request_t request;
    uint8_t input[32];
    uint64_t out_size = 0;
    if (arg1 != sizeof(request) ||
        vmm_validate_user_buffer(arg0, sizeof(request), 0) != OSAI_OK) {
      return reject_syscall(syscall, arg0, arg1, "bad-cpu-ai-request");
    }
    bytes_copy(&request, (const void *)(uintptr_t)arg0, sizeof(request));
    if (request.input_size == 0 || request.input_size > sizeof(input) ||
        request.output_size == 0 ||
        vmm_validate_user_buffer(request.input, request.input_size, 0) !=
            OSAI_OK ||
        vmm_validate_user_buffer(request.output, request.output_size,
                                 OSAI_VMM_WRITABLE) != OSAI_OK ||
        vmm_validate_user_buffer(request.out_size, sizeof(out_size),
                                 OSAI_VMM_WRITABLE) != OSAI_OK) {
      return reject_syscall(syscall, arg0, arg1, "cpu-ai-denied");
    }
    bytes_copy(input, (const void *)(uintptr_t)request.input,
               request.input_size);
    if (ensure_app_cpu_ai_binding() != OSAI_OK ||
        cpu_ai_runtime_decode_piece(3, input, request.input_size,
                                    (char *)(uintptr_t)request.output,
                                    request.output_size, &out_size) !=
            OSAI_OK) {
      return reject_syscall(syscall, arg0, arg1, "cpu-ai-failed");
    }
    bytes_copy((void *)(uintptr_t)request.out_size, &out_size,
               sizeof(out_size));
    return complete_control_syscall(out_size);
  }

  if (syscall == OSAI_SYSCALL_REMOTE_LOGIN) {
    osai_syscall_remote_login_request_t request;
    char user[32];
    char command[96];
    uint64_t out_size = 0;
    if (arg1 != sizeof(request) ||
        vmm_validate_user_buffer(arg0, sizeof(request), 0) != OSAI_OK) {
      return reject_syscall(syscall, arg0, arg1, "bad-remote-login-request");
    }
    bytes_copy(&request, (const void *)(uintptr_t)arg0, sizeof(request));
    if (copy_user_string(request.user, request.user_size, user,
                         sizeof(user)) != OSAI_OK ||
        copy_user_string(request.command, request.command_size, command,
                         sizeof(command)) != OSAI_OK ||
        request.output_size == 0 ||
        vmm_validate_user_buffer(request.output, request.output_size,
                                 OSAI_VMM_WRITABLE) != OSAI_OK ||
        vmm_validate_user_buffer(request.out_size, sizeof(out_size),
                                 OSAI_VMM_WRITABLE) != OSAI_OK) {
      return reject_syscall(syscall, arg0, arg1, "remote-login-denied");
    }
    if (remote_login_execute(user, command, (char *)(uintptr_t)request.output,
                             request.output_size, &out_size) != OSAI_OK) {
      return reject_syscall(syscall, arg0, arg1, "remote-login-failed");
    }
    bytes_copy((void *)(uintptr_t)request.out_size, &out_size,
               sizeof(out_size));
    return complete_control_syscall(out_size);
  }

  if (syscall == OSAI_SYSCALL_NET_EXTERNAL_SESSION) {
    osai_syscall_net_external_session_request_t request;
    uint8_t payload[64];
    uint64_t out_size = 0;
    if (arg1 != sizeof(request) ||
        vmm_validate_user_buffer(arg0, sizeof(request), 0) != OSAI_OK) {
      return reject_syscall(syscall, arg0, arg1, "bad-net-external-request");
    }
    bytes_copy(&request, (const void *)(uintptr_t)arg0, sizeof(request));
    if (request.payload_size == 0 || request.payload_size > sizeof(payload) ||
        request.output_size == 0 ||
        vmm_validate_user_buffer(request.payload, request.payload_size, 0) !=
            OSAI_OK ||
        vmm_validate_user_buffer(request.output, request.output_size,
                                 OSAI_VMM_WRITABLE) != OSAI_OK ||
        vmm_validate_user_buffer(request.out_size, sizeof(out_size),
                                 OSAI_VMM_WRITABLE) != OSAI_OK) {
      return reject_syscall(syscall, arg0, arg1, "net-external-denied");
    }
    bytes_copy(payload, (const void *)(uintptr_t)request.payload,
               request.payload_size);
    if (network_stack_external_session(
            request.protocol, request.port, payload, request.payload_size,
            (char *)(uintptr_t)request.output, request.output_size,
            &out_size) != OSAI_OK) {
      return reject_syscall(syscall, arg0, arg1, "net-external-failed");
    }
    bytes_copy((void *)(uintptr_t)request.out_size, &out_size,
               sizeof(out_size));
    return complete_control_syscall(out_size);
  }

  if (syscall == OSAI_SYSCALL_THREAD_GROUP_RUN) {
    osai_syscall_thread_group_request_t request;
    uint64_t ran_threads = 0;
    uint64_t checksum = 0;
    if (arg1 != sizeof(request) ||
        vmm_validate_user_buffer(arg0, sizeof(request), 0) != OSAI_OK) {
      return reject_syscall(syscall, arg0, arg1, "bad-thread-group-request");
    }
    bytes_copy(&request, (const void *)(uintptr_t)arg0, sizeof(request));
    if (vmm_validate_user_buffer(request.out_threads, sizeof(ran_threads),
                                 OSAI_VMM_WRITABLE) != OSAI_OK ||
        vmm_validate_user_buffer(request.out_checksum, sizeof(checksum),
                                 OSAI_VMM_WRITABLE) != OSAI_OK) {
      return reject_syscall(syscall, arg0, arg1, "thread-group-denied");
    }
    if (smp_run_user_thread_group(request.thread_count, request.iterations,
                                  &ran_threads, &checksum) != OSAI_OK) {
      return reject_syscall(syscall, arg0, arg1, "thread-group-failed");
    }
    bytes_copy((void *)(uintptr_t)request.out_threads, &ran_threads,
               sizeof(ran_threads));
    bytes_copy((void *)(uintptr_t)request.out_checksum, &checksum,
               sizeof(checksum));
    return complete_control_syscall(ran_threads);
  }

  if (syscall == OSAI_SYSCALL_ML_RUN) {
    osai_syscall_ml_run_request_t request;
    uint8_t input[64];
    uint64_t out_size = 0;
    if (arg1 != sizeof(request) ||
        vmm_validate_user_buffer(arg0, sizeof(request), 0) != OSAI_OK) {
      return reject_syscall(syscall, arg0, arg1, "bad-ml-request");
    }
    bytes_copy(&request, (const void *)(uintptr_t)arg0, sizeof(request));
    if (request.input_size == 0 || request.input_size > sizeof(input) ||
        request.output_size == 0 ||
        vmm_validate_user_buffer(request.input, request.input_size, 0) !=
            OSAI_OK ||
        vmm_validate_user_buffer(request.output, request.output_size,
                                 OSAI_VMM_WRITABLE) != OSAI_OK ||
        vmm_validate_user_buffer(request.out_size, sizeof(out_size),
                                 OSAI_VMM_WRITABLE) != OSAI_OK) {
      return reject_syscall(syscall, arg0, arg1, "ml-run-denied");
    }
    bytes_copy(input, (const void *)(uintptr_t)request.input,
               request.input_size);
    if (ensure_app_cpu_ai_binding() != OSAI_OK ||
        cpu_ai_runtime_run_model(3, request.model_kind, input,
                                 request.input_size,
                                 (char *)(uintptr_t)request.output,
                                 request.output_size, &out_size) != OSAI_OK) {
      return reject_syscall(syscall, arg0, arg1, "ml-run-failed");
    }
    bytes_copy((void *)(uintptr_t)request.out_size, &out_size,
               sizeof(out_size));
    return complete_control_syscall(out_size);
  }

  if (syscall == OSAI_SYSCALL_NET_LISTEN) {
    osai_syscall_socket_request_t request;
    uint64_t out_sockfd = 0;
    if (arg1 != sizeof(request) ||
        vmm_validate_user_buffer(arg0, sizeof(request), 0) != OSAI_OK) {
      return reject_syscall(syscall, arg0, arg1, "bad-net-listen-request");
    }
    bytes_copy(&request, (const void *)(uintptr_t)arg0, sizeof(request));
    if (vmm_validate_user_buffer(request.out_sockfd, sizeof(out_sockfd),
                                 OSAI_VMM_WRITABLE) != OSAI_OK ||
        request.port == 0 || request.port > 65535U) {
      return reject_syscall(syscall, arg0, arg1, "net-listen-denied");
    }
    /* Allocate kernel socket for listening */
    uint64_t sockfd = kernel_socket_alloc(KERNEL_SOCK_LISTEN, (uint16_t)request.port);
    if (sockfd == 0) {
      return reject_syscall(syscall, arg0, arg1, "net-listen-no-memory");
    }
    *(uint64_t *)(uintptr_t)request.out_sockfd = sockfd;
    klog("syscall: net_listen port=%lu sockfd=%lu\n", request.port, sockfd);
    return OSAI_OK;
  }

  if (syscall == OSAI_SYSCALL_NET_ACCEPT) {
    osai_syscall_socket_request_t request;
    uint64_t out_sockfd = 0;
    if (arg1 != sizeof(request) ||
        vmm_validate_user_buffer(arg0, sizeof(request), 0) != OSAI_OK) {
      return reject_syscall(syscall, arg0, arg1, "bad-net-accept-request");
    }
    bytes_copy(&request, (const void *)(uintptr_t)arg0, sizeof(request));
    if (vmm_validate_user_buffer(request.out_sockfd, sizeof(out_sockfd),
                                 OSAI_VMM_WRITABLE) != OSAI_OK) {
      return reject_syscall(syscall, arg0, arg1, "net-accept-denied");
    }
    /* Create a connected socket */
    uint64_t connfd = kernel_socket_alloc(KERNEL_SOCK_CONNECTED, 0);
    if (connfd == 0) {
      return reject_syscall(syscall, arg0, arg1, "net-accept-no-memory");
    }
    *(uint64_t *)(uintptr_t)request.out_sockfd = connfd;
    klog("syscall: net_accept listenfd=%lu connfd=%lu\n", request.sockfd, connfd);
    return OSAI_OK;
  }

  if (syscall == OSAI_SYSCALL_NET_RECV) {
    osai_syscall_socket_request_t request;
    uint64_t out_bytes = 0;
    if (arg1 != sizeof(request) ||
        vmm_validate_user_buffer(arg0, sizeof(request), 0) != OSAI_OK) {
      return reject_syscall(syscall, arg0, arg1, "bad-net-recv-request");
    }
    bytes_copy(&request, (const void *)(uintptr_t)arg0, sizeof(request));
    if (request.buffer_size == 0 ||
        vmm_validate_user_buffer(request.buffer, request.buffer_size,
                                 OSAI_VMM_WRITABLE) != OSAI_OK ||
        vmm_validate_user_buffer(request.out_bytes, sizeof(out_bytes),
                                 OSAI_VMM_WRITABLE) != OSAI_OK) {
      return reject_syscall(syscall, arg0, arg1, "net-recv-denied");
    }
    /* Return 0 bytes (no data available yet) */
    *(uint64_t *)(uintptr_t)request.out_bytes = 0;
    return OSAI_OK;
  }

  if (syscall == OSAI_SYSCALL_NET_SEND) {
    osai_syscall_socket_request_t request;
    uint64_t out_bytes = 0;
    if (arg1 != sizeof(request) ||
        vmm_validate_user_buffer(arg0, sizeof(request), 0) != OSAI_OK) {
      return reject_syscall(syscall, arg0, arg1, "bad-net-send-request");
    }
    bytes_copy(&request, (const void *)(uintptr_t)arg0, sizeof(request));
    if (request.buffer_size == 0 ||
        vmm_validate_user_buffer(request.buffer, request.buffer_size, 0) !=
            OSAI_OK ||
        vmm_validate_user_buffer(request.out_bytes, sizeof(out_bytes),
                                 OSAI_VMM_WRITABLE) != OSAI_OK) {
      return reject_syscall(syscall, arg0, arg1, "net-send-denied");
    }
    /* Accept all data as sent (stub) */
    *(uint64_t *)(uintptr_t)request.out_bytes = request.buffer_size;
    klog("syscall: net_send sockfd=%lu len=%lu\n", request.sockfd, request.buffer_size);
    return OSAI_OK;
  }

  if (syscall == OSAI_SYSCALL_NET_CLOSE) {
    kernel_socket_free(arg0);
    klog("syscall: net_close sockfd=%lu\n", arg0);
    return OSAI_OK;
  }

  if (syscall == OSAI_SYSCALL_AGENT_DISPATCH) {
    osai_syscall_agent_dispatch_request_t request;
    osai_agent_request_t agent_req;
    osai_agent_response_t agent_resp;
    uint8_t agent_payload[4096];
    uint64_t out_size = 0;
    if (arg1 != sizeof(request) ||
        vmm_validate_user_buffer(arg0, sizeof(request), 0) != OSAI_OK) {
      return reject_syscall(syscall, arg0, arg1, "bad-agent-request");
    }
    bytes_copy(&request, (const void *)(uintptr_t)arg0, sizeof(request));
    if (request.request_size != sizeof(agent_req) ||
        vmm_validate_user_buffer(request.request, sizeof(agent_req), 0) !=
            OSAI_OK ||
        request.response_size != sizeof(agent_resp) ||
        vmm_validate_user_buffer(request.response, sizeof(agent_resp),
                                 OSAI_VMM_WRITABLE) != OSAI_OK ||
        request.output_size == 0 ||
        vmm_validate_user_buffer(request.output, request.output_size,
                                 OSAI_VMM_WRITABLE) != OSAI_OK ||
        vmm_validate_user_buffer(request.out_size, sizeof(out_size),
                                 OSAI_VMM_WRITABLE) != OSAI_OK) {
      return reject_syscall(syscall, arg0, arg1, "agent-dispatch-denied");
    }
    bytes_copy(&agent_req, (const void *)(uintptr_t)request.request,
               sizeof(agent_req));
    if (request.payload_size > 0) {
      if (request.payload_size > sizeof(agent_payload) ||
          vmm_validate_user_buffer(request.payload, request.payload_size, 0) !=
              OSAI_OK) {
        return reject_syscall(syscall, arg0, arg1, "agent-payload-denied");
      }
      bytes_copy(agent_payload, (const void *)(uintptr_t)request.payload,
                 request.payload_size);
    }
    if (agent_protocol_dispatch(&agent_req, &agent_resp,
                                request.payload_size > 0 ? agent_payload : 0,
                                request.payload_size,
                                (char *)(uintptr_t)request.output,
                                request.output_size, &out_size) != OSAI_OK) {
      bytes_copy((void *)(uintptr_t)request.response, &agent_resp,
                 sizeof(agent_resp));
      return reject_syscall(syscall, arg0, arg1, "agent-dispatch-failed");
    }
    bytes_copy((void *)(uintptr_t)request.response, &agent_resp,
               sizeof(agent_resp));
    bytes_copy((void *)(uintptr_t)request.out_size, &out_size,
               sizeof(out_size));
    klog("syscall: agent_dispatch cmd=%u cell=%u out=%lu\n",
         agent_req.command, agent_req.cell_id, out_size);
    return complete_control_syscall(out_size);
  }

  return reject_syscall(syscall, arg0, arg1, "unreachable");
}

void syscall_self_test(void) {
  kassert(lookup_syscall(OSAI_SYSCALL_LOG) != 0);
  kassert(lookup_syscall(OSAI_SYSCALL_EXIT) != 0);
  kassert(lookup_syscall(OSAI_SYSCALL_OSCTL) != 0);
  kassert(lookup_syscall(OSAI_SYSCALL_READ_SERVICE_DESCRIPTOR) != 0);
  kassert(lookup_syscall(OSAI_SYSCALL_SERVICE_STATUS) != 0);
  kassert(lookup_syscall(OSAI_SYSCALL_SERVICE_START) != 0);
  kassert(lookup_syscall(OSAI_SYSCALL_SERVICE_STOP) != 0);
  kassert(lookup_syscall(OSAI_SYSCALL_SERVICE_RESTART) != 0);
  kassert(lookup_syscall(OSAI_SYSCALL_SERVICE_ROLLBACK) != 0);
  kassert(lookup_syscall(OSAI_SYSCALL_SERVICE_UPDATE) != 0);
  kassert(lookup_syscall(OSAI_SYSCALL_FS_OPEN) != 0);
  kassert(lookup_syscall(OSAI_SYSCALL_FS_READ) != 0);
  kassert(lookup_syscall(OSAI_SYSCALL_FS_WRITE) != 0);
  kassert(lookup_syscall(OSAI_SYSCALL_FS_CLOSE) != 0);
  kassert(lookup_syscall(OSAI_SYSCALL_FS_STAT) != 0);
  kassert(lookup_syscall(OSAI_SYSCALL_FS_MKDIR) != 0);
  kassert(lookup_syscall(OSAI_SYSCALL_FS_DELETE) != 0);
  kassert(lookup_syscall(OSAI_SYSCALL_FS_RENAME) != 0);
  kassert(lookup_syscall(OSAI_SYSCALL_FS_LIST) != 0);
  kassert(lookup_syscall(OSAI_SYSCALL_CLOCK_NANOS) != 0);
  kassert(lookup_syscall(OSAI_SYSCALL_NET_UDP_ECHO) != 0);
  kassert(lookup_syscall(OSAI_SYSCALL_NET_TCP_CONNECT) != 0);
  kassert(lookup_syscall(OSAI_SYSCALL_SMP_RUN) != 0);
  kassert(lookup_syscall(OSAI_SYSCALL_CPU_AI_DECODE) != 0);
  kassert(lookup_syscall(OSAI_SYSCALL_REMOTE_LOGIN) != 0);
  kassert(lookup_syscall(OSAI_SYSCALL_NET_EXTERNAL_SESSION) != 0);
  kassert(lookup_syscall(OSAI_SYSCALL_THREAD_GROUP_RUN) != 0);
  kassert(lookup_syscall(OSAI_SYSCALL_ML_RUN) != 0);
  kassert(lookup_syscall(OSAI_SYSCALL_NET_LISTEN) != 0);
  kassert(lookup_syscall(OSAI_SYSCALL_NET_ACCEPT) != 0);
  kassert(lookup_syscall(OSAI_SYSCALL_NET_RECV) != 0);
  kassert(lookup_syscall(OSAI_SYSCALL_NET_SEND) != 0);
  kassert(lookup_syscall(OSAI_SYSCALL_NET_CLOSE) != 0);
  kassert(lookup_syscall(OSAI_SYSCALL_AGENT_DISPATCH) != 0);
  kassert(lookup_syscall(99) == 0);
  klog("syscall: table self-test passed entries=%lu\n",
       (uint64_t)(sizeof(g_syscall_table) / sizeof(g_syscall_table[0])));
}

uint64_t syscall_control_plane_count(void) {
  return g_control_plane_syscall_count;
}

uint64_t syscall_control_plane_denial_count(void) {
  return g_control_plane_denial_count;
}

uint64_t syscall_service_descriptor_read_count(void) {
  return g_service_descriptor_read_count;
}
