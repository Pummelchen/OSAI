#include <osai/assert.h>
#include <osai/initramfs.h>
#include <osai/klog.h>
#include <osai/mutable_fs.h>
#include <osai/security.h>
#include <osai/service.h>
#include <osai/syscall.h>
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
};

static uint64_t g_control_plane_syscall_count;
static uint64_t g_control_plane_denial_count;
static uint64_t g_service_descriptor_read_count;

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
          syscall <= OSAI_SYSCALL_FS_STAT);
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
      klog("user: %s exit rejected status=%u service_not_running\n",
           name, (unsigned)arg0);
      return reject_syscall(syscall, arg0, arg1, "service-not-running");
    }
    user_process_note_syscall(0);
    klog("user: %s exited status=%u syscalls=%lu rejected=%lu\n",
         name, (unsigned)arg0, process != 0 ? process->syscall_count : 0,
         process != 0 ? process->rejected_syscall_count : 0);
    return user_process_note_exit((int)arg0);
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
