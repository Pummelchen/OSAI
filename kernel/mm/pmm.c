#include <osai/assert.h>
#include <osai/klog.h>
#include <osai/pmm.h>

#define PAGE_SIZE UINT64_C(4096)
#define MAX_FREE_PAGES UINT64_C(1048576)

static uint64_t g_free_stack[MAX_FREE_PAGES];
static uint64_t g_free_count;
static uint64_t g_total_pages;
static uint64_t g_reserved_pages;

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

static int page_is_reserved(const osai_boot_info_t *boot, uint64_t page) {
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

void pmm_init(const osai_boot_info_t *boot) {
  g_free_count = 0;
  g_total_pages = 0;
  g_reserved_pages = 0;

  uint64_t offset = 0;
  while (offset + sizeof(osai_memory_descriptor_t) <= boot->memory_map_size) {
    const osai_memory_descriptor_t *desc =
        (const osai_memory_descriptor_t *)(uintptr_t)(boot->memory_map + offset);
    uint64_t region_start = desc->physical_start;
    uint64_t region_end = region_start + (desc->number_of_pages * PAGE_SIZE);

    if (desc->type == OSAI_MEMORY_TYPE_CONVENTIONAL) {
      uint64_t page = align_up(region_start, PAGE_SIZE);
      uint64_t end = align_down(region_end, PAGE_SIZE);
      while (page + PAGE_SIZE <= end) {
        ++g_total_pages;
        if (page_is_reserved(boot, page)) {
          ++g_reserved_pages;
        } else if (g_free_count < MAX_FREE_PAGES) {
          g_free_stack[g_free_count++] = page;
        } else {
          ++g_reserved_pages;
        }
        page += PAGE_SIZE;
      }
    } else {
      g_reserved_pages += desc->number_of_pages;
    }

    offset += boot->memory_descriptor_size;
  }

  klog("PMM total pages=%lu free=%lu reserved=%lu\n",
       g_total_pages, g_free_count, g_reserved_pages);
  kassert(g_free_count != 0);
}

void *pmm_alloc_page(void) {
  if (g_free_count == 0) {
    return 0;
  }
  return (void *)(uintptr_t)g_free_stack[--g_free_count];
}

void pmm_free_page(void *page) {
  if (page == 0) {
    return;
  }
  kassert(g_free_count < MAX_FREE_PAGES);
  g_free_stack[g_free_count++] = (uint64_t)(uintptr_t)page;
}

uint64_t pmm_total_pages(void) {
  return g_total_pages;
}

uint64_t pmm_free_pages(void) {
  return g_free_count;
}
