#include <osai/assert.h>
#include <osai/klog.h>
#include <osai/pmm.h>
#include <osai/user.h>
#include <osai/vmm.h>

#define PAGE_SIZE UINT64_C(4096)
#define ELF_MAGIC UINT32_C(0x464c457f)
#define PT_LOAD UINT32_C(1)
#define PF_X UINT32_C(1)
#define PF_W UINT32_C(2)
#define PF_R UINT32_C(4)
#define ET_EXEC UINT16_C(2)
#define EM_AARCH64 UINT16_C(183)

typedef struct elf64_ehdr {
  uint8_t ident[16];
  uint16_t type;
  uint16_t machine;
  uint32_t version;
  uint64_t entry;
  uint64_t phoff;
  uint64_t shoff;
  uint32_t flags;
  uint16_t ehsize;
  uint16_t phentsize;
  uint16_t phnum;
  uint16_t shentsize;
  uint16_t shnum;
  uint16_t shstrndx;
} elf64_ehdr_t;

typedef struct elf64_phdr {
  uint32_t type;
  uint32_t flags;
  uint64_t offset;
  uint64_t vaddr;
  uint64_t paddr;
  uint64_t filesz;
  uint64_t memsz;
  uint64_t align;
} elf64_phdr_t;

static uint64_t align_down(uint64_t value, uint64_t align) {
  return value & ~(align - 1);
}

static uint64_t align_up(uint64_t value, uint64_t align) {
  return (value + align - 1) & ~(align - 1);
}

static void bytes_zero(void *buffer, uint64_t size) {
  uint8_t *bytes = (uint8_t *)buffer;
  for (uint64_t i = 0; i < size; ++i) {
    bytes[i] = 0;
  }
}

static void bytes_copy(void *dst, const void *src, uint64_t size) {
  uint8_t *out = (uint8_t *)dst;
  const uint8_t *in = (const uint8_t *)src;
  for (uint64_t i = 0; i < size; ++i) {
    out[i] = in[i];
  }
}

static uint32_t elf_magic(const uint8_t *ident) {
  return ((uint32_t)ident[0]) | ((uint32_t)ident[1] << 8U) |
         ((uint32_t)ident[2] << 16U) | ((uint32_t)ident[3] << 24U);
}

static osai_user_process_t g_process_table[OSAI_MAX_USER_PROCESSES];
static osai_user_process_t *g_current_process;
static uint64_t g_process_transition_count;
static uint64_t g_process_loaded_count;
static uint64_t g_process_runnable_count;
static uint64_t g_process_running_count;
static uint64_t g_process_waiting_count;
static uint64_t g_process_exited_count;
static uint64_t g_process_failed_count;
static uint64_t g_process_reclaim_count;
static uint64_t g_process_scheduled_count;
static uint64_t g_process_wait_count;
static uint64_t g_process_wake_count;

extern uint64_t aarch64_enter_user(uint64_t entry, uint64_t stack);

static void copy_process(osai_user_process_t *dst,
                         const osai_user_process_t *src) {
  dst->pid = src->pid;
  dst->parent_pid = src->parent_pid;
  dst->name = src->name;
  dst->state = src->state;
  dst->exit_code = src->exit_code;
  dst->capability_mask = src->capability_mask;
  dst->syscall_count = src->syscall_count;
  dst->rejected_syscall_count = src->rejected_syscall_count;
  dst->entry = src->entry;
  dst->stack_top = src->stack_top;
  dst->stack_guard_low = src->stack_guard_low;
  dst->stack_guard_high = src->stack_guard_high;
  dst->mapped_low = src->mapped_low;
  dst->mapped_high = src->mapped_high;
  dst->scheduler_ticks = src->scheduler_ticks;
}

static const char *process_state_name(osai_user_process_state_t state) {
  switch (state) {
  case OSAI_USER_PROCESS_EMPTY:
    return "empty";
  case OSAI_USER_PROCESS_LOADED:
    return "loaded";
  case OSAI_USER_PROCESS_RUNNABLE:
    return "runnable";
  case OSAI_USER_PROCESS_RUNNING:
    return "running";
  case OSAI_USER_PROCESS_WAITING:
    return "waiting";
  case OSAI_USER_PROCESS_EXITED:
    return "exited";
  case OSAI_USER_PROCESS_FAILED:
    return "failed";
  default:
    return "unknown";
  }
}

