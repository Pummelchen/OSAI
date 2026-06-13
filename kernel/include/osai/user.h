#ifndef OSAI_USER_H
#define OSAI_USER_H

#include <osai/initramfs.h>
#include <osai/status.h>
#include <osai/types.h>

#define OSAI_SYSCALL_LOG UINT64_C(1)
#define OSAI_SYSCALL_EXIT UINT64_C(2)
#define OSAI_SYSCALL_OSCTL UINT64_C(3)

typedef struct osai_user_process {
  uint64_t entry;
  uint64_t stack_top;
  uint64_t stack_guard_low;
  uint64_t stack_guard_high;
} osai_user_process_t;

osai_status_t user_load_init(const osai_initramfs_file_t *file,
                             osai_user_process_t *process);
void user_process_run(const osai_user_process_t *process);

#endif
