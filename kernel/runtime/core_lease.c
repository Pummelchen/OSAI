#include <xaios/core_lease.h>
#include <xaios/assert.h>
#include <xaios/klog.h>
#include <xaios/smp.h>

#define MAX_CORE_LEASES 8U

typedef struct core_lease {
  uint32_t owner_id;
  uint32_t core_mask;
  uint32_t irq_isolated_mask;
  uint32_t active;
} core_lease_t;

static core_lease_t g_leases[MAX_CORE_LEASES];

void core_lease_init(void) {
  for (uint32_t i = 0; i < MAX_CORE_LEASES; ++i) {
    g_leases[i].owner_id = 0;
    g_leases[i].core_mask = 0;
    g_leases[i].irq_isolated_mask = 0;
    g_leases[i].active = 0;
  }
  klog("core-lease: initialized\n");
}

uint32_t core_lease_used_mask(void) {
  uint32_t mask = 0;
  for (uint32_t i = 0; i < MAX_CORE_LEASES; ++i) {
    if (g_leases[i].active != 0) {
      mask |= g_leases[i].core_mask;
    }
  }
  return mask;
}

static xaios_status_t validate_core_mask(uint32_t core_mask) {
  if (core_mask == 0 || (core_mask & 1U) != 0) {
    return XAIOS_ERR_INVALID;
  }
  for (uint32_t cpu = 0; cpu < XAIOS_MAX_CPUS; ++cpu) {
    if ((core_mask & (UINT32_C(1) << cpu)) != 0) {
      const xaios_cpu_state_t *state = smp_cpu_state(cpu);
      if (state == 0 || state->online == 0 ||
          state->role != XAIOS_CPU_ROLE_RESERVED_IDLE) {
        return XAIOS_ERR_INVALID;
      }
    }
  }
  if ((core_mask >> XAIOS_MAX_CPUS) != 0) {
    return XAIOS_ERR_INVALID;
  }
  return XAIOS_OK;
}

static void release_marked_cores(uint32_t owner_id, uint32_t marked_mask) {
  for (uint32_t cpu = 1; cpu < XAIOS_MAX_CPUS; ++cpu) {
    if ((marked_mask & (UINT32_C(1) << cpu)) != 0) {
      kassert(smp_release_core_lease(cpu, owner_id) == XAIOS_OK);
    }
  }
}

xaios_status_t core_lease_acquire(uint32_t owner_id, uint32_t core_mask) {
  if (validate_core_mask(core_mask) != XAIOS_OK ||
      (core_lease_used_mask() & core_mask) != 0) {
    return XAIOS_ERR_INVALID;
  }

  for (uint32_t i = 0; i < MAX_CORE_LEASES; ++i) {
    if (g_leases[i].active == 0) {
      uint32_t marked_mask = 0;
      for (uint32_t cpu = 1; cpu < XAIOS_MAX_CPUS; ++cpu) {
        if ((core_mask & (UINT32_C(1) << cpu)) != 0) {
          if (smp_mark_core_leased(cpu, owner_id) != XAIOS_OK) {
            release_marked_cores(owner_id, marked_mask);
            return XAIOS_ERR_INVALID;
          }
          marked_mask |= UINT32_C(1) << cpu;
        }
      }
      g_leases[i].owner_id = owner_id;
      g_leases[i].core_mask = core_mask;
      g_leases[i].irq_isolated_mask = marked_mask;
      g_leases[i].active = 1;
      klog("core-lease: owner=%u mask=0x%x acquired irq_isolated=0x%x migration_total=%lu context_switch_total=%lu\n",
           owner_id, core_mask, g_leases[i].irq_isolated_mask,
           core_lease_migration_count(),
           core_lease_involuntary_context_switch_count());
      return XAIOS_OK;
    }
  }

  return XAIOS_ERR_NO_MEMORY;
}

xaios_status_t core_lease_release(uint32_t owner_id) {
  for (uint32_t i = 0; i < MAX_CORE_LEASES; ++i) {
    if (g_leases[i].active != 0 && g_leases[i].owner_id == owner_id) {
      for (uint32_t cpu = 1; cpu < XAIOS_MAX_CPUS; ++cpu) {
        if ((g_leases[i].core_mask & (UINT32_C(1) << cpu)) != 0) {
          kassert(smp_release_core_lease(cpu, owner_id) == XAIOS_OK);
        }
      }
      klog("core-lease: owner=%u mask=0x%x released\n",
           owner_id, g_leases[i].core_mask);
      g_leases[i].active = 0;
      g_leases[i].core_mask = 0;
      g_leases[i].irq_isolated_mask = 0;
      return XAIOS_OK;
    }
  }

  return XAIOS_ERR_INVALID;
}

uint32_t core_lease_irq_isolated_mask(void) {
  uint32_t mask = 0;
  for (uint32_t i = 0; i < MAX_CORE_LEASES; ++i) {
    if (g_leases[i].active != 0) {
      mask |= g_leases[i].irq_isolated_mask;
    }
  }
  return mask;
}

uint64_t core_lease_migration_count(void) {
  return smp_total_migration_count();
}

uint64_t core_lease_involuntary_context_switch_count(void) {
  return smp_total_involuntary_context_switch_count();
}

void core_lease_self_test(void) {
  core_lease_init();
  kassert(core_lease_acquire(99, 0x2) == XAIOS_OK);
  kassert((core_lease_used_mask() & 0x2U) != 0);
  kassert((smp_hot_core_mask() & 0x2U) != 0);
  kassert((core_lease_irq_isolated_mask() & 0x2U) != 0);
  kassert(core_lease_migration_count() == 0);
  kassert(core_lease_involuntary_context_switch_count() == 0);
  kassert(core_lease_acquire(100, 0x2) == XAIOS_ERR_INVALID);
  kassert(core_lease_acquire(100, 0x1) == XAIOS_ERR_INVALID);
  kassert(core_lease_release(99) == XAIOS_OK);
  kassert((smp_hot_core_mask() & 0x2U) == 0);
  klog("core-lease: isolation self-test passed migration_total=%lu context_switch_total=%lu irq_isolated_mask=0x%x\n",
       core_lease_migration_count(),
       core_lease_involuntary_context_switch_count(), smp_irq_isolated_mask());
}
