#include <osai/assert.h>
#include <osai/context.h>
#include <osai/klog.h>
#include <osai/scheduler.h>
#include <osai/user.h>

static osai_sched_task_t g_tasks[OSAI_SCHEDULER_MAX_TASKS];
static uint32_t g_runqueue[OSAI_SCHEDULER_MAX_TASKS];
static uint32_t g_runqueue_count;
static uint32_t g_current_index;
static uint32_t g_current_pid;
static uint32_t g_lock_depth;
static uint64_t g_tick_count;
static uint64_t g_context_switch_count;
static uint64_t g_yield_count;
static uint32_t g_initialized;

static void bytes_zero(void *buffer, uint64_t size) {
  uint8_t *bytes = (uint8_t *)buffer;
  for (uint64_t i = 0; i < size; ++i) {
    bytes[i] = 0;
  }
}

static osai_sched_task_t *find_task(uint32_t pid) {
  for (uint32_t i = 0; i < OSAI_SCHEDULER_MAX_TASKS; ++i) {
    if (g_tasks[i].active != 0 && g_tasks[i].pid == pid) {
      return &g_tasks[i];
    }
  }
  return 0;
}

static uint32_t runqueue_index(uint32_t pid) {
  for (uint32_t i = 0; i < g_runqueue_count; ++i) {
    if (g_runqueue[i] == pid) {
      return i;
    }
  }
  return UINT32_C(0xffffffff);
}

static void runqueue_add(uint32_t pid) {
  if (g_runqueue_count < OSAI_SCHEDULER_MAX_TASKS &&
      runqueue_index(pid) == UINT32_C(0xffffffff)) {
    g_runqueue[g_runqueue_count++] = pid;
  }
}

static void runqueue_remove(uint32_t pid) {
  uint32_t idx = runqueue_index(pid);
  if (idx == UINT32_C(0xffffffff)) {
    return;
  }
  for (uint32_t i = idx; i + 1U < g_runqueue_count; ++i) {
    g_runqueue[i] = g_runqueue[i + 1U];
  }
  --g_runqueue_count;
  if (g_current_index >= g_runqueue_count && g_runqueue_count > 0) {
    g_current_index = 0;
  }
}

void scheduler_init(void) {
  bytes_zero(g_tasks, sizeof(g_tasks));
  bytes_zero(g_runqueue, sizeof(g_runqueue));
  g_runqueue_count = 0;
  g_current_index = 0;
  g_current_pid = 0;
  g_lock_depth = 0;
  g_tick_count = 0;
  g_context_switch_count = 0;
  g_yield_count = 0;
  g_initialized = 1;
  klog("scheduler: initialized max_tasks=%u tick_hz=%u\n",
       OSAI_SCHEDULER_MAX_TASKS, OSAI_SCHEDULER_DEFAULT_TICK_HZ);
}

osai_status_t scheduler_register(uint32_t pid) {
  if (pid == 0 || pid > OSAI_SCHEDULER_MAX_TASKS) {
    return OSAI_ERR_INVALID;
  }
  osai_sched_task_t *task = find_task(pid);
  if (task != 0) {
    return OSAI_ERR_INVALID;
  }
  for (uint32_t i = 0; i < OSAI_SCHEDULER_MAX_TASKS; ++i) {
    if (g_tasks[i].active == 0) {
      bytes_zero(&g_tasks[i], sizeof(g_tasks[i]));
      g_tasks[i].pid = pid;
      g_tasks[i].active = 1;
      klog("scheduler: registered pid=%u slot=%u\n", pid, i);
      return OSAI_OK;
    }
  }
  return OSAI_ERR_NO_MEMORY;
}

void scheduler_unregister(uint32_t pid) {
  osai_sched_task_t *task = find_task(pid);
  if (task == 0) {
    return;
  }
  runqueue_remove(pid);
  task->active = 0;
  klog("scheduler: unregistered pid=%u\n", pid);
}

osai_status_t scheduler_set_runnable(uint32_t pid) {
  osai_sched_task_t *task = find_task(pid);
  if (task == 0) {
    return OSAI_ERR_INVALID;
  }
  runqueue_add(pid);
  return OSAI_OK;
}

osai_status_t scheduler_set_blocked(uint32_t pid) {
  osai_sched_task_t *task = find_task(pid);
  if (task == 0) {
    return OSAI_ERR_INVALID;
  }
  runqueue_remove(pid);
  return OSAI_OK;
}

osai_context_frame_t *scheduler_task_frame(uint32_t pid) {
  osai_sched_task_t *task = find_task(pid);
  if (task == 0) {
    return 0;
  }
  return &task->frame;
}

void scheduler_lock(void) {
  ++g_lock_depth;
}

