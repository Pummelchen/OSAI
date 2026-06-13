#include <osai/core_lease.h>
#include <osai/klog.h>

#define MAX_CORE_LEASES 8U

typedef struct core_lease {
  uint32_t owner_id;
  uint32_t core_mask;
  uint32_t active;
} core_lease_t;

static core_lease_t g_leases[MAX_CORE_LEASES];

void core_lease_init(void) {
  for (uint32_t i = 0; i < MAX_CORE_LEASES; ++i) {
    g_leases[i].owner_id = 0;
    g_leases[i].core_mask = 0;
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

osai_status_t core_lease_acquire(uint32_t owner_id, uint32_t core_mask) {
  if (core_mask == 0 || (core_lease_used_mask() & core_mask) != 0) {
    return OSAI_ERR_INVALID;
  }

  for (uint32_t i = 0; i < MAX_CORE_LEASES; ++i) {
    if (g_leases[i].active == 0) {
      g_leases[i].owner_id = owner_id;
      g_leases[i].core_mask = core_mask;
      g_leases[i].active = 1;
      klog("core-lease: owner=%u mask=0x%x acquired\n", owner_id, core_mask);
      return OSAI_OK;
    }
  }

  return OSAI_ERR_NO_MEMORY;
}

osai_status_t core_lease_release(uint32_t owner_id) {
  for (uint32_t i = 0; i < MAX_CORE_LEASES; ++i) {
    if (g_leases[i].active != 0 && g_leases[i].owner_id == owner_id) {
      klog("core-lease: owner=%u mask=0x%x released\n",
           owner_id, g_leases[i].core_mask);
      g_leases[i].active = 0;
      g_leases[i].core_mask = 0;
      return OSAI_OK;
    }
  }

  return OSAI_ERR_INVALID;
}
