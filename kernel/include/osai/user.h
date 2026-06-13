#ifndef OSAI_USER_H
#define OSAI_USER_H

#include <osai/initramfs.h>
#include <osai/status.h>
#include <osai/syscall.h>
#include <osai/types.h>

#define OSAI_MAX_USER_PROCESSES 4U
#define OSAI_USER_EXIT_RETURN_MAGIC UINT64_C(0x4f53414900000000)
#define OSAI_USER_EXIT_RETURN_MASK UINT64_C(0xffffffff00000000)

typedef enum osai_user_process_state {
  OSAI_USER_PROCESS_EMPTY = 0,
  OSAI_USER_PROCESS_LOADED = 1,
  OSAI_USER_PROCESS_RUNNING = 2,
  OSAI_USER_PROCESS_EXITED = 3,
  OSAI_USER_PROCESS_FAILED = 4,
} osai_user_process_state_t;

typedef struct osai_user_process {
  uint32_t pid;
  const char *name;
  osai_user_process_state_t state;
  int exit_code;
  uint64_t capability_mask;
  uint64_t syscall_count;
  uint64_t rejected_syscall_count;
  uint64_t entry;
  uint64_t stack_top;
  uint64_t stack_guard_low;
  uint64_t stack_guard_high;
} osai_user_process_t;

void user_process_table_init(void);
void user_process_lifecycle_self_test(void);
const osai_user_process_t *user_current_process(void);
osai_status_t user_process_has_capability(uint64_t capability);
void user_process_note_syscall(uint32_t rejected);
uint64_t user_process_note_exit(int exit_code);
osai_status_t user_load_init(const osai_initramfs_file_t *file,
                             osai_user_process_t *process);
int user_process_run(const osai_user_process_t *process);
uint64_t user_process_transition_count(void);
uint64_t user_process_loaded_count(void);
uint64_t user_process_running_count(void);
uint64_t user_process_exited_count(void);
uint64_t user_process_failed_count(void);

#endif
