#include <xaios/assert.h>
#include <xaios/klog.h>
#include <xaios/pmm.h>
#include <xaios/smmu.h>

#define PAGE_SIZE UINT64_C(4096)

static volatile uint32_t *g_smmu_page0;
static volatile uint32_t *g_smmu_page1;
static uint32_t g_smmu_ready;
static uint32_t g_smmu_idr0;
static uint64_t g_stream_table_phys;
static uint64_t g_cmdq_phys;
static xaios_smmu_stream_t g_streams[XAIOS_SMMU_MAX_STREAMS];
static uint32_t g_active_streams;
static uint64_t g_tlb_invalidate_count;
static uint64_t g_fault_count;
static uint64_t g_cmdq_prod;
static uint64_t g_cmdq_cons;

static uint32_t mmio_read32(volatile uint32_t *base, uint32_t offset) {
  return base[offset / 4];
}

static void mmio_write32(volatile uint32_t *base, uint32_t offset,
                         uint32_t value) {
  base[offset / 4] = value;
  __asm__ volatile("dsb sy" ::: "memory");
}

static void zero_memory(void *dst, uint64_t size) {
  uint8_t *bytes = (uint8_t *)dst;
  for (uint64_t i = 0; i < size; ++i) {
    bytes[i] = 0;
  }
}

static int smmu_issue_command(uint32_t opcode, uint32_t word0_extra,
                              uint64_t word1) {
  if (g_smmu_ready == 0) {
    return 0;
  }
  /* Wait for space in command queue */
  uint64_t prod_idx = g_cmdq_prod & (XAIOS_SMMU_CMDQ_ENTRIES - 1);
  uint64_t cons_idx = g_cmdq_cons & (XAIOS_SMMU_CMDQ_ENTRIES - 1);
  if (prod_idx == cons_idx && g_cmdq_prod != g_cmdq_cons) {
    /* Queue full -- poll consumer */
    for (uint32_t spin = 0; spin < 100000; ++spin) {
      g_cmdq_cons = mmio_read32(g_smmu_page1, SMMU_CMDQ_CONS);
      cons_idx = g_cmdq_cons & (XAIOS_SMMU_CMDQ_ENTRIES - 1);
      if (prod_idx != cons_idx || g_cmdq_prod == g_cmdq_cons) {
        break;
      }
    }
    if (prod_idx == cons_idx && g_cmdq_prod != g_cmdq_cons) {
      return 0;
    }
  }

  /* Write command entry: 2x 64-bit words */
  volatile uint64_t *cmdq =
      (volatile uint64_t *)(uintptr_t)(g_cmdq_phys + prod_idx * 16);
  uint64_t w0 = (uint64_t)opcode | ((uint64_t)word0_extra << 8);
  cmdq[0] = w0;
  cmdq[1] = word1;
  __asm__ volatile("dsb sy" ::: "memory");

  /* Advance producer */
  ++g_cmdq_prod;
  mmio_write32(g_smmu_page1, SMMU_CMDQ_PROD, (uint32_t)g_cmdq_prod);
  return 1;
}

