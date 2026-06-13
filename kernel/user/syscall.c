#include <osai/assert.h>
#include <osai/klog.h>
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
};

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

static uint64_t reject_syscall(uint64_t syscall, uint64_t arg0, uint64_t arg1,
                               const char *reason) {
  user_process_note_syscall(1);
  klog("user: rejected syscall=%lu arg0=0x%lx arg1=0x%lx reason=%s\n",
       syscall, arg0, arg1, reason);
  return UINT64_C(-1);
}

uint64_t syscall_dispatch(uint64_t syscall, uint64_t arg0, uint64_t arg1,
                          uint64_t arg2) {
  (void)arg2;

  const osai_syscall_entry_t *entry = lookup_syscall(syscall);
  if (entry == 0) {
    return reject_syscall(syscall, arg0, arg1, "unknown");
  }
  if (user_process_has_capability(entry->required_capability) != OSAI_OK) {
    return reject_syscall(syscall, arg0, arg1, "missing-capability");
  }

  if (syscall == OSAI_SYSCALL_LOG) {
    if (vmm_validate_user_buffer(arg0, arg1, 0) != OSAI_OK) {
      return reject_syscall(syscall, arg0, arg1, "bad-user-buffer");
    }
    user_process_note_syscall(0);
    klog_write((const char *)(uintptr_t)arg0, arg1);
    return 0;
  }

  if (syscall == OSAI_SYSCALL_EXIT) {
    if (service_exit("/init", (int)arg0) != OSAI_OK) {
      klog("user: /init exit rejected status=%u service_not_running\n",
           (unsigned)arg0);
      return reject_syscall(syscall, arg0, arg1, "service-not-running");
    }
    user_process_note_syscall(0);
    const osai_user_process_t *process = user_current_process();
    klog("user: /init exited status=%u syscalls=%lu rejected=%lu\n",
         (unsigned)arg0, process != 0 ? process->syscall_count : 0,
         process != 0 ? process->rejected_syscall_count : 0);
    for (;;) {
      __asm__ volatile("wfe");
    }
  }

  if (syscall == OSAI_SYSCALL_OSCTL) {
    char command[128];
    if (copy_user_string(arg0, arg1, command, sizeof(command)) != OSAI_OK) {
      return reject_syscall(syscall, arg0, arg1, "bad-user-string");
    }
    klog("user: osctl command='%s'\n", command);
    if (osctl_execute(command) != OSAI_OK) {
      return reject_syscall(syscall, arg0, arg1, "osctl-denied");
    }
    user_process_note_syscall(0);
    return 0;
  }

  return reject_syscall(syscall, arg0, arg1, "unreachable");
}

void syscall_self_test(void) {
  kassert(lookup_syscall(OSAI_SYSCALL_LOG) != 0);
  kassert(lookup_syscall(OSAI_SYSCALL_EXIT) != 0);
  kassert(lookup_syscall(OSAI_SYSCALL_OSCTL) != 0);
  kassert(lookup_syscall(99) == 0);
  klog("syscall: table self-test passed entries=%lu\n",
       (uint64_t)(sizeof(g_syscall_table) / sizeof(g_syscall_table[0])));
}
