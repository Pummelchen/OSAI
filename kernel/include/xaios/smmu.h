#ifndef XAIOS_SMMU_H
#define XAIOS_SMMU_H

#include <xaios/status.h>
#include <xaios/types.h>

#define XAIOS_SMMU_MMIO_BASE UINT64_C(0x09050000)
#define XAIOS_SMMU_MMIO_PAGE1 UINT64_C(0x09060000)
#define XAIOS_SMMU_MAX_STREAMS 32U
#define XAIOS_SMMU_STREAM_TABLE_BYTES (XAIOS_SMMU_MAX_STREAMS * 64)
#define XAIOS_SMMU_CMDQ_ENTRIES 128U
#define XAIOS_SMMU_CMDQ_BYTES (XAIOS_SMMU_CMDQ_ENTRIES * 16)

/* Page 0 register offsets */
#define SMMU_IDR0 UINT32_C(0x0000)
#define SMMU_IDR1 UINT32_C(0x0004)
#define SMMU_CR0 UINT32_C(0x0020)
#define SMMU_CR0ACK UINT32_C(0x0024)
#define SMMU_GBPA UINT32_C(0x0044)
#define SMMU_ACR UINT32_C(0x004C)
#define SMMU_GERROR UINT32_C(0x0060)

/* Page 1 register offsets */
#define SMMU_STBASE_LO UINT32_C(0x0020)
#define SMMU_STBASE_HI UINT32_C(0x0024)
#define SMMU_STRTAB_CFG UINT32_C(0x0028)
#define SMMU_CMDQ_BASE_LO UINT32_C(0x0040)
#define SMMU_CMDQ_BASE_HI UINT32_C(0x0044)
#define SMMU_CMDQ_PROD UINT32_C(0x0048)
#define SMMU_CMDQ_CONS UINT32_C(0x004C)

/* CR0 bits */
#define SMMU_CR0_SMMUEN (UINT32_C(1) << 0)

/* GBPA bits */
#define SMMU_GBPA_UPDATE (UINT32_C(1) << 20)
#define SMMU_GBPA_ABORT (UINT32_C(1) << 17)

/* STRTAB_CFG format */
#define SMMU_STRTAB_CFG_LINEAR UINT32_C(0)
#define SMMU_STRTAB_CFG_LOG2N_MASK UINT32_C(0x1f)

/* Command opcodes */
#define SMMU_CMD_TLBI_NH_ALL UINT32_C(0x10)
#define SMMU_CMD_CFGI_STE UINT32_C(0x03)

typedef struct xaios_smmu_stream {
  uint32_t stream_id;
  uint32_t active;
  uint32_t device_type;
} xaios_smmu_stream_t;

void smmu_init(void);
uint32_t smmu_initialized(void);
uint32_t smmu_idr0_value(void);
xaios_status_t smmu_register_stream(uint32_t stream_id, uint32_t device_type);
xaios_status_t smmu_unregister_stream(uint32_t stream_id);
xaios_status_t smmu_tlb_invalidate_all(void);
uint64_t smmu_tlb_invalidate_count(void);
uint64_t smmu_fault_count(void);
uint64_t smmu_stream_count(void);
void smmu_self_test(void);

#endif