static void reset_process_slot(osai_user_process_t *process) {
  process->pid = 0;
  process->parent_pid = 0;
  process->name = 0;
  process->state = OSAI_USER_PROCESS_EMPTY;
  process->exit_code = 0;
  process->capability_mask = 0;
  process->syscall_count = 0;
  process->rejected_syscall_count = 0;
  process->entry = 0;
  process->stack_top = 0;
  process->stack_guard_low = 0;
  process->stack_guard_high = 0;
  process->mapped_low = 0;
  process->mapped_high = 0;
  process->scheduler_ticks = 0;
}

static void track_process_mapping(osai_user_process_t *process, uint64_t start,
                                  uint64_t end) {
  if (process->mapped_low == 0 || start < process->mapped_low) {
    process->mapped_low = start;
  }
  if (end > process->mapped_high) {
    process->mapped_high = end;
  }
}

static osai_status_t validate_process_transition(osai_user_process_state_t from,
                                                 osai_user_process_state_t to) {
  if (from == OSAI_USER_PROCESS_EMPTY && to == OSAI_USER_PROCESS_LOADED) {
    return OSAI_OK;
  }
  if (from == OSAI_USER_PROCESS_LOADED &&
      (to == OSAI_USER_PROCESS_RUNNABLE || to == OSAI_USER_PROCESS_RUNNING)) {
    return OSAI_OK;
  }
  if (from == OSAI_USER_PROCESS_RUNNABLE &&
      (to == OSAI_USER_PROCESS_RUNNING || to == OSAI_USER_PROCESS_WAITING ||
       to == OSAI_USER_PROCESS_FAILED)) {
    return OSAI_OK;
  }
  if (from == OSAI_USER_PROCESS_WAITING &&
      (to == OSAI_USER_PROCESS_RUNNABLE || to == OSAI_USER_PROCESS_FAILED)) {
    return OSAI_OK;
  }
  if (from == OSAI_USER_PROCESS_RUNNING &&
      (to == OSAI_USER_PROCESS_RUNNABLE || to == OSAI_USER_PROCESS_WAITING ||
       to == OSAI_USER_PROCESS_EXITED || to == OSAI_USER_PROCESS_FAILED)) {
    return OSAI_OK;
  }
  return OSAI_ERR_INVALID;
}

static void transition_process(osai_user_process_t *process,
                               osai_user_process_state_t state,
                               int exit_code) {
  kassert(process != 0);
  if (process->state == state && process->exit_code == exit_code) {
    return;
  }
  kassert(validate_process_transition(process->state, state) == OSAI_OK);

  process->state = state;
  process->exit_code = exit_code;
  ++g_process_transition_count;

  switch (state) {
  case OSAI_USER_PROCESS_LOADED:
    ++g_process_loaded_count;
    break;
  case OSAI_USER_PROCESS_RUNNABLE:
    ++g_process_runnable_count;
    break;
  case OSAI_USER_PROCESS_RUNNING:
    ++g_process_running_count;
    break;
  case OSAI_USER_PROCESS_WAITING:
    ++g_process_waiting_count;
    break;
  case OSAI_USER_PROCESS_EXITED:
    ++g_process_exited_count;
    break;
  case OSAI_USER_PROCESS_FAILED:
    ++g_process_failed_count;
    break;
  default:
    break;
  }

  klog("user: process pid=%u name=%s state=%s exit_code=%u transitions=%lu\n",
       process->pid, process->name != 0 ? process->name : "(none)",
       process_state_name(state), (unsigned)exit_code,
       g_process_transition_count);
}

void user_process_table_init(void) {
  for (uint32_t i = 0; i < OSAI_MAX_USER_PROCESSES; ++i) {
    reset_process_slot(&g_process_table[i]);
  }
  g_current_process = 0;
  g_process_transition_count = 0;
  g_process_loaded_count = 0;
  g_process_runnable_count = 0;
  g_process_running_count = 0;
  g_process_waiting_count = 0;
  g_process_exited_count = 0;
  g_process_failed_count = 0;
  g_process_reclaim_count = 0;
  g_process_scheduled_count = 0;
  g_process_wait_count = 0;
  g_process_wake_count = 0;
  klog("user: process table initialized slots=%u\n", OSAI_MAX_USER_PROCESSES);
}

