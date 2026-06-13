#include <osai/exception.h>
#include <osai/klog.h>
#include <osai/panic.h>

#define ESR_EC_SHIFT 26U
#define ESR_EC_MASK UINT64_C(0x3f)
#define ESR_ISS_MASK UINT64_C(0x01ffffff)

#define ESR_EC_UNKNOWN UINT64_C(0x00)
#define ESR_EC_WFI_WFE UINT64_C(0x01)
#define ESR_EC_BRK_A64 UINT64_C(0x3c)
#define ESR_EC_INSN_ABORT_LOWER UINT64_C(0x20)
#define ESR_EC_INSN_ABORT_CURRENT UINT64_C(0x21)
#define ESR_EC_DATA_ABORT_LOWER UINT64_C(0x24)
#define ESR_EC_DATA_ABORT_CURRENT UINT64_C(0x25)

extern char __exception_vectors[];

static const char *exception_kind_name(uint64_t kind) {
  switch ((osai_exception_kind_t)kind) {
    case OSAI_EXCEPTION_CURRENT_SP0_SYNC:
      return "current-sp0-sync";
    case OSAI_EXCEPTION_CURRENT_SP0_IRQ:
      return "current-sp0-irq";
    case OSAI_EXCEPTION_CURRENT_SP0_FIQ:
      return "current-sp0-fiq";
    case OSAI_EXCEPTION_CURRENT_SP0_SERROR:
      return "current-sp0-serror";
    case OSAI_EXCEPTION_CURRENT_SPX_SYNC:
      return "current-spx-sync";
    case OSAI_EXCEPTION_CURRENT_SPX_IRQ:
      return "current-spx-irq";
    case OSAI_EXCEPTION_CURRENT_SPX_FIQ:
      return "current-spx-fiq";
    case OSAI_EXCEPTION_CURRENT_SPX_SERROR:
      return "current-spx-serror";
    case OSAI_EXCEPTION_LOWER_A64_SYNC:
      return "lower-a64-sync";
    case OSAI_EXCEPTION_LOWER_A64_IRQ:
      return "lower-a64-irq";
    case OSAI_EXCEPTION_LOWER_A64_FIQ:
      return "lower-a64-fiq";
    case OSAI_EXCEPTION_LOWER_A64_SERROR:
      return "lower-a64-serror";
    case OSAI_EXCEPTION_LOWER_A32_SYNC:
      return "lower-a32-sync";
    case OSAI_EXCEPTION_LOWER_A32_IRQ:
      return "lower-a32-irq";
    case OSAI_EXCEPTION_LOWER_A32_FIQ:
      return "lower-a32-fiq";
    case OSAI_EXCEPTION_LOWER_A32_SERROR:
      return "lower-a32-serror";
  }

  return "unknown-kind";
}

static const char *exception_class_name(uint64_t ec) {
  switch (ec) {
    case ESR_EC_UNKNOWN:
      return "unknown";
    case ESR_EC_WFI_WFE:
      return "wfi-wfe";
    case ESR_EC_BRK_A64:
      return "brk-a64";
    case ESR_EC_INSN_ABORT_LOWER:
      return "instruction-abort-lower";
    case ESR_EC_INSN_ABORT_CURRENT:
      return "instruction-abort-current";
    case ESR_EC_DATA_ABORT_LOWER:
      return "data-abort-lower";
    case ESR_EC_DATA_ABORT_CURRENT:
      return "data-abort-current";
    default:
      return "unclassified";
  }
}

void exception_init(void) {
  uint64_t vector_base = (uint64_t)(uintptr_t)__exception_vectors;
  __asm__ volatile(
      "msr vbar_el1, %[vectors]\n"
      "isb\n"
      :
      : [vectors] "r"(vector_base)
      : "memory");

  klog("exceptions: VBAR_EL1=0x%lx\n", vector_base);
}

void exception_self_test(void) {
  uint64_t vector_base = 0;
  __asm__ volatile("mrs %[vectors], vbar_el1" : [vectors] "=r"(vector_base));

  klog("exceptions: self-test vector_base=0x%lx\n", vector_base);
  if (vector_base != (uint64_t)(uintptr_t)__exception_vectors) {
    panic("exception vector install failed");
  }
}

void exception_trigger_page_fault_for_test(void) {
  volatile uint64_t *unmapped = (volatile uint64_t *)UINT64_C(0x1000000000);
  klog("exceptions: triggering controlled page fault at 0x%lx\n",
       (uint64_t)(uintptr_t)unmapped);
  (void)*unmapped;
}

void aarch64_exception_entry(uint64_t kind, uint64_t esr, uint64_t elr,
                             uint64_t far) {
  uint64_t ec = (esr >> ESR_EC_SHIFT) & ESR_EC_MASK;
  uint64_t iss = esr & ESR_ISS_MASK;

  klog("\nEXCEPTION: kind=%s class=%s ec=0x%lx iss=0x%lx\n",
       exception_kind_name(kind), exception_class_name(ec), ec, iss);
  klog("EXCEPTION: elr=0x%lx far=0x%lx esr=0x%lx\n", elr, far, esr);

  if (ec == ESR_EC_DATA_ABORT_CURRENT || ec == ESR_EC_INSN_ABORT_CURRENT ||
      ec == ESR_EC_DATA_ABORT_LOWER || ec == ESR_EC_INSN_ABORT_LOWER) {
    panic("controlled page fault reported");
  }

  panic("fatal exception reported");
}
