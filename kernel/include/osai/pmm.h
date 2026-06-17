#ifndef OSAI_PMM_H
#define OSAI_PMM_H

#include <osai/boot_info.h>
#include <osai/types.h>

void pmm_init(const osai_boot_info_t *boot);
void *pmm_alloc_page(void);
void *pmm_alloc_page_on_node(uint32_t node_id);
void *pmm_alloc_page_near(uint32_t preferred_node);
void pmm_free_page(void *page);
uint32_t pmm_node_of_page(void *page);
uint64_t pmm_total_pages(void);
uint64_t pmm_free_pages(void);

#endif
