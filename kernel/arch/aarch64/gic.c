#include <osai/assert.h>
#include <osai/gic.h>
#include <osai/klog.h>

#define QEMU_VIRT_GICD_BASE UINT64_C(0x08000000)
#define QEMU_VIRT_GICR_BASE UINT64_C(0x080A0000)

/* GIC Distributor registers */
#define GICD_CTLR        0x0000U
#define GICD_TYPER       0x0004U
#define GICD_IIDR        0x0008U
#define GICD_IGROUPR0    0x0080U
#define GICD_ISENABLER0  0x0100U
#define GICD_IPRIORITYR0 0x0400U

/* GIC Redistributor registers (per-CPU frame 0) */
#define GICR_CTLR         0x0000U
#define GICR_IIDR         0x0004U
#define GICR_TYPER        0x0008U
#define GICR_WAKER        0x0014U
#define GICR_ISENABLER0   0x0100U
#define GICR_IPRIORITYR0  0x0400U

/* GIC CPU Interface system registers */
#define ICC_CTLR_EL1   "S3_0_C12_C12_4"
#define ICC_PMR_EL1    "S3_0_C4_C6_0"
#define ICC_IGRPEN1_EL1 "S3_0_C12_C12_7"

/* Physical timer INTID (PPI 14) */
#define TIMER_PPI_INTID 30U

static osai_gic_info_t g_gic_info;
static uint32_t g_gic_full_init;

static uint32_t mmio_read32(uint64_t base, uint32_t offset) {
  volatile uint32_t *reg = (volatile uint32_t *)(uintptr_t)(base + offset);
  return *reg;
}

static void mmio_write32(uint64_t base, uint32_t offset, uint32_t value) {
  volatile uint32_t *reg = (volatile uint32_t *)(uintptr_t)(base + offset);
  *reg = value;
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

void gic_enable_full(void) {
  if (g_gic_full_init != 0) {
    return;
  }

  /* Route timer PPI (INTID 30) to Group 1 */
  uint32_t igroupr = mmio_read32(QEMU_VIRT_GICD_BASE, GICD_IGROUPR0);
  igroupr |= (1U << TIMER_PPI_INTID);
  mmio_write32(QEMU_VIRT_GICD_BASE, GICD_IGROUPR0, igroupr);

  /* Set priority for timer INTID 30 (byte offset = (30 % 4) * 8 = 24) */
  uint32_t ipr7 = mmio_read32(QEMU_VIRT_GICD_BASE, GICD_IPRIORITYR0 + 28U);
  ipr7 &= ~(0xffU << 24U);
  ipr7 |= (0xa0U << 24U);
  mmio_write32(QEMU_VIRT_GICD_BASE, GICD_IPRIORITYR0 + 28U, ipr7);

  /* Enable timer INTID 30 in distributor */
  mmio_write32(QEMU_VIRT_GICD_BASE, GICD_ISENABLER0, (1U << TIMER_PPI_INTID));

  /* Enable distributor Group 1 */
  uint32_t ctlr = mmio_read32(QEMU_VIRT_GICD_BASE, GICD_CTLR);
  ctlr |= 1U;
  mmio_write32(QEMU_VIRT_GICD_BASE, GICD_CTLR, ctlr);

  /* Configure redistributor for CPU 0 */
  uint32_t gicr_waker = mmio_read32(QEMU_VIRT_GICR_BASE, GICR_WAKER);
  gicr_waker &= ~(1U << 1U); /* clear ProcessorSleep */
  mmio_write32(QEMU_VIRT_GICR_BASE, GICR_WAKER, gicr_waker);

  /* Set redistributor priority for timer */
  uint32_t gicr_ipr7 = mmio_read32(QEMU_VIRT_GICR_BASE, GICR_IPRIORITYR0 + 28U);
  gicr_ipr7 &= ~(0xffU << 24U);
  gicr_ipr7 |= (0xa0U << 24U);
  mmio_write32(QEMU_VIRT_GICR_BASE, GICR_IPRIORITYR0 + 28U, gicr_ipr7);

  /* Enable redistributor PPI 30 */
  mmio_write32(QEMU_VIRT_GICR_BASE, GICR_ISENABLER0, (1U << TIMER_PPI_INTID));

  /* Enable CPU interface: set priority mask to allow all priorities */
  __asm__ volatile("msr " ICC_PMR_EL1 ", %0" : : "r"((uint64_t)0xffU));
  __asm__ volatile("msr " ICC_IGRPEN1_EL1 ", %0" : : "r"((uint64_t)1U));
  __asm__ volatile("isb");

  /* Unmask IRQs at CPU level (clear I bit in DAIF) */
  __asm__ volatile("msr daifclr, #2");

  g_gic_full_init = 1;
  klog("gic: full init complete redistributor=0x%lx timer_intid=%u\n",
       QEMU_VIRT_GICR_BASE, TIMER_PPI_INTID);
}

void gic_disable_full(void) {
  if (g_gic_full_init == 0) {
    return;
  }
  /* Mask IRQs at CPU level */
  __asm__ volatile("msr daifset, #2");
  /* Disable CPU interface */
  __asm__ volatile("msr " ICC_IGRPEN1_EL1 ", %0" : : "r"((uint64_t)0U));
  __asm__ volatile("isb");
  /* Disable timer interrupt in distributor */
  mmio_write32(QEMU_VIRT_GICD_BASE + 0x180U, 0U, (1U << TIMER_PPI_INTID));
  g_gic_full_init = 0;
  klog("gic: full mode disabled\n");
}

const osai_gic_info_t *gic_info(void) {
  return &g_gic_info;
}

void gic_self_test(void) {
  kassert(g_gic_info.distributor_base == QEMU_VIRT_GICD_BASE);
  kassert(g_gic_info.interrupt_lines >= 32);
  klog("gic: discovery self-test passed\n");
}
