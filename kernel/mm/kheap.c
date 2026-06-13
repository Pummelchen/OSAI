#include <osai/assert.h>
#include <osai/kheap.h>
#include <osai/klog.h>
#include <osai/pmm.h>

#define PAGE_SIZE UINT64_C(4096)

typedef struct heap_page {
  uint8_t *base;
  uint64_t used;
} heap_page_t;

static heap_page_t g_current_page;
static uint64_t g_heap_pages;
static uint64_t g_heap_bytes_allocated;

static uint64_t align_up(uint64_t value, uint64_t align) {
  return (value + align - 1) & ~(align - 1);
}

void kheap_init(void) {
  g_current_page.base = 0;
  g_current_page.used = 0;
  g_heap_pages = 0;
  g_heap_bytes_allocated = 0;
  klog("kheap: initialized\n");
}

void *kheap_alloc(uint64_t size, uint64_t align) {
  if (size == 0 || size > PAGE_SIZE || align == 0 || (align & (align - 1)) != 0) {
    return 0;
  }

  uint64_t offset = align_up(g_current_page.used, align);
  if (g_current_page.base == 0 || offset + size > PAGE_SIZE) {
    g_current_page.base = (uint8_t *)pmm_alloc_page();
    g_current_page.used = 0;
    offset = 0;
    if (g_current_page.base == 0) {
      return 0;
    }
    ++g_heap_pages;
  }

  void *result = g_current_page.base + offset;
  g_current_page.used = offset + size;
  g_heap_bytes_allocated += size;
  return result;
}

void kheap_self_test(void) {
  kheap_init();
  void *a = kheap_alloc(24, 8);
  void *b = kheap_alloc(128, 64);
  void *c = kheap_alloc(PAGE_SIZE + 1, 8);

  kassert(a != 0);
  kassert(b != 0);
  kassert(c == 0);
  kassert((((uint64_t)(uintptr_t)a) & 7U) == 0);
  kassert((((uint64_t)(uintptr_t)b) & 63U) == 0);
  kassert(g_heap_pages >= 1);
  kassert(g_heap_bytes_allocated == 152);
  klog("kheap: self-test passed pages=%lu bytes=%lu\n",
       g_heap_pages, g_heap_bytes_allocated);
}
