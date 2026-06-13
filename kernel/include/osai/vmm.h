#ifndef OSAI_VMM_H
#define OSAI_VMM_H

#include <osai/boot_info.h>
#include <osai/status.h>
#include <osai/types.h>

#define OSAI_VMM_PRESENT UINT32_C(1)
#define OSAI_VMM_WRITABLE UINT32_C(1 << 1)
#define OSAI_VMM_EXECUTABLE UINT32_C(1 << 2)
#define OSAI_VMM_DEVICE UINT32_C(1 << 3)

void vmm_init(const osai_boot_info_t *boot);
osai_status_t vmm_translate(uint64_t virtual_address, uint64_t *physical_address,
                            uint32_t *flags);

#endif
