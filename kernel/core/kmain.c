#include <osai/assert.h>
#include <osai/ai_cell.h>
#include <osai/boot_info.h>
#include <osai/exception.h>
#include <osai/gic.h>
#include <osai/klog.h>
#include <osai/model_arena.h>
#include <osai/pmm.h>
#include <osai/service.h>
#include <osai/smp.h>
#include <osai/timer.h>
#include <osai/virtio_mmio.h>
#include <osai/vmm.h>

static const char g_vmm_rodata_probe[] = "vmm-rodata";
static uint64_t g_vmm_data_probe;

void kmain(const osai_boot_info_t *boot) {
  klog_init(boot);
  klog("OSAI kernel starting\n");
  kassert(boot->magic == OSAI_BOOT_INFO_MAGIC);
  kassert(boot->version == OSAI_BOOT_INFO_VERSION);

  klog("boot: memory_map=0x%lx size=%lu desc_size=%lu\n",
       boot->memory_map, boot->memory_map_size, boot->memory_descriptor_size);
  klog("boot: kernel=[0x%lx, 0x%lx)\n",
       boot->kernel_phys_base, boot->kernel_phys_end);

  exception_init();
  exception_self_test();
  timer_init();
  timer_self_test();
  smp_init_qemu_virt();
  smp_self_test();
  virtio_block_self_test();
  virtio_net_self_test();
  service_supervisor_self_test();
  model_arena_self_test();
  ai_cell_self_test();

  pmm_init(boot);

  vmm_init(boot);
  uint64_t translated = 0;
  uint32_t flags = 0;
  kassert(vmm_translate((uint64_t)(uintptr_t)&kmain, &translated, &flags) == OSAI_OK);
  kassert(translated == (uint64_t)(uintptr_t)&kmain);
  kassert((flags & OSAI_VMM_EXECUTABLE) != 0);
  kassert((flags & OSAI_VMM_DEVICE) == 0);
  kassert(vmm_translate((uint64_t)(uintptr_t)g_vmm_rodata_probe, &translated, &flags) == OSAI_OK);
  kassert(translated == (uint64_t)(uintptr_t)g_vmm_rodata_probe);
  kassert((flags & OSAI_VMM_WRITABLE) == 0);
  kassert((flags & OSAI_VMM_EXECUTABLE) == 0);
  kassert(vmm_translate((uint64_t)(uintptr_t)&g_vmm_data_probe, &translated, &flags) == OSAI_OK);
  kassert(translated == (uint64_t)(uintptr_t)&g_vmm_data_probe);
  kassert((flags & OSAI_VMM_WRITABLE) != 0);
  kassert((flags & OSAI_VMM_EXECUTABLE) == 0);
  kassert(vmm_translate(boot->uart_base, &translated, &flags) == OSAI_OK);
  kassert(translated == boot->uart_base);
  kassert((flags & OSAI_VMM_DEVICE) != 0);
  kassert((flags & OSAI_VMM_EXECUTABLE) == 0);
  klog("VMM translation test passed\n");
  gic_init_qemu_virt();
  gic_self_test();

#if defined(OSAI_FAULT_TEST_PAGE)
  exception_trigger_page_fault_for_test();
#endif

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
