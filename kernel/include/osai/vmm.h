#ifndef OSAI_VMM_H
#define OSAI_VMM_H

#include <osai/boot_info.h>
#include <osai/status.h>
#include <osai/types.h>

#define OSAI_VMM_PRESENT UINT32_C(1)
#define OSAI_VMM_WRITABLE UINT32_C(1 << 1)
#define OSAI_VMM_EXECUTABLE UINT32_C(1 << 2)
#define OSAI_VMM_DEVICE UINT32_C(1 << 3)
#define OSAI_VMM_USER UINT32_C(1 << 4)

#define OSAI_USER_BASE UINT64_C(0x41000000)
#define OSAI_USER_LIMIT UINT64_C(0x50000000)
#define OSAI_USER_STACK_TOP UINT64_C(0x4f000000)

void vmm_init(const osai_boot_info_t *boot);
osai_status_t vmm_translate(uint64_t virtual_address, uint64_t *physical_address,
                            uint32_t *flags);
osai_status_t vmm_map_page(uint64_t virtual_address, uint64_t physical_address,
                           uint32_t flags);
osai_status_t vmm_unmap_page(uint64_t virtual_address);
osai_status_t vmm_validate_user_buffer(uint64_t virtual_address, uint64_t size,
                                       uint32_t required_flags);
void vmm_self_test(void);

#endif