void user_process_lifecycle_self_test(void) {
  kassert(validate_process_transition(OSAI_USER_PROCESS_EMPTY,
                                      OSAI_USER_PROCESS_LOADED) == OSAI_OK);
  kassert(validate_process_transition(OSAI_USER_PROCESS_LOADED,
                                      OSAI_USER_PROCESS_RUNNABLE) == OSAI_OK);
  kassert(validate_process_transition(OSAI_USER_PROCESS_RUNNABLE,
                                      OSAI_USER_PROCESS_RUNNING) == OSAI_OK);
  kassert(validate_process_transition(OSAI_USER_PROCESS_RUNNING,
                                      OSAI_USER_PROCESS_WAITING) == OSAI_OK);
  kassert(validate_process_transition(OSAI_USER_PROCESS_WAITING,
                                      OSAI_USER_PROCESS_RUNNABLE) == OSAI_OK);
  kassert(validate_process_transition(OSAI_USER_PROCESS_RUNNING,
                                      OSAI_USER_PROCESS_EXITED) == OSAI_OK);
  kassert(validate_process_transition(OSAI_USER_PROCESS_RUNNING,
                                      OSAI_USER_PROCESS_FAILED) == OSAI_OK);
  kassert(validate_process_transition(OSAI_USER_PROCESS_EMPTY,
                                      OSAI_USER_PROCESS_RUNNING) ==
          OSAI_ERR_INVALID);
  kassert(validate_process_transition(OSAI_USER_PROCESS_RUNNABLE,
                                      OSAI_USER_PROCESS_EXITED) ==
          OSAI_ERR_INVALID);
  kassert(validate_process_transition(OSAI_USER_PROCESS_EXITED,
                                      OSAI_USER_PROCESS_RUNNING) ==
          OSAI_ERR_INVALID);
  kassert(validate_process_transition(OSAI_USER_PROCESS_FAILED,
                                      OSAI_USER_PROCESS_RUNNING) ==
          OSAI_ERR_INVALID);
  klog("user: process lifecycle invalid/failed transition self-test passed\n");
}

void user_scheduler_self_test(void) {
  kassert(validate_process_transition(OSAI_USER_PROCESS_LOADED,
                                      OSAI_USER_PROCESS_RUNNABLE) == OSAI_OK);
  kassert(validate_process_transition(OSAI_USER_PROCESS_RUNNABLE,
                                      OSAI_USER_PROCESS_WAITING) == OSAI_OK);
  kassert(validate_process_transition(OSAI_USER_PROCESS_WAITING,
                                      OSAI_USER_PROCESS_RUNNABLE) == OSAI_OK);
  kassert(validate_process_transition(OSAI_USER_PROCESS_EMPTY,
                                      OSAI_USER_PROCESS_WAITING) ==
          OSAI_ERR_INVALID);
  klog("scheduler: lifecycle self-test passed\n");
}

const osai_user_process_t *user_current_process(void) {
  return g_current_process;
}

osai_status_t user_process_has_capability(uint64_t capability) {
  if (g_current_process == 0 ||
      (g_current_process->capability_mask & capability) != capability) {
    return OSAI_ERR_INVALID;
  }
  return OSAI_OK;
}

void user_process_note_syscall(uint32_t rejected) {
  if (g_current_process != 0) {
    ++g_current_process->syscall_count;
    if (rejected != 0) {
      ++g_current_process->rejected_syscall_count;
    }
  }
}

uint64_t user_process_note_exit(int exit_code) {
  if (g_current_process != 0) {
    transition_process(g_current_process,
                       exit_code == 0 ? OSAI_USER_PROCESS_EXITED
                                      : OSAI_USER_PROCESS_FAILED,
                       exit_code);
  }
  return OSAI_USER_EXIT_RETURN_MAGIC | ((uint64_t)(uint32_t)exit_code);
}

static osai_status_t validate_ehdr(const osai_initramfs_file_t *file,
                                   const elf64_ehdr_t **out) {
  if (file == 0 || file->base == 0 || file->size < sizeof(elf64_ehdr_t) ||
      file->executable == 0) {
    return OSAI_ERR_INVALID;
  }

  const elf64_ehdr_t *ehdr = (const elf64_ehdr_t *)file->base;
  if (elf_magic(ehdr->ident) != ELF_MAGIC || ehdr->ident[4] != 2 ||
      ehdr->ident[5] != 1 || ehdr->type != ET_EXEC ||
      ehdr->machine != EM_AARCH64 || ehdr->phentsize != sizeof(elf64_phdr_t) ||
      ehdr->phnum == 0 ||
      ehdr->phoff + ((uint64_t)ehdr->phnum * ehdr->phentsize) > file->size) {
    return OSAI_ERR_INVALID;
  }
  if (ehdr->entry < OSAI_USER_BASE || ehdr->entry >= OSAI_USER_LIMIT) {
    return OSAI_ERR_INVALID;
  }

  *out = ehdr;
  return OSAI_OK;
}

