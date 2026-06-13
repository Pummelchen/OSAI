#include <osai/assert.h>
#include <osai/gic.h>
#include <osai/klog.h>

#define QEMU_VIRT_GICD_BASE UINT64_C(0x08000000)
#define GICD_CTLR 0x0000U
#define GICD_TYPER 0x0004U
#define GICD_IIDR 0x0008U

static osai_gic_info_t g_gic_info;

static uint32_t mmio_read32(uint64_t base, uint32_t offset) {
  volatile uint32_t *reg = (volatile uint32_t *)(uintptr_t)(base + offset);
  return *reg;
}

void gic_init_qemu_virt(void) {
  g_gic_info.distributor_base = QEMU_VIRT_GICD_BASE;
  (void)mmio_read32(QEMU_VIRT_GICD_BASE, GICD_CTLR);
  g_gic_info.typer = mmio_read32(QEMU_VIRT_GICD_BASE, GICD_TYPER);
  g_gic_info.iidr = mmio_read32(QEMU_VIRT_GICD_BASE, GICD_IIDR);
  g_gic_info.interrupt_lines = ((g_gic_info.typer & 0x1fU) + 1U) * 32U;
  g_gic_info.cpu_count_hint = ((g_gic_info.typer >> 5U) & 0x7U) + 1U;

  klog("gic: distributor=0x%lx typer=0x%x iidr=0x%x lines=%u cpu_hint=%u\n",
       g_gic_info.distributor_base, g_gic_info.typer, g_gic_info.iidr,
       g_gic_info.interrupt_lines, g_gic_info.cpu_count_hint);
}

const osai_gic_info_t *gic_info(void) {
  return &g_gic_info;
}

void gic_self_test(void) {
  kassert(g_gic_info.distributor_base == QEMU_VIRT_GICD_BASE);
  kassert(g_gic_info.interrupt_lines >= 32);
  klog("gic: discovery self-test passed\n");
}
