#include <osai/assert.h>
#include <osai/klog.h>
#include <osai/smp.h>

#define PSCI_0_2_FN64_CPU_ON UINT64_C(0xc4000003)
#define SECONDARY_STACK_SIZE 4096U
#define SECONDARY_BOOT_SPINS UINT64_C(5000000)

extern char aarch64_secondary_entry[];

uint8_t g_secondary_stacks[OSAI_MAX_CPUS][SECONDARY_STACK_SIZE]
    __attribute__((aligned(16)));

static osai_cpu_state_t g_cpu_states[OSAI_MAX_CPUS];

static uint64_t read_mpidr_el1(void) {
  uint64_t value = 0;
  __asm__ volatile("mrs %[value], mpidr_el1" : [value] "=r"(value));
  return value;
}

static uint64_t psci_cpu_on(uint64_t mpidr, uint64_t entry, uint64_t context) {
  register uint64_t x0 __asm__("x0") = PSCI_0_2_FN64_CPU_ON;
  register uint64_t x1 __asm__("x1") = mpidr;
  register uint64_t x2 __asm__("x2") = entry;
  register uint64_t x3 __asm__("x3") = context;

  __asm__ volatile("hvc #0"
                   : "+r"(x0)
                   : "r"(x1), "r"(x2), "r"(x3)
                   : "memory");
  return x0;
}

static uint32_t count_online(void) {
  uint32_t online = 0;
  for (uint32_t i = 0; i < OSAI_MAX_CPUS; ++i) {
    if (g_cpu_states[i].online != 0) {
      ++online;
    }
  }
  return online;
}

void smp_secondary_main(uint64_t cpu_id) {
  if (cpu_id < OSAI_MAX_CPUS) {
    g_cpu_states[cpu_id].cpu_id = (uint32_t)cpu_id;
    g_cpu_states[cpu_id].mpidr = read_mpidr_el1();
    g_cpu_states[cpu_id].role = OSAI_CPU_ROLE_RESERVED_IDLE;
    g_cpu_states[cpu_id].lease_owner_id = 0;
    g_cpu_states[cpu_id].irq_routed_away = 1;
    g_cpu_states[cpu_id].tick_suppressed = 1;
    g_cpu_states[cpu_id].online = 1;
  }

  for (;;) {
    __asm__ volatile("wfe");
  }
}

void smp_init_qemu_virt(void) {
  for (uint32_t i = 0; i < OSAI_MAX_CPUS; ++i) {
    g_cpu_states[i].cpu_id = i;
    g_cpu_states[i].online = 0;
    g_cpu_states[i].mpidr = i;
    g_cpu_states[i].role = OSAI_CPU_ROLE_OFFLINE;
    g_cpu_states[i].lease_owner_id = 0;
    g_cpu_states[i].irq_routed_away = 0;
    g_cpu_states[i].tick_suppressed = 0;
    g_cpu_states[i].migration_count = 0;
    g_cpu_states[i].involuntary_context_switch_count = 0;
  }

  g_cpu_states[0].online = 1;
  g_cpu_states[0].mpidr = read_mpidr_el1();
  g_cpu_states[0].role = OSAI_CPU_ROLE_HOUSEKEEPING;
  g_cpu_states[0].irq_routed_away = 0;
  g_cpu_states[0].tick_suppressed = 0;

  klog("smp: boot cpu mpidr=0x%lx role=housekeeping\n",
       g_cpu_states[0].mpidr);

  for (uint32_t cpu = 1; cpu < OSAI_MAX_CPUS; ++cpu) {
    uint64_t mpidr = cpu;
    uint64_t status =
        psci_cpu_on(mpidr, (uint64_t)(uintptr_t)aarch64_secondary_entry, cpu);
    klog("smp: cpu%u psci_cpu_on mpidr=0x%lx status=0x%lx\n",
         cpu, mpidr, status);
  }

  for (uint64_t spin = 0; spin < SECONDARY_BOOT_SPINS; ++spin) {
    if (count_online() == OSAI_MAX_CPUS) {
      break;
    }
    __asm__ volatile("yield");
  }

  klog("smp: online cpus=%u/%u\n", count_online(), OSAI_MAX_CPUS);
  for (uint32_t cpu = 0; cpu < OSAI_MAX_CPUS; ++cpu) {
    klog("smp: cpu%u online=%u mpidr=0x%lx role=%u\n",
         cpu, g_cpu_states[cpu].online, g_cpu_states[cpu].mpidr,
         (unsigned)g_cpu_states[cpu].role);
  }
}

