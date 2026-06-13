#ifndef OSAI_USER_H
#define OSAI_USER_H

#include <osai/initramfs.h>
#include <osai/status.h>
#include <osai/syscall.h>
#include <osai/types.h>

#define OSAI_MAX_USER_PROCESSES 4U

typedef struct osai_user_process {
  uint32_t pid;
  const char *name;
  uint64_t capability_mask;
  uint64_t syscall_count;
  uint64_t rejected_syscall_count;
  uint64_t entry;
  uint64_t stack_top;
  uint64_t stack_guard_low;
  uint64_t stack_guard_high;
} osai_user_process_t;

void user_process_table_init(void);
const osai_user_process_t *user_current_process(void);
osai_status_t user_process_has_capability(uint64_t capability);
void user_process_note_syscall(uint32_t rejected);
osai_status_t user_load_init(const osai_initramfs_file_t *file,
                             osai_user_process_t *process);
void user_process_run(const osai_user_process_t *process);

#endif