static uint32_t flags_from_phdr(const elf64_phdr_t *phdr) {
  uint32_t flags = OSAI_VMM_PRESENT | OSAI_VMM_USER;
  if ((phdr->flags & PF_W) != 0) {
    flags |= OSAI_VMM_WRITABLE;
  }
  if ((phdr->flags & PF_X) != 0) {
    flags |= OSAI_VMM_EXECUTABLE;
  }
  (void)PF_R;
  return flags;
}

static osai_status_t load_segment(const osai_initramfs_file_t *file,
                                  const elf64_phdr_t *phdr,
                                  osai_user_process_t *process) {
  if (phdr->memsz < phdr->filesz ||
      phdr->offset + phdr->filesz > file->size ||
      phdr->vaddr < OSAI_USER_BASE ||
      phdr->vaddr + phdr->memsz < phdr->vaddr ||
      phdr->vaddr + phdr->memsz > OSAI_USER_LIMIT) {
    return OSAI_ERR_INVALID;
  }

  uint64_t map_start = align_down(phdr->vaddr, PAGE_SIZE);
  uint64_t map_end = align_up(phdr->vaddr + phdr->memsz, PAGE_SIZE);
  uint64_t source_start = phdr->offset;
  uint32_t flags = flags_from_phdr(phdr);

  for (uint64_t va = map_start; va < map_end; va += PAGE_SIZE) {
    void *page = pmm_alloc_page();
    if (page == 0) {
      return OSAI_ERR_NO_MEMORY;
    }
    bytes_zero(page, PAGE_SIZE);

    uint64_t page_file_start = 0;
    uint64_t page_file_end = 0;
    uint64_t seg_file_start_va = phdr->vaddr;
    uint64_t seg_file_end_va = phdr->vaddr + phdr->filesz;
    if (va < seg_file_end_va && va + PAGE_SIZE > seg_file_start_va) {
      uint64_t copy_va_start = va > seg_file_start_va ? va : seg_file_start_va;
      uint64_t copy_va_end = va + PAGE_SIZE < seg_file_end_va
                                 ? va + PAGE_SIZE
                                 : seg_file_end_va;
      page_file_start = source_start + (copy_va_start - phdr->vaddr);
      page_file_end = source_start + (copy_va_end - phdr->vaddr);
      bytes_copy((uint8_t *)page + (copy_va_start - va),
                 (const uint8_t *)file->base + page_file_start,
                 page_file_end - page_file_start);
    }

    if (vmm_map_page(va, (uint64_t)(uintptr_t)page, flags) != OSAI_OK) {
      pmm_free_page(page);
      return OSAI_ERR_INVALID;
    }
  }

  track_process_mapping(process, map_start, map_end);
  return OSAI_OK;
}

static osai_status_t map_user_stack(osai_user_process_t *process) {
  uint64_t guard_low = OSAI_USER_STACK_TOP - (3U * PAGE_SIZE);
  uint64_t stack = OSAI_USER_STACK_TOP - (2U * PAGE_SIZE);
  uint64_t guard_high = OSAI_USER_STACK_TOP - PAGE_SIZE;
  void *stack_page = pmm_alloc_page();
  if (stack_page == 0) {
    return OSAI_ERR_NO_MEMORY;
  }
  bytes_zero(stack_page, PAGE_SIZE);

  kassert(vmm_unmap_page(guard_low) == OSAI_OK);
  kassert(vmm_unmap_page(guard_high) == OSAI_OK);
  if (vmm_map_page(stack, (uint64_t)(uintptr_t)stack_page,
                   OSAI_VMM_PRESENT | OSAI_VMM_USER |
                       OSAI_VMM_WRITABLE) != OSAI_OK) {
    pmm_free_page(stack_page);
    return OSAI_ERR_INVALID;
  }

  uint64_t translated = 0;
  uint32_t flags = 0;
  kassert(vmm_translate(guard_low, &translated, &flags) == OSAI_ERR_INVALID);
  kassert(vmm_translate(guard_high, &translated, &flags) == OSAI_ERR_INVALID);
  process->stack_top = stack + PAGE_SIZE;
  process->stack_guard_low = guard_low;
  process->stack_guard_high = guard_high;
  track_process_mapping(process, stack, stack + PAGE_SIZE);
  return OSAI_OK;
}