void smmu_init(void) {
  g_smmu_ready = 0;
  g_smmu_idr0 = 0;
  g_active_streams = 0;
  g_tlb_invalidate_count = 0;
  g_fault_count = 0;
  g_cmdq_prod = 0;
  g_cmdq_cons = 0;
  for (uint32_t i = 0; i < XAIOS_SMMU_MAX_STREAMS; ++i) {
    g_streams[i].stream_id = 0;
    g_streams[i].active = 0;
    g_streams[i].device_type = 0;
  }

  g_smmu_page0 = (volatile uint32_t *)(uintptr_t)XAIOS_SMMU_MMIO_BASE;
  g_smmu_page1 = (volatile uint32_t *)(uintptr_t)XAIOS_SMMU_MMIO_PAGE1;

  /* Read IDR0 to detect SMMUv3 */
  g_smmu_idr0 = mmio_read32(g_smmu_page0, SMMU_IDR0);
  if (g_smmu_idr0 == 0 || g_smmu_idr0 == UINT32_C(0xffffffff)) {
    klog("SMMU: not detected (IDR0=0x%x), operating in bypass mode\n",
         g_smmu_idr0);
    g_smmu_idr0 = 0;
    return;
  }
  klog("SMMU: IDR0=0x%x IDR1=0x%x\n", g_smmu_idr0,
       mmio_read32(g_smmu_page0, SMMU_IDR1));

  /* Allocate stream table (2KB = 32 STEs * 64 bytes, fits in one page) */
  void *st_page = pmm_alloc_page();
  if (st_page == 0) {
    klog("SMMU: failed to allocate stream table\n");
    return;
  }
  g_stream_table_phys = (uint64_t)(uintptr_t)st_page;
  zero_memory(st_page, XAIOS_SMMU_STREAM_TABLE_BYTES);

  /* Allocate command queue (128 entries * 16 bytes = 2KB, fits in one page) */
  void *cq_page = pmm_alloc_page();
  if (cq_page == 0) {
    klog("SMMU: failed to allocate command queue\n");
    return;
  }
  g_cmdq_phys = (uint64_t)(uintptr_t)cq_page;
  zero_memory(cq_page, XAIOS_SMMU_CMDQ_BYTES);

  /* Program stream table base (page 1 registers) */
  mmio_write32(g_smmu_page1, SMMU_STBASE_LO,
               (uint32_t)(g_stream_table_phys & UINT32_C(0xfffffff0)));
  mmio_write32(g_smmu_page1, SMMU_STBASE_HI,
               (uint32_t)(g_stream_table_phys >> 32));

  /* STRTAB_CFG: linear format, log2(size) = 5 (32 entries) */
  mmio_write32(g_smmu_page1, SMMU_STRTAB_CFG,
               SMMU_STRTAB_CFG_LINEAR | (UINT32_C(5) << 16));

  /* Program command queue base (page 1 registers) */
  mmio_write32(g_smmu_page1, SMMU_CMDQ_BASE_LO,
               (uint32_t)(g_cmdq_phys & UINT32_C(0xfffffff0)));
  mmio_write32(g_smmu_page1, SMMU_CMDQ_BASE_HI,
               (uint32_t)(g_cmdq_phys >> 32));

  /* Command queue size: log2(128) = 7, packed in bits [4:0] of BASE_LO */
  uint32_t cmdq_base_lo =
      (uint32_t)(g_cmdq_phys & UINT32_C(0xfffffff0)) | UINT32_C(7);
  mmio_write32(g_smmu_page1, SMMU_CMDQ_BASE_LO, cmdq_base_lo);

  /* Set producer/consumer to 0 */
  mmio_write32(g_smmu_page1, SMMU_CMDQ_PROD, 0);
  mmio_write32(g_smmu_page1, SMMU_CMDQ_CONS, 0);

  /* Set GBPA: update=1 for bypass of unmapped streams, no abort */
  mmio_write32(g_smmu_page0, SMMU_GBPA, SMMU_GBPA_UPDATE);

  /* Enable SMMU via CR0 */
  mmio_write32(g_smmu_page0, SMMU_CR0, SMMU_CR0_SMMUEN);

  /* Wait for CR0ACK to confirm enable */
  for (uint32_t spin = 0; spin < 100000; ++spin) {
    uint32_t ack = mmio_read32(g_smmu_page0, SMMU_CR0ACK);
    if ((ack & SMMU_CR0_SMMUEN) != 0) {
      g_smmu_ready = 1;
      break;
    }
  }

  if (g_smmu_ready == 0) {
    klog("SMMU: enable timeout (CR0ACK=0x%x)\n",
         mmio_read32(g_smmu_page0, SMMU_CR0ACK));
    return;
  }

  klog("SMMU: enabled IDR0=0x%x streams_max=%u cmdq_entries=%u\n",
       g_smmu_idr0, XAIOS_SMMU_MAX_STREAMS, XAIOS_SMMU_CMDQ_ENTRIES);
}

uint32_t smmu_initialized(void) { return g_smmu_ready; }

uint32_t smmu_idr0_value(void) { return g_smmu_idr0; }

xaios_status_t smmu_register_stream(uint32_t stream_id, uint32_t device_type) {
  if (stream_id >= XAIOS_SMMU_MAX_STREAMS) {
    return XAIOS_ERR_INVALID;
  }
  if (g_streams[stream_id].active) {
    return XAIOS_ERR_INVALID;
  }

  /* Program STE in stream table: 64 bytes per STE
   * Word 0: Valid=1, Config=bypass (S1DSS=0, S1P=0)
   * Word 1: reserved
   * The rest: zeroed for bypass mode */
  if (g_smmu_ready && g_stream_table_phys != 0) {
    volatile uint64_t *ste = (volatile uint64_t *)(uintptr_t)(
        g_stream_table_phys + (uint64_t)stream_id * 64);
    /* STE word 0: Valid bit (bit 0) = 1 */
    ste[0] = UINT64_C(1);
    ste[1] = 0;
    ste[2] = 0;
    ste[3] = 0;
    ste[4] = 0;
    ste[5] = 0;
    ste[6] = 0;
    ste[7] = 0;
    __asm__ volatile("dsb sy" ::: "memory");

    /* Invalidate cached STE via CFGI command */
    smmu_issue_command(SMMU_CMD_CFGI_STE, stream_id, 0);
  }

  g_streams[stream_id].stream_id = stream_id;
  g_streams[stream_id].active = 1;
  g_streams[stream_id].device_type = device_type;
  ++g_active_streams;

  return XAIOS_OK;
}

