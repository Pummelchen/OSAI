#ifndef OSAI_USER_H
#define OSAI_USER_H

#include <osai/initramfs.h>
#include <osai/status.h>
#include <osai/syscall.h>
#include <osai/types.h>

#define OSAI_MAX_USER_PROCESSES 16U
#define OSAI_USER_EXIT_RETURN_MAGIC UINT64_C(0x4f53414900000000)
#define OSAI_USER_EXIT_RETURN_MASK UINT64_C(0xffffffff00000000)

typedef enum osai_user_process_state {
  OSAI_USER_PROCESS_EMPTY = 0,
  OSAI_USER_PROCESS_LOADED = 1,
  OSAI_USER_PROCESS_RUNNABLE = 2,
  OSAI_USER_PROCESS_RUNNING = 3,
  OSAI_USER_PROCESS_WAITING = 4,
  OSAI_USER_PROCESS_EXITED = 5,
  OSAI_USER_PROCESS_FAILED = 6,
} osai_user_process_state_t;

typedef struct osai_user_process {
  uint32_t pid;
  uint32_t parent_pid;
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
  uint64_t mapped_low;
  uint64_t mapped_high;
  uint64_t scheduler_ticks;
} osai_user_process_t;

void user_process_table_init(void);
void user_process_lifecycle_self_test(void);
void user_scheduler_self_test(void);
const osai_user_process_t *user_current_process(void);
osai_status_t user_process_has_capability(uint64_t capability);
void user_process_note_syscall(uint32_t rejected);
uint64_t user_process_note_exit(int exit_code);
osai_status_t user_load_init(const osai_initramfs_file_t *file,
                             osai_user_process_t *process);
osai_status_t user_load_process(const osai_initramfs_file_t *file,
                                uint32_t pid, uint64_t capability_mask,
                                osai_user_process_t *process);
osai_status_t user_process_snapshot(uint32_t pid, osai_user_process_t *process);
osai_status_t user_process_make_runnable(uint32_t pid, uint32_t parent_pid);
osai_status_t user_process_wait(uint32_t pid);
osai_status_t user_process_wake(uint32_t pid);
int user_process_run(const osai_user_process_t *process);
void user_process_reclaim_address_space(const osai_user_process_t *process);
uint64_t user_process_transition_count(void);
uint64_t user_process_loaded_count(void);
uint64_t user_process_runnable_count(void);
uint64_t user_process_running_count(void);
uint64_t user_process_waiting_count(void);
uint64_t user_process_exited_count(void);
uint64_t user_process_failed_count(void);
uint64_t user_process_reclaim_count(void);
uint64_t user_process_scheduled_count(void);
uint64_t user_process_wait_count(void);
uint64_t user_process_wake_count(void);
uint64_t user_process_active_count(void);

#endif
