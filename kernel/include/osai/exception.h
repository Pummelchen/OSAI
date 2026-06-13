#ifndef OSAI_EXCEPTION_H
#define OSAI_EXCEPTION_H

#include <osai/types.h>

typedef enum osai_exception_kind {
  OSAI_EXCEPTION_CURRENT_SP0_SYNC = 0,
  OSAI_EXCEPTION_CURRENT_SP0_IRQ = 1,
  OSAI_EXCEPTION_CURRENT_SP0_FIQ = 2,
  OSAI_EXCEPTION_CURRENT_SP0_SERROR = 3,
  OSAI_EXCEPTION_CURRENT_SPX_SYNC = 4,
  OSAI_EXCEPTION_CURRENT_SPX_IRQ = 5,
  OSAI_EXCEPTION_CURRENT_SPX_FIQ = 6,
  OSAI_EXCEPTION_CURRENT_SPX_SERROR = 7,
  OSAI_EXCEPTION_LOWER_A64_SYNC = 8,
  OSAI_EXCEPTION_LOWER_A64_IRQ = 9,
  OSAI_EXCEPTION_LOWER_A64_FIQ = 10,
  OSAI_EXCEPTION_LOWER_A64_SERROR = 11,
  OSAI_EXCEPTION_LOWER_A32_SYNC = 12,
  OSAI_EXCEPTION_LOWER_A32_IRQ = 13,
  OSAI_EXCEPTION_LOWER_A32_FIQ = 14,
  OSAI_EXCEPTION_LOWER_A32_SERROR = 15,
} osai_exception_kind_t;

void exception_init(void);
void exception_self_test(void);
void exception_trigger_page_fault_for_test(void);
void aarch64_exception_entry(uint64_t kind, uint64_t esr, uint64_t elr,
                             uint64_t far);

#endif
