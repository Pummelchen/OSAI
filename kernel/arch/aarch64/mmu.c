#include <osai/assert.h>
#include <osai/klog.h>
#include <osai/vmm.h>

#define PAGE_SIZE UINT64_C(4096)
#define L2_BLOCK_SIZE UINT64_C(0x200000)
#define L1_BLOCK_SIZE UINT64_C(0x40000000)
#define EARLY_IDENTITY_SIZE UINT64_C(0x100000000)
#define EARLY_L1_TABLES 4
#define EARLY_KERNEL_L3_TABLES 16

#define PTE_VALID UINT64_C(1)
#define PTE_TABLE UINT64_C(1 << 1)
#define PTE_ATTR_NORMAL UINT64_C(0 << 2)
#define PTE_AP_RO UINT64_C(1 << 7)
#define PTE_AP_EL0 UINT64_C(1 << 6)
#define PTE_SH_INNER UINT64_C(3 << 8)
#define PTE_AF UINT64_C(1 << 10)
#define PTE_PXN UINT64_C(1) << 53
#define PTE_UXN UINT64_C(1) << 54
#define PTE_ADDR_MASK UINT64_C(0x0000fffffffff000)
#define PTE_BLOCK_L2_ADDR_MASK UINT64_C(0x0000ffffffe00000)

#define MAIR_NORMAL_WB UINT64_C(0xff)

extern char __text_start[];
extern char __text_end[];
extern char __rodata_start[];
extern char __rodata_end[];
extern char __data_start[];
extern char __data_end[];
extern char __bss_start[];
extern char __bss_end[];
extern char __user_text_start[];
extern char __user_text_end[];
extern char __user_rodata_start[];
extern char __user_rodata_end[];
extern char __user_stack_start[];
extern char __user_stack_end[];

static uint64_t g_l0_table[512] __attribute__((aligned(PAGE_SIZE)));
static uint64_t g_l1_table[512] __attribute__((aligned(PAGE_SIZE)));
static uint64_t g_l2_tables[EARLY_L1_TABLES][512] __attribute__((aligned(PAGE_SIZE)));
static uint64_t g_kernel_l3_tables[EARLY_KERNEL_L3_TABLES][512]
    __attribute__((aligned(PAGE_SIZE)));
static uint64_t g_mmio_start;
static uint64_t g_mmio_end;

static uint64_t align_down(uint64_t value, uint64_t align) {
  return value & ~(align - 1);
}

static uint64_t align_up(uint64_t value, uint64_t align) {
  return (value + align - 1) & ~(align - 1);
}

static void zero_table(uint64_t *table, uint64_t entries) {
  for (uint64_t i = 0; i < entries; ++i) {
    table[i] = 0;
  }
}

static uint64_t table_descriptor(const uint64_t *table) {
  return ((uint64_t)(uintptr_t)table & PTE_ADDR_MASK) | PTE_VALID | PTE_TABLE;
}

static uint64_t page_descriptor(uint64_t physical_address, uint64_t attrs) {
  return (physical_address & PTE_ADDR_MASK) | attrs | PTE_VALID | PTE_TABLE |
         PTE_AF;
}

static uint64_t block_descriptor(uint64_t physical_address, uint64_t attrs) {
  return (physical_address & PTE_BLOCK_L2_ADDR_MASK) | attrs | PTE_VALID | PTE_AF;
}

static uint64_t normal_rw_nx_attrs(void) {
  return PTE_ATTR_NORMAL | PTE_SH_INNER | PTE_PXN | PTE_UXN;
}

static uint64_t normal_ro_nx_attrs(void) {
  return PTE_ATTR_NORMAL | PTE_SH_INNER | PTE_AP_RO | PTE_PXN | PTE_UXN;
}

static uint64_t normal_rx_attrs(void) {
  return PTE_ATTR_NORMAL | PTE_SH_INNER | PTE_UXN;
}

static uint64_t user_rx_attrs(void) {
  return PTE_ATTR_NORMAL | PTE_SH_INNER | PTE_AP_EL0 | PTE_PXN;
}

static uint64_t user_ro_nx_attrs(void) {
  return PTE_ATTR_NORMAL | PTE_SH_INNER | PTE_AP_EL0 | PTE_AP_RO |
         PTE_PXN | PTE_UXN;
}

static uint64_t user_rw_nx_attrs(void) {
  return PTE_ATTR_NORMAL | PTE_SH_INNER | PTE_AP_EL0 | PTE_PXN | PTE_UXN;
}

static int in_range(uint64_t value, uint64_t start, uint64_t end) {
  return value >= start && value < end;
}

static int overlaps(uint64_t start, uint64_t end, uint64_t used_start,
                    uint64_t used_end) {
  return start < used_end && used_start < end;
}

