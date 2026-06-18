#ifndef XAIOS_NUMA_H
#define XAIOS_NUMA_H

#include <xaios/boot_info.h>
#include <xaios/types.h>

#define XAIOS_NUMA_MAX_NODES 8U
#define XAIOS_NUMA_MAX_FREE_PER_NODE UINT64_C(262144)

typedef struct xaios_numa_node {
  uint32_t node_id;
  uint32_t online;
  uint64_t phys_start;
  uint64_t phys_end;
  uint64_t total_pages;
  uint64_t free_count;
  uint64_t cpu_mask;
  uint64_t free_stack[XAIOS_NUMA_MAX_FREE_PER_NODE];
} xaios_numa_node_t;

void numa_init(const xaios_boot_info_t *boot);
uint32_t numa_node_count(void);
const xaios_numa_node_t *numa_node(uint32_t node_id);
uint32_t numa_node_of_phys(uint64_t phys_addr);
void *numa_alloc_page_on_node(uint32_t node_id);
void numa_free_page(void *page);
void numa_self_test(void);

#endif