void scheduler_unlock(void) {
  if (g_lock_depth > 0) {
    --g_lock_depth;
  }
}

void scheduler_tick(osai_context_frame_t *irq_frame) {
  if (g_initialized == 0 || g_lock_depth > 0 || g_runqueue_count == 0) {
    return;
  }
  ++g_tick_count;

  /* save current task's IRQ frame if we have a current task */
  if (g_current_pid != 0) {
    osai_sched_task_t *current = find_task(g_current_pid);
    if (current != 0) {
      current->frame = *irq_frame;
      ++current->tick_count;
    }
  }

  /* round-robin: advance to next runnable task */
  uint32_t next_index = g_current_index;
  if (g_runqueue_count > 1U) {
    next_index = (g_current_index + 1U) % g_runqueue_count;
  }

  uint32_t next_pid = g_runqueue[next_index];
  if (next_pid == g_current_pid) {
    /* same task, no switch needed */
    return;
  }

  /* context switch to next task */
  osai_sched_task_t *next_task = find_task(next_pid);
  if (next_task == 0) {
    return;
  }

  uint32_t old_pid = g_current_pid;
  g_current_index = next_index;
  g_current_pid = next_pid;
  ++g_context_switch_count;
  if (next_task != 0) {
    ++next_task->switch_count;
  }

  klog("scheduler: switch %u -> %u ticks=%lu switches=%lu\n",
       old_pid, next_pid, g_tick_count, g_context_switch_count);

  /* Switch per-process address space if changing processes */
  user_switch_address_space(next_pid);

  /* restore next task's frame into the IRQ return frame */
  *irq_frame = next_task->frame;
}

void scheduler_yield(void) {
  if (g_initialized == 0 || g_runqueue_count <= 1U) {
    return;
  }
  ++g_yield_count;
  /* trigger a timer-like tick by calling scheduler_tick with current frame */
  osai_context_frame_t frame;
  bytes_zero(&frame, sizeof(frame));
  if (g_current_pid != 0) {
    osai_sched_task_t *current = find_task(g_current_pid);
    if (current != 0) {
      frame = current->frame;
    }
  }
  scheduler_tick(&frame);
  /* if we switched, frame now has the new task's state */
  if (g_current_pid != 0) {
    osai_sched_task_t *current = find_task(g_current_pid);
    if (current != 0) {
      current->frame = frame;
    }
  }
}

uint64_t scheduler_tick_count(void) { return g_tick_count; }
uint64_t scheduler_context_switch_count(void) { return g_context_switch_count; }
uint64_t scheduler_yield_count(void) { return g_yield_count; }
uint32_t scheduler_current_pid(void) { return g_current_pid; }

uint32_t scheduler_runnable_count(void) { return g_runqueue_count; }

void scheduler_self_test(void) {
  kassert(g_initialized != 0);
  kassert(scheduler_register(1) == OSAI_OK);
  kassert(scheduler_register(2) == OSAI_OK);
  kassert(scheduler_register(3) == OSAI_OK);
  kassert(scheduler_set_runnable(1) == OSAI_OK);
  kassert(scheduler_set_runnable(2) == OSAI_OK);
  kassert(scheduler_set_runnable(3) == OSAI_OK);
  kassert(scheduler_runnable_count() == 3);

  /* simulate ticks to verify round-robin */
  g_current_pid = 1;
  g_current_index = 0;
  osai_context_frame_t dummy_frame;
  bytes_zero(&dummy_frame, sizeof(dummy_frame));
  dummy_frame.elr_el1 = 0x1000;
  scheduler_tick(&dummy_frame);
  kassert(g_current_pid == 2);
  scheduler_tick(&dummy_frame);
  kassert(g_current_pid == 3);
  scheduler_tick(&dummy_frame);
  kassert(g_current_pid == 1);
  kassert(g_context_switch_count >= 3);

  /* test lock prevents switching */
  scheduler_lock();
  uint32_t before = g_current_pid;
  scheduler_tick(&dummy_frame);
  kassert(g_current_pid == before);
  scheduler_unlock();

  /* test blocking removes from runqueue */
  kassert(scheduler_set_blocked(2) == OSAI_OK);
  kassert(scheduler_runnable_count() == 2);
  kassert(scheduler_set_runnable(2) == OSAI_OK);
  kassert(scheduler_runnable_count() == 3);

  /* unregister cleans up */
  scheduler_unregister(1);
  scheduler_unregister(2);
  scheduler_unregister(3);
  kassert(scheduler_runnable_count() == 0);
  kassert(find_task(1) == 0);

  klog("scheduler: self-test passed ticks=%lu switches=%lu yields=%lu\n",
       g_tick_count, g_context_switch_count, g_yield_count);
}