static uint64_t kernel_page_attrs(uint64_t address) {
  uint64_t page_end = address + PAGE_SIZE;
  uint64_t text_start = (uint64_t)(uintptr_t)__text_start;
  uint64_t text_end = (uint64_t)(uintptr_t)__text_end;
  uint64_t rodata_start = (uint64_t)(uintptr_t)__rodata_start;
  uint64_t rodata_end = (uint64_t)(uintptr_t)__rodata_end;
  uint64_t data_start = (uint64_t)(uintptr_t)__data_start;
  uint64_t data_end = (uint64_t)(uintptr_t)__data_end;
  uint64_t bss_start = (uint64_t)(uintptr_t)__bss_start;
  uint64_t bss_end = (uint64_t)(uintptr_t)__bss_end;
  uint64_t user_text_start = (uint64_t)(uintptr_t)__user_text_start;
  uint64_t user_text_end = (uint64_t)(uintptr_t)__user_text_end;
  uint64_t user_rodata_start = (uint64_t)(uintptr_t)__user_rodata_start;
  uint64_t user_rodata_end = (uint64_t)(uintptr_t)__user_rodata_end;
  uint64_t user_stack_start = (uint64_t)(uintptr_t)__user_stack_start;
  uint64_t user_stack_end = (uint64_t)(uintptr_t)__user_stack_end;

  if (overlaps(address, page_end, user_text_start, user_text_end)) {
    return user_rx_attrs();
  }
  if (overlaps(address, page_end, user_rodata_start, user_rodata_end)) {
    return user_ro_nx_attrs();
  }
  if (overlaps(address, page_end, user_stack_start, user_stack_end)) {
    return user_rw_nx_attrs();
  }

  if (in_range(address, text_start, text_end)) {
    return normal_rx_attrs();
  }
  if (in_range(address, rodata_start, rodata_end)) {
    return normal_ro_nx_attrs();
  }
  if (in_range(address, data_start, data_end) || in_range(address, bss_start, bss_end)) {
    return normal_rw_nx_attrs();
  }

  return normal_rw_nx_attrs();
}

static void map_identity_l2_blocks(uint64_t start, uint64_t end, uint64_t attrs) {
  for (uint64_t address = start; address < end; address += L2_BLOCK_SIZE) {
    uint64_t l1_index = (address >> 30) & 0x1ffU;
    uint64_t l2_index = (address >> 21) & 0x1ffU;
    kassert(l1_index < EARLY_L1_TABLES);
    g_l2_tables[l1_index][l2_index] = block_descriptor(address, attrs);
  }
}

static void map_kernel_pages(uint64_t start, uint64_t end) {
  uint64_t table_index = 0;
  uint64_t table_start = align_down(start, L2_BLOCK_SIZE);
  uint64_t table_end = align_up(end, L2_BLOCK_SIZE);

  for (uint64_t region = table_start; region < table_end; region += L2_BLOCK_SIZE) {
    kassert(table_index < EARLY_KERNEL_L3_TABLES);

    uint64_t l1_index = (region >> 30) & 0x1ffU;
    uint64_t l2_index = (region >> 21) & 0x1ffU;
    uint64_t *l3 = g_kernel_l3_tables[table_index++];

    for (uint64_t page = 0; page < 512; ++page) {
      uint64_t address = region + (page * PAGE_SIZE);
      l3[page] = page_descriptor(address, kernel_page_attrs(address));
    }

    g_l2_tables[l1_index][l2_index] = table_descriptor(l3);
  }
}

static void build_tables(const osai_boot_info_t *boot) {
  zero_table(g_l0_table, 512);
  zero_table(g_l1_table, 512);
  for (uint64_t i = 0; i < EARLY_L1_TABLES; ++i) {
    zero_table(g_l2_tables[i], 512);
  }
  for (uint64_t i = 0; i < EARLY_KERNEL_L3_TABLES; ++i) {
    zero_table(g_kernel_l3_tables[i], 512);
  }

  g_l0_table[0] = table_descriptor(g_l1_table);

  for (uint64_t i = 0; i < EARLY_L1_TABLES; ++i) {
    g_l1_table[i] = table_descriptor(g_l2_tables[i]);
  }

  map_identity_l2_blocks(0, EARLY_IDENTITY_SIZE, normal_rw_nx_attrs());
  g_mmio_start = align_down(boot->uart_base, L2_BLOCK_SIZE);
  g_mmio_end = align_up(boot->uart_base + PAGE_SIZE, L2_BLOCK_SIZE);
  /* Keep early PL011 serial stable until OSAI owns exception vectors. */
  map_identity_l2_blocks(g_mmio_start, g_mmio_end, normal_rw_nx_attrs());
  map_kernel_pages(boot->kernel_phys_base, boot->kernel_phys_end);
}