xaios_status_t smmu_unregister_stream(uint32_t stream_id) {
  if (stream_id >= XAIOS_SMMU_MAX_STREAMS) {
    return XAIOS_ERR_INVALID;
  }
  if (g_streams[stream_id].active == 0) {
    return XAIOS_ERR_INVALID;
  }

  /* Clear STE */
  if (g_smmu_ready && g_stream_table_phys != 0) {
    volatile uint64_t *ste = (volatile uint64_t *)(uintptr_t)(
        g_stream_table_phys + (uint64_t)stream_id * 64);
    ste[0] = 0;
    ste[1] = 0;
    ste[2] = 0;
    ste[3] = 0;
    ste[4] = 0;
    ste[5] = 0;
    ste[6] = 0;
    ste[7] = 0;
    __asm__ volatile("dsb sy" ::: "memory");
    smmu_issue_command(SMMU_CMD_CFGI_STE, stream_id, 0);
  }

  g_streams[stream_id].active = 0;
  if (g_active_streams > 0) {
    --g_active_streams;
  }

  return XAIOS_OK;
}

xaios_status_t smmu_tlb_invalidate_all(void) {
  if (g_smmu_ready == 0) {
    ++g_tlb_invalidate_count;
    return XAIOS_OK;
  }
  if (smmu_issue_command(SMMU_CMD_TLBI_NH_ALL, 0, 0)) {
    ++g_tlb_invalidate_count;
    return XAIOS_OK;
  }
  return XAIOS_ERR_IO;
}

uint64_t smmu_tlb_invalidate_count(void) { return g_tlb_invalidate_count; }

uint64_t smmu_fault_count(void) { return g_fault_count; }

uint64_t smmu_stream_count(void) { return g_active_streams; }

void smmu_self_test(void) {
  if (g_smmu_idr0 == 0) {
    klog("SMMU: self-test bypass mode (hardware not present)\n");
    /* Even in bypass mode, register/unregister should work */
    kassert(smmu_register_stream(0, 0xAA) == XAIOS_OK);
    kassert(smmu_stream_count() == 1);
    kassert(smmu_unregister_stream(0) == XAIOS_OK);
    kassert(smmu_stream_count() == 0);
    kassert(smmu_tlb_invalidate_all() == XAIOS_OK);
    kassert(smmu_tlb_invalidate_count() == 1);
    return;
  }

  /* Hardware present -- full test */
  kassert(g_smmu_ready == 1);
  kassert(g_smmu_idr0 != 0);

  /* Verify CR0.SMMUEN is set */
  uint32_t cr0 = mmio_read32(g_smmu_page0, SMMU_CR0);
  kassert((cr0 & SMMU_CR0_SMMUEN) != 0);

  /* Register a test stream */
  uint32_t test_sid = 31; /* Use last slot */
  kassert(smmu_register_stream(test_sid, 0xBB) == XAIOS_OK);
  kassert(g_streams[test_sid].active == 1);
  kassert(smmu_stream_count() == 1);

  /* Verify STE was written */
  if (g_stream_table_phys != 0) {
    volatile uint64_t *ste = (volatile uint64_t *)(uintptr_t)(
        g_stream_table_phys + (uint64_t)test_sid * 64);
    kassert((ste[0] & UINT64_C(1)) != 0); /* Valid bit */
  }

  /* TLB invalidation */
  kassert(smmu_tlb_invalidate_all() == XAIOS_OK);
  kassert(smmu_tlb_invalidate_count() >= 1);

  /* Unregister */
  kassert(smmu_unregister_stream(test_sid) == XAIOS_OK);
  kassert(g_streams[test_sid].active == 0);
  kassert(smmu_stream_count() == 0);

  /* Duplicate register should fail */
  kassert(smmu_register_stream(0, 1) == XAIOS_OK);
  kassert(smmu_register_stream(0, 2) == XAIOS_ERR_INVALID);
  kassert(smmu_unregister_stream(0) == XAIOS_OK);

  /* Out-of-range should fail */
  kassert(smmu_register_stream(XAIOS_SMMU_MAX_STREAMS, 0) == XAIOS_ERR_INVALID);

  klog("SMMU: self-test passed IDR0=0x%x tlb_inv=%lu\n", g_smmu_idr0,
       g_tlb_invalidate_count);
}
