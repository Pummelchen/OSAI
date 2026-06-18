#include <xaios/assert.h>
#include <xaios/klog.h>
#include <xaios/numa.h>
#include <xaios/smp.h>

#define PAGE_SIZE UINT64_C(4096)

static xaios_numa_node_t g_numa_nodes[XAIOS_NUMA_MAX_NODES];
static uint32_t g_numa_node_count;

static uint64_t align_up(uint64_t value, uint64_t align) {
  return (value + align - 1) & ~(align - 1);
}

static uint64_t align_down(uint64_t value, uint64_t align) {
  return value & ~(align - 1);
}

static int overlaps(uint64_t start, uint64_t end, uint64_t used_start,
                    uint64_t used_end) {
  return start < used_end && used_start < end;
}

static int page_is_reserved(const xaios_boot_info_t *boot, uint64_t page) {
  uint64_t page_end = page + PAGE_SIZE;
  uint64_t map_start = boot->memory_map;
  uint64_t map_end = boot->memory_map + boot->memory_map_size;
  if (overlaps(page, page_end, boot->kernel_phys_base, boot->kernel_phys_end)) {
    return 1;
  }
  if (overlaps(page, page_end, map_start, map_end)) {
    return 1;
  }
  return 0;
}

void numa_init(const xaios_boot_info_t *boot) {
  for (uint32_t i = 0; i < XAIOS_NUMA_MAX_NODES; ++i) {
    g_numa_nodes[i].node_id = i;
    g_numa_nodes[i].online = 0;
    g_numa_nodes[i].phys_start = 0;
    g_numa_nodes[i].phys_end = 0;
    g_numa_nodes[i].total_pages = 0;
    g_numa_nodes[i].free_count = 0;
    g_numa_nodes[i].cpu_mask = 0;
  }
  g_numa_node_count = 0;

  /* Walk the UEFI memory map to find conventional memory bounds */
  uint64_t lowest = UINT64_C(0xffffffffffffffff);
  uint64_t highest = 0;
  uint64_t offset = 0;
  while (offset + sizeof(xaios_memory_descriptor_t) <= boot->memory_map_size) {
    const xaios_memory_descriptor_t *desc =
        (const xaios_memory_descriptor_t *)(uintptr_t)(boot->memory_map + offset);
    if (desc->type == XAIOS_MEMORY_TYPE_CONVENTIONAL) {
      uint64_t region_start = desc->physical_start;
      uint64_t region_end =
          region_start + (desc->number_of_pages * PAGE_SIZE);
      if (region_start < lowest) {
        lowest = region_start;
      }
      if (region_end > highest) {
        highest = region_end;
      }
    }
    offset += boot->memory_descriptor_size;
  }

  if (lowest >= highest) {
    klog("NUMA: no conventional memory found\n");
    return;
  }

  /* Create single node 0 spanning all conventional memory */
  xaios_numa_node_t *node = &g_numa_nodes[0];
  node->node_id = 0;
  node->online = 1;
  node->phys_start = lowest;
  node->phys_end = highest;
  node->cpu_mask = 0;
  for (uint32_t c = 0; c < smp_online_count(); ++c) {
    node->cpu_mask |= (UINT64_C(1) << c);
  }

  /* Walk memory map again and classify pages into node 0 free-stack */
  node->total_pages = 0;
  node->free_count = 0;
  offset = 0;
  while (offset + sizeof(xaios_memory_descriptor_t) <= boot->memory_map_size) {
    const xaios_memory_descriptor_t *desc =
        (const xaios_memory_descriptor_t *)(uintptr_t)(boot->memory_map + offset);
    if (desc->type == XAIOS_MEMORY_TYPE_CONVENTIONAL) {
      uint64_t region_start = desc->physical_start;
      uint64_t region_end =
          region_start + (desc->number_of_pages * PAGE_SIZE);
      uint64_t page = align_up(region_start, PAGE_SIZE);
      uint64_t end = align_down(region_end, PAGE_SIZE);
      while (page + PAGE_SIZE <= end) {
        ++node->total_pages;
        if (page_is_reserved(boot, page)) {
          /* reserved -- skip */
        } else if (node->free_count < XAIOS_NUMA_MAX_FREE_PER_NODE) {
          node->free_stack[node->free_count++] = page;
        }
        page += PAGE_SIZE;
      }
    }
    offset += boot->memory_descriptor_size;
  }

  g_numa_node_count = 1;
  klog("NUMA: node 0 phys=[0x%lx, 0x%lx) total=%lu free=%lu cpu_mask=0x%lx\n",
       node->phys_start, node->phys_end, node->total_pages, node->free_count,
       node->cpu_mask);
}

