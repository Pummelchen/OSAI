#ifndef OSAI_PMM_H
#define OSAI_PMM_H

#include <osai/boot_info.h>
#include <osai/types.h>

void pmm_init(const osai_boot_info_t *boot);
void *pmm_alloc_page(void);
void pmm_free_page(void *page);
uint64_t pmm_total_pages(void);
uint64_t pmm_free_pages(void);

#endif
