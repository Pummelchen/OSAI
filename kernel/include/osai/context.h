#ifndef OSAI_CONTEXT_H
#define OSAI_CONTEXT_H

#include <osai/types.h>

/* Full AArch64 context frame: x0-x30, sp_el1, sp_el0, elr_el1, spsr_el1
 * 35 * 8 = 280 bytes, padded to 288 for 16-byte alignment. */
#define OSAI_CONTEXT_FRAME_SIZE 288U
#define OSAI_CONTEXT_FRAME_REGS 35U

typedef struct osai_context_frame {
  uint64_t regs[31]; /* x0-x30 */
  uint64_t sp_el1;   /* kernel stack pointer */
  uint64_t sp_el0;   /* user stack pointer */
  uint64_t elr_el1;  /* return address */
  uint64_t spsr_el1; /* processor state */
} osai_context_frame_t;

/* Save callee-saved registers (x19-x30, x29=fp, x30=lr) and sp to frame.
 * Returns 0 on initial save, 1 when restored via context_switch. */
uint64_t context_save(osai_context_frame_t *frame);

/* Restore callee-saved registers and sp from frame. Does not return to caller;
 * instead resumes at the saved lr (x30). */
void context_restore(const osai_context_frame_t *frame);

/* Atomic context switch: save current to *old, load *new_frame.
 * When the old context is later restored, context_save returns 1. */
void context_switch(osai_context_frame_t *old,
                    const osai_context_frame_t *new_frame);

#endif
