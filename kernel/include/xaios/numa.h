#ifndef XAIOS_NUMA_H
#define XAIOS_NUMA_H

#include <xaios/boot_info.h>
#include <xaios/spinlock.h>
#include <xaios/types.h>

/*
 * NUMA node capacity: up to 2,048 nodes.
 * Actual node count detected from UEFI memory map at boot.
 * 2,048 nodes x 4GB per node = 8 TB total system capacity.
 * BSS: 256 MB at max scale (2,048 x 128 KB bitmaps).
 * Requires >= 1 GB RAM for testing, >= 64 GB for production.
 */
#define XAIOS_NUMA_MAX_NODES 2048U

/*
 * Bitmap page allocator: 1 bit per 4KB page.
 * XAIOS_NUMA_BITMAP_BITS = 1,048,576 entries -> supports up to 4GB per node.
 * 2,048 nodes x 4GB = 8 TB total system capacity.
 * Bitmap memory cost: 128 KB per node (2,048 nodes = 256 MB total in BSS).
 * Detected RAM via UEFI memory map; pages beyond bitmap capacity are logged.
 * For systems with >4GB per NUMA node, increase XAIOS_NUMA_BITMAP_BITS.
 */
#define XAIOS_NUMA_BITMAP_BITS UINT64_C(1048576)
#define XAIOS_NUMA_BITMAP_WORDS (XAIOS_NUMA_BITMAP_BITS / 64U)

typedef struct xaios_numa_node {
  uint32_t node_id;
  uint32_t online;
  uint64_t phys_start;
  uint64_t phys_end;
  uint64_t total_pages;
  uint64_t free_count;
  uint64_t cpu_mask;
  uint64_t alloc_hint; /* next-fit hint for faster allocation */
  xaios_spinlock_t lock;
  uint64_t bitmap[XAIOS_NUMA_BITMAP_WORDS]; /* 1 = free page */
} xaios_numa_node_t;

void numa_init(const xaios_boot_info_t *boot);
uint32_t numa_node_count(void);
const xaios_numa_node_t *numa_node(uint32_t node_id);
uint32_t numa_node_of_phys(uint64_t phys_addr);
void *numa_alloc_page_on_node(uint32_t node_id);
void numa_free_page(void *page);
void numa_self_test(void);

#endif