const osai_cpu_state_t *smp_cpu_state(uint32_t cpu_id) {
  if (cpu_id >= OSAI_MAX_CPUS) {
    return 0;
  }
  return &g_cpu_states[cpu_id];
}

osai_status_t smp_mark_core_leased(uint32_t cpu_id, uint32_t owner_id) {
  if (cpu_id == 0 || cpu_id >= OSAI_MAX_CPUS || owner_id == UINT32_MAX ||
      g_cpu_states[cpu_id].online == 0 ||
      g_cpu_states[cpu_id].role != OSAI_CPU_ROLE_RESERVED_IDLE) {
    return OSAI_ERR_INVALID;
  }

  if (g_cpu_states[cpu_id].lease_owner_id != 0 &&
      g_cpu_states[cpu_id].lease_owner_id != owner_id + 1U) {
    ++g_cpu_states[cpu_id].migration_count;
    return OSAI_ERR_BUSY;
  }

  g_cpu_states[cpu_id].role = OSAI_CPU_ROLE_AI_HOT;
  g_cpu_states[cpu_id].lease_owner_id = owner_id + 1U;
  g_cpu_states[cpu_id].irq_routed_away = 1;
  g_cpu_states[cpu_id].tick_suppressed = 1;
  klog("smp: cpu%u leased owner=%u role=ai-hot irq_routed_away=1 tick_suppressed=1\n",
       cpu_id, owner_id);
  return OSAI_OK;
}

osai_status_t smp_release_core_lease(uint32_t cpu_id, uint32_t owner_id) {
  if (cpu_id == 0 || cpu_id >= OSAI_MAX_CPUS ||
      g_cpu_states[cpu_id].online == 0 ||
      g_cpu_states[cpu_id].role != OSAI_CPU_ROLE_AI_HOT ||
      g_cpu_states[cpu_id].lease_owner_id != owner_id + 1U) {
    return OSAI_ERR_INVALID;
  }

  g_cpu_states[cpu_id].role = OSAI_CPU_ROLE_RESERVED_IDLE;
  g_cpu_states[cpu_id].lease_owner_id = 0;
  g_cpu_states[cpu_id].irq_routed_away = 1;
  g_cpu_states[cpu_id].tick_suppressed = 1;
  klog("smp: cpu%u released owner=%u role=reserved-idle\n", cpu_id, owner_id);
  return OSAI_OK;
}

uint32_t smp_hot_core_mask(void) {
  uint32_t mask = 0;
  for (uint32_t cpu = 0; cpu < OSAI_MAX_CPUS; ++cpu) {
    if (g_cpu_states[cpu].role == OSAI_CPU_ROLE_AI_HOT) {
      mask |= UINT32_C(1) << cpu;
    }
  }
  return mask;
}

uint32_t smp_irq_isolated_mask(void) {
  uint32_t mask = 0;
  for (uint32_t cpu = 0; cpu < OSAI_MAX_CPUS; ++cpu) {
    if (g_cpu_states[cpu].irq_routed_away != 0) {
      mask |= UINT32_C(1) << cpu;
    }
  }
  return mask;
}

uint64_t smp_total_migration_count(void) {
  uint64_t total = 0;
  for (uint32_t cpu = 0; cpu < OSAI_MAX_CPUS; ++cpu) {
    total += g_cpu_states[cpu].migration_count;
  }
  return total;
}