osai_status_t user_load_process(const osai_initramfs_file_t *file,
                                uint32_t pid, uint64_t capability_mask,
                                osai_user_process_t *process) {
  const elf64_ehdr_t *ehdr = 0;
  if (process == 0 || pid == 0 || pid > OSAI_MAX_USER_PROCESSES ||
      validate_ehdr(file, &ehdr) != OSAI_OK) {
    return OSAI_ERR_INVALID;
  }

  reset_process_slot(process);
  process->pid = pid;
  process->name = file->path;
  process->capability_mask = capability_mask;

  const uint8_t *base = (const uint8_t *)file->base;
  for (uint16_t i = 0; i < ehdr->phnum; ++i) {
    const elf64_phdr_t *phdr =
        (const elf64_phdr_t *)(const void *)(base + ehdr->phoff +
                                             ((uint64_t)i * ehdr->phentsize));
    if (phdr->type == PT_LOAD && load_segment(file, phdr, process) != OSAI_OK) {
      return OSAI_ERR_INVALID;
    }
  }

  process->entry = ehdr->entry;
  process->state = OSAI_USER_PROCESS_LOADED;
  process->exit_code = 0;
  process->syscall_count = 0;
  process->rejected_syscall_count = 0;
  if (map_user_stack(process) != OSAI_OK) {
    return OSAI_ERR_INVALID;
  }

  osai_user_process_t *slot = &g_process_table[pid - 1U];
  copy_process(slot, process);
  slot->state = OSAI_USER_PROCESS_EMPTY;
  transition_process(slot, OSAI_USER_PROCESS_LOADED, 0);
  copy_process(process, slot);
  klog("user: loaded %s ELF pid=%u caps=0x%lx entry=0x%lx stack=0x%lx guard=[0x%lx,0x%lx]\n",
       process->name, process->pid, process->capability_mask, process->entry,
       process->stack_top, process->stack_guard_low, process->stack_guard_high);
  return OSAI_OK;
}

osai_status_t user_load_init(const osai_initramfs_file_t *file,
                             osai_user_process_t *process) {
  return user_load_process(file, 1,
                           OSAI_CAP_LOG | OSAI_CAP_EXIT | OSAI_CAP_OSCTL,
                           process);
}

osai_status_t user_process_snapshot(uint32_t pid, osai_user_process_t *process) {
  if (process == 0 || pid == 0 || pid > OSAI_MAX_USER_PROCESSES) {
    return OSAI_ERR_INVALID;
  }

  const osai_user_process_t *slot = &g_process_table[pid - 1U];
  if (slot->pid != pid || slot->state == OSAI_USER_PROCESS_EMPTY) {
    return OSAI_ERR_INVALID;
  }

  copy_process(process, slot);
  return OSAI_OK;
}

osai_status_t user_process_make_runnable(uint32_t pid, uint32_t parent_pid) {
  if (pid == 0 || pid > OSAI_MAX_USER_PROCESSES || parent_pid == pid) {
    return OSAI_ERR_INVALID;
  }

  osai_user_process_t *process = &g_process_table[pid - 1U];
  if (process->pid != pid) {
    return OSAI_ERR_INVALID;
  }

  process->parent_pid = parent_pid;
  transition_process(process, OSAI_USER_PROCESS_RUNNABLE, 0);
  klog("scheduler: process pid=%u parent=%u runnable name=%s\n", process->pid,
       process->parent_pid, process->name != 0 ? process->name : "(none)");
  return OSAI_OK;
}

osai_status_t user_process_wait(uint32_t pid) {
  if (pid == 0 || pid > OSAI_MAX_USER_PROCESSES) {
    return OSAI_ERR_INVALID;
  }

  osai_user_process_t *process = &g_process_table[pid - 1U];
  if (process->pid != pid) {
    return OSAI_ERR_INVALID;
  }

  transition_process(process, OSAI_USER_PROCESS_WAITING, process->exit_code);
  ++g_process_wait_count;
  klog("scheduler: process pid=%u waiting waits=%lu\n", pid,
       g_process_wait_count);
  return OSAI_OK;
}

