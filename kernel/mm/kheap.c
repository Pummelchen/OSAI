#include <osai/assert.h>
#include <osai/kheap.h>
#include <osai/klog.h>
#include <osai/pmm.h>
#include <osai/vmm.h>

#define PAGE_SIZE UINT64_C(4096)
#define KHEAP_BASE UINT64_C(0x4a000000)
#define KHEAP_SIZE UINT64_C(0x01000000)
#define KHEAP_LIMIT (KHEAP_BASE + KHEAP_SIZE)

static uint64_t g_heap_next;
static uint64_t g_heap_mapped_end;
static uint64_t g_heap_pages;
static uint64_t g_heap_bytes_allocated;

static uint64_t align_up(uint64_t value, uint64_t align) {
  return (value + align - 1) & ~(align - 1);
}

void kheap_init(void) {
  g_heap_next = KHEAP_BASE;
  g_heap_mapped_end = KHEAP_BASE;
  g_heap_pages = 0;
  g_heap_bytes_allocated = 0;
  klog("kheap: initialized base=0x%lx size=%lu\n", KHEAP_BASE, KHEAP_SIZE);
}

static osai_status_t ensure_mapped(uint64_t end) {
  uint64_t mapped = g_heap_mapped_end;
  while (mapped < end) {
    void *page = pmm_alloc_page();
    if (page == 0) {
      return OSAI_ERR_NO_MEMORY;
    }
    if (vmm_map_page(mapped, (uint64_t)(uintptr_t)page,
                     OSAI_VMM_PRESENT | OSAI_VMM_WRITABLE) != OSAI_OK) {
      pmm_free_page(page);
      return OSAI_ERR_INVALID;
    }
    mapped += PAGE_SIZE;
    ++g_heap_pages;
  }
  g_heap_mapped_end = mapped;
  return OSAI_OK;
}

void *kheap_alloc(uint64_t size, uint64_t align) {
  if (size == 0 || align == 0 || (align & (align - 1)) != 0) {
    return 0;
  }

  uint64_t start = align_up(g_heap_next, align);
  uint64_t end = start + size;
  if (end < start || end > KHEAP_LIMIT) {
    return 0;
  }

  if (ensure_mapped(align_up(end, PAGE_SIZE)) != OSAI_OK) {
    return 0;
  }

  g_heap_next = end;
  g_heap_bytes_allocated += size;
  return (void *)(uintptr_t)start;
}

void *kheap_calloc(uint64_t size, uint64_t align) {
  uint8_t *result = (uint8_t *)kheap_alloc(size, align);
  if (result == 0) {
    return 0;
  }
  for (uint64_t i = 0; i < size; ++i) {
    result[i] = 0;
  }
  return result;
}

uint64_t kheap_pages_allocated(void) {
  return g_heap_pages;
}

uint64_t kheap_bytes_allocated(void) {
  return g_heap_bytes_allocated;
}

void kheap_self_test(void) {
  kheap_init();
  void *a = kheap_alloc(24, 8);
  void *b = kheap_alloc(128, 64);
  void *c = kheap_alloc(KHEAP_SIZE + 1U, 8);
  void *d = kheap_alloc(16, 3);
  uint8_t *z = (uint8_t *)kheap_calloc(32, 16);
  void *large = kheap_alloc(70496, 4096);

  kassert(a != 0);
  kassert(b != 0);
  kassert(c == 0);
  kassert(d == 0);
  kassert(z != 0);
  kassert(large != 0);
  for (uint32_t i = 0; i < 32; ++i) {
    kassert(z[i] == 0);
  }
  kassert((((uint64_t)(uintptr_t)a) & 7U) == 0);
  kassert((((uint64_t)(uintptr_t)b) & 63U) == 0);
  kassert((((uint64_t)(uintptr_t)z) & 15U) == 0);
  kassert((((uint64_t)(uintptr_t)large) & 4095U) == 0);
  kassert(g_heap_pages >= 18);
  kassert(g_heap_bytes_allocated == 70680);
  klog("kheap: self-test passed pages=%lu bytes=%lu\n",
       g_heap_pages, g_heap_bytes_allocated);
}
