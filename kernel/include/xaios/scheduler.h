#ifndef XAIOS_SCHEDULER_H
#define XAIOS_SCHEDULER_H

#include <xaios/context.h>
#include <xaios/status.h>
#include <xaios/types.h>

#define XAIOS_SCHEDULER_MAX_TASKS 16U
#define XAIOS_SCHEDULER_DEFAULT_TICK_HZ 100U

typedef struct xaios_sched_task {
  uint32_t pid;
  uint32_t active;
  xaios_context_frame_t frame;
  uint64_t tick_count;
  uint64_t switch_count;
} xaios_sched_task_t;

void scheduler_init(void);
void scheduler_tick(xaios_context_frame_t *irq_frame);
void scheduler_yield(void);
void scheduler_lock(void);
void scheduler_unlock(void);
xaios_status_t scheduler_register(uint32_t pid);
void scheduler_unregister(uint32_t pid);
xaios_status_t scheduler_set_runnable(uint32_t pid);
xaios_status_t scheduler_set_blocked(uint32_t pid);
xaios_context_frame_t *scheduler_task_frame(uint32_t pid);
uint64_t scheduler_tick_count(void);
uint64_t scheduler_context_switch_count(void);
uint64_t scheduler_yield_count(void);
uint32_t scheduler_current_pid(void);
uint32_t scheduler_runnable_count(void);
void scheduler_self_test(void);

#endif