static void aarch64_enable_mmu(uint64_t root_table) {
  uint64_t mair = MAIR_NORMAL_WB;
  uint64_t tcr = UINT64_C(16) | UINT64_C(1 << 8) | UINT64_C(1 << 10) |
                 UINT64_C(3 << 12) | UINT64_C(2) << 32;
  uint64_t sctlr = 0;

  __asm__ volatile(
      "dsb sy\n"
      "mrs %[sctlr], sctlr_el1\n"
      "msr mair_el1, %[mair]\n"
      "msr tcr_el1, %[tcr]\n"
      "msr ttbr0_el1, %[root]\n"
      "tlbi vmalle1\n"
      "dsb ish\n"
      "isb\n"
      "orr %[sctlr], %[sctlr], #(1 << 0)\n"
      "orr %[sctlr], %[sctlr], #(1 << 2)\n"
      "orr %[sctlr], %[sctlr], #(1 << 12)\n"
      "msr sctlr_el1, %[sctlr]\n"
      "isb\n"
      : [sctlr] "=&r"(sctlr)
      : [mair] "r"(mair), [tcr] "r"(tcr), [root] "r"(root_table)
      : "memory");
}

static osai_status_t descriptor_to_flags(uint64_t virtual_address,
                                         uint64_t descriptor, uint32_t *flags) {
  if ((descriptor & PTE_VALID) == 0) {
    return OSAI_ERR_INVALID;
  }

  uint32_t out = OSAI_VMM_PRESENT;
  uint64_t attr_index = (descriptor >> 2) & 0x7U;
  if (attr_index == 1) {
    out |= OSAI_VMM_DEVICE;
  }
  if (virtual_address >= g_mmio_start && virtual_address < g_mmio_end) {
    out |= OSAI_VMM_DEVICE;
  }
  if ((descriptor & PTE_AP_RO) == 0) {
    out |= OSAI_VMM_WRITABLE;
  }
  if ((descriptor & PTE_PXN) == 0) {
    out |= OSAI_VMM_EXECUTABLE;
  }

  *flags = out;
  return OSAI_OK;
}

void vmm_init(const osai_boot_info_t *boot) {
  build_tables(boot);
  aarch64_enable_mmu((uint64_t)(uintptr_t)g_l0_table);
  klog("VMM enabled\n");
}

osai_status_t vmm_translate(uint64_t virtual_address, uint64_t *physical_address,
                            uint32_t *flags) {
  uint64_t l0_index = (virtual_address >> 39) & 0x1ffU;
  uint64_t l1_index = (virtual_address >> 30) & 0x1ffU;
  uint64_t l2_index = (virtual_address >> 21) & 0x1ffU;
  uint64_t l3_index = (virtual_address >> 12) & 0x1ffU;
  uint64_t page_offset = virtual_address & UINT64_C(0xfff);
  uint64_t l2_offset = virtual_address & (L2_BLOCK_SIZE - 1);

  uint64_t l0_desc = g_l0_table[l0_index];
  if ((l0_desc & (PTE_VALID | PTE_TABLE)) != (PTE_VALID | PTE_TABLE)) {
    return OSAI_ERR_INVALID;
  }
  const uint64_t *l1 = (const uint64_t *)(uintptr_t)(l0_desc & PTE_ADDR_MASK);

  uint64_t l1_desc = l1[l1_index];
  if ((l1_desc & (PTE_VALID | PTE_TABLE)) != (PTE_VALID | PTE_TABLE)) {
    return OSAI_ERR_INVALID;
  }
  const uint64_t *l2 = (const uint64_t *)(uintptr_t)(l1_desc & PTE_ADDR_MASK);

  uint64_t l2_desc = l2[l2_index];
  if ((l2_desc & PTE_VALID) == 0) {
    return OSAI_ERR_INVALID;
  }
  if ((l2_desc & PTE_TABLE) == 0) {
    *physical_address = (l2_desc & PTE_BLOCK_L2_ADDR_MASK) + l2_offset;
    return descriptor_to_flags(virtual_address, l2_desc, flags);
  }

  const uint64_t *l3 = (const uint64_t *)(uintptr_t)(l2_desc & PTE_ADDR_MASK);
  uint64_t l3_desc = l3[l3_index];
  if ((l3_desc & PTE_VALID) == 0) {
    return OSAI_ERR_INVALID;
  }

  *physical_address = (l3_desc & PTE_ADDR_MASK) + page_offset;
  return descriptor_to_flags(virtual_address, l3_desc, flags);
}