osai_status_t user_process_wake(uint32_t pid) {
  if (pid == 0 || pid > OSAI_MAX_USER_PROCESSES) {
    return OSAI_ERR_INVALID;
  }

  osai_user_process_t *process = &g_process_table[pid - 1U];
  if (process->pid != pid) {
    return OSAI_ERR_INVALID;
  }

  transition_process(process, OSAI_USER_PROCESS_RUNNABLE, process->exit_code);
  ++g_process_wake_count;
  klog("scheduler: process pid=%u woken wakes=%lu\n", pid,
       g_process_wake_count);
  return OSAI_OK;
}

int user_process_run(const osai_user_process_t *process) {
  kassert(process != 0);
  kassert(process->pid != 0 && process->pid <= OSAI_MAX_USER_PROCESSES);
  g_current_process = &g_process_table[process->pid - 1U];
  if (g_current_process->pid != process->pid ||
      g_current_process->state == OSAI_USER_PROCESS_EMPTY) {
    copy_process(g_current_process, process);
  }
  uint64_t entry = g_current_process->entry;
  uint64_t stack = g_current_process->stack_top;
  transition_process(g_current_process, OSAI_USER_PROCESS_RUNNING, 0);
  ++g_current_process->scheduler_ticks;
  ++g_process_scheduled_count;

  klog("scheduler: dispatch pid=%u parent=%u name=%s ticks=%lu scheduled=%lu\n",
       g_current_process->pid, g_current_process->parent_pid,
       g_current_process->name != 0 ? g_current_process->name : "(none)",
       g_current_process->scheduler_ticks, g_process_scheduled_count);

  klog("user: entering EL0 %s pid=%u entry=0x%lx stack=0x%lx\n",
       g_current_process->name, g_current_process->pid, entry, stack);

  uint64_t encoded = aarch64_enter_user(entry, stack);
  kassert((encoded & OSAI_USER_EXIT_RETURN_MASK) ==
          OSAI_USER_EXIT_RETURN_MAGIC);
  int exit_code = (int)(uint32_t)encoded;

  klog("user: kernel resumed after EL0 pid=%u state=%s exit_code=%u transitions=%lu\n",
       g_current_process->pid, process_state_name(g_current_process->state),
       (unsigned)exit_code, g_process_transition_count);
  return exit_code;
}

void user_process_reclaim_address_space(const osai_user_process_t *process) {
  if (process == 0 || process->mapped_low == 0 ||
      process->mapped_high <= process->mapped_low) {
    return;
  }

  for (uint64_t va = process->mapped_low; va < process->mapped_high;
       va += PAGE_SIZE) {
    uint64_t physical = 0;
    uint32_t flags = 0;
    if (vmm_translate(va, &physical, &flags) == OSAI_OK &&
        (flags & OSAI_VMM_USER) != 0) {
      kassert(vmm_unmap_page(va) == OSAI_OK);
      pmm_free_page((void *)(uintptr_t)physical);
    }
  }
  ++g_process_reclaim_count;
  klog("user: reclaimed address space pid=%u range=[0x%lx,0x%lx)\n",
       process->pid, process->mapped_low, process->mapped_high);
}

uint64_t user_process_transition_count(void) {
  return g_process_transition_count;
}

uint64_t user_process_loaded_count(void) {
  return g_process_loaded_count;
}

uint64_t user_process_runnable_count(void) {
  return g_process_runnable_count;
}

uint64_t user_process_running_count(void) {
  return g_process_running_count;
}

uint64_t user_process_waiting_count(void) {
  return g_process_waiting_count;
}

uint64_t user_process_exited_count(void) {
  return g_process_exited_count;
}

uint64_t user_process_failed_count(void) {
  return g_process_failed_count;
}

uint64_t user_process_reclaim_count(void) {
  return g_process_reclaim_count;
}

uint64_t user_process_scheduled_count(void) {
  return g_process_scheduled_count;
}

uint64_t user_process_wait_count(void) {
  return g_process_wait_count;
}

uint64_t user_process_wake_count(void) {
  return g_process_wake_count;
}

uint64_t user_process_active_count(void) {
  uint64_t active = 0;
  for (uint32_t i = 0; i < OSAI_MAX_USER_PROCESSES; ++i) {
    osai_user_process_state_t state = g_process_table[i].state;
    if (state == OSAI_USER_PROCESS_LOADED ||
        state == OSAI_USER_PROCESS_RUNNABLE ||
        state == OSAI_USER_PROCESS_RUNNING ||
        state == OSAI_USER_PROCESS_WAITING) {
      ++active;
    }
  }
  return active;
}