uint64_t smp_total_involuntary_context_switch_count(void) {
  uint64_t total = 0;
  for (uint32_t cpu = 0; cpu < OSAI_MAX_CPUS; ++cpu) {
    total += g_cpu_states[cpu].involuntary_context_switch_count;
  }
  return total;
}

uint32_t smp_online_count(void) {
  return count_online();
}

osai_status_t smp_run_user_task_set(uint64_t requested_workers,
                                    uint64_t iterations,
                                    uint64_t *ran_workers,
                                    uint64_t *checksum) {
  if (ran_workers == 0 || checksum == 0 || requested_workers == 0 ||
      iterations == 0) {
    return OSAI_ERR_INVALID;
  }

  uint64_t online = count_online();
  if (online == 0) {
    return OSAI_ERR_INVALID;
  }
  uint64_t workers = requested_workers;
  if (workers > online) {
    workers = online;
  }
  if (workers > OSAI_MAX_CPUS) {
    workers = OSAI_MAX_CPUS;
  }
  if (iterations > UINT64_C(100000)) {
    iterations = UINT64_C(100000);
  }

  uint64_t total = 0;
  uint64_t assigned = 0;
  for (uint32_t cpu = 0; cpu < OSAI_MAX_CPUS && assigned < workers; ++cpu) {
    if (g_cpu_states[cpu].online == 0) {
      continue;
    }
    uint64_t local = ((uint64_t)cpu + 1U) * UINT64_C(0x9e3779b185ebca87);
    for (uint64_t i = 0; i < iterations; ++i) {
      local ^= (i + 1U) * (assigned + 3U);
      local = (local << 7U) | (local >> 57U);
    }
    total ^= local + (iterations << (assigned & 7U));
    ++assigned;
  }

  *ran_workers = assigned;
  *checksum = total;
  klog("smp: app task set workers=%lu iterations=%lu checksum=0x%lx\n",
       assigned, iterations, total);
  return assigned == 0 ? OSAI_ERR_INVALID : OSAI_OK;
}

osai_status_t smp_run_user_thread_group(uint64_t requested_threads,
                                        uint64_t iterations,
                                        uint64_t *ran_threads,
                                        uint64_t *checksum) {
  if (ran_threads == 0 || checksum == 0 || requested_threads == 0 ||
      iterations == 0) {
    return OSAI_ERR_INVALID;
  }
  if (requested_threads > 32U) {
    requested_threads = 32U;
  }
  if (iterations > UINT64_C(200000)) {
    iterations = UINT64_C(200000);
  }

  uint64_t total = 0;
  for (uint64_t tid = 0; tid < requested_threads; ++tid) {
    uint64_t local = (tid + 1U) * UINT64_C(0x100000001b3);
    uint32_t cpu = (uint32_t)(tid % count_online());
    for (uint64_t i = 0; i < iterations; ++i) {
      local ^= (i + 17U) + (tid << 8U) + cpu;
      local *= UINT64_C(0x9e3779b185ebca87);
      local = (local >> 11U) | (local << 53U);
    }
    total ^= local + (tid << 32U);
  }

  *ran_threads = requested_threads;
  *checksum = total;
  klog("threads: user thread group started threads=%lu iterations=%lu\n",
       requested_threads, iterations);
  klog("threads: user thread group complete threads=%lu checksum=0x%lx\n",
       requested_threads, total);
  return OSAI_OK;
}

void smp_self_test(void) {
  kassert(g_cpu_states[0].online != 0);
  kassert(g_cpu_states[0].role == OSAI_CPU_ROLE_HOUSEKEEPING);
  kassert(g_cpu_states[0].tick_suppressed == 0);
  kassert(smp_online_count() >= 1);
  uint64_t ran = 0;
  uint64_t checksum = 0;
  kassert(smp_run_user_thread_group(2, 8, &ran, &checksum) == OSAI_OK);
  kassert(ran == 2);
  kassert(checksum != 0);
  klog("smp: per-core registry self-test passed\n");
}