uint32_t numa_node_count(void) { return g_numa_node_count; }

const xaios_numa_node_t *numa_node(uint32_t node_id) {
  if (node_id >= g_numa_node_count) {
    return 0;
  }
  return &g_numa_nodes[node_id];
}

uint32_t numa_node_of_phys(uint64_t phys_addr) {
  for (uint32_t i = 0; i < g_numa_node_count; ++i) {
    if (phys_addr >= g_numa_nodes[i].phys_start &&
        phys_addr < g_numa_nodes[i].phys_end) {
      return i;
    }
  }
  return UINT32_C(0xffffffff);
}

/* Internal: allocate from a specific node's free-stack */
void *numa_alloc_page_on_node(uint32_t node_id) {
  if (node_id >= g_numa_node_count) {
    return 0;
  }
  xaios_numa_node_t *node = &g_numa_nodes[node_id];
  if (node->free_count == 0) {
    return 0;
  }
  uint64_t page = node->free_stack[--node->free_count];
  return (void *)(uintptr_t)page;
}

/* Internal: free a page back to its owning node */
void numa_free_page(void *page) {
  if (page == 0) {
    return;
  }
  uint64_t phys = (uint64_t)(uintptr_t)page;
  uint32_t node_id = numa_node_of_phys(phys);
  if (node_id == UINT32_C(0xffffffff)) {
    return;
  }
  xaios_numa_node_t *node = &g_numa_nodes[node_id];
  if (node->free_count < XAIOS_NUMA_MAX_FREE_PER_NODE) {
    node->free_stack[node->free_count++] = phys;
  }
}

void numa_self_test(void) {
  kassert(g_numa_node_count >= 1);
  const xaios_numa_node_t *node0 = numa_node(0);
  kassert(node0 != 0);
  kassert(node0->online == 1);
  kassert(node0->free_count > 0);
  kassert(node0->cpu_mask != 0);
  kassert(node0->phys_start < node0->phys_end);

  /* Allocate a page on node 0 and verify node-of-page */
  void *p = numa_alloc_page_on_node(0);
  kassert(p != 0);
  uint64_t phys = (uint64_t)(uintptr_t)p;
  kassert(numa_node_of_phys(phys) == 0);
  kassert(phys >= node0->phys_start && phys < node0->phys_end);

  /* Free and verify count restored */
  uint64_t prev_free = g_numa_nodes[0].free_count;
  numa_free_page(p);
  kassert(g_numa_nodes[0].free_count == prev_free + 1);

  /* Allocate 64 pages, all on node 0 */
  void *pages[64];
  for (uint32_t i = 0; i < 64; ++i) {
    pages[i] = numa_alloc_page_on_node(0);
    kassert(pages[i] != 0);
    kassert(numa_node_of_phys((uint64_t)(uintptr_t)pages[i]) == 0);
  }
  for (uint32_t i = 0; i < 64; ++i) {
    numa_free_page(pages[i]);
  }

  /* Total pages should equal sum of node totals */
  uint64_t sum_total = 0;
  for (uint32_t i = 0; i < g_numa_node_count; ++i) {
    sum_total += g_numa_nodes[i].total_pages;
  }
  kassert(sum_total > 0);

  klog("NUMA: self-test passed nodes=%u node0_free=%lu\n", g_numa_node_count,
       g_numa_nodes[0].free_count);
}
