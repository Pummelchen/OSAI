#ifndef OSAI_CORE_LEASE_H
#define OSAI_CORE_LEASE_H

#include <osai/status.h>
#include <osai/types.h>

void core_lease_init(void);
osai_status_t core_lease_acquire(uint32_t owner_id, uint32_t core_mask);
osai_status_t core_lease_release(uint32_t owner_id);
uint32_t core_lease_used_mask(void);

#endif
