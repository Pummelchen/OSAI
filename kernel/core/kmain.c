#include <osai/assert.h>
#include <osai/boot_info.h>
#include <osai/klog.h>
#include <osai/pmm.h>

void kmain(const osai_boot_info_t *boot) {
  klog_init(boot);
  klog("OSAI kernel starting\n");
  kassert(boot->magic == OSAI_BOOT_INFO_MAGIC);
  kassert(boot->version == OSAI_BOOT_INFO_VERSION);

  klog("boot: memory_map=0x%lx size=%lu desc_size=%lu\n",
       boot->memory_map, boot->memory_map_size, boot->memory_descriptor_size);
  klog("boot: kernel=[0x%lx, 0x%lx)\n",
       boot->kernel_phys_base, boot->kernel_phys_end);

  pmm_init(boot);

  void *pages[1024];
  for (unsigned i = 0; i < 1024; ++i) {
    pages[i] = pmm_alloc_page();
    kassert(pages[i] != 0);
  }
  for (unsigned i = 0; i < 1024; ++i) {
    pmm_free_page(pages[i]);
  }

  klog("PMM 1024 page allocate/free test passed\n");
  klog("OSAI kernel idle\n");

  for (;;) {
    __asm__ volatile("wfe");
  }
}
