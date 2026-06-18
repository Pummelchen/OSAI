.section .text.context, "ax"

/* IRQ full-frame entry: saves x0-x30, sp_el1, sp_el0, elr_el1, spsr_el1
 * Frame layout (288 bytes, 16-byte aligned):
 *   [sp+0]   x0   [sp+8]   x1
 *   [sp+16]  x2   [sp+32]  x3
 *   ...
 *   [sp+232] x28  [sp+240] x29
 *   [sp+248] x30
 *   [sp+256] sp_el1 (pre-interrupt kernel sp)
 *   [sp+264] sp_el0
 *   [sp+272] elr_el1
 *   [sp+280] spsr_el1
 *   [sp+284..287] padding
 */

.global irq_entry_full
.type irq_entry_full, %function
irq_entry_full:
  sub sp, sp, #288
  stp x0, x1, [sp, #0]
  stp x2, x3, [sp, #16]
  stp x4, x5, [sp, #32]
  stp x6, x7, [sp, #48]
  stp x8, x9, [sp, #64]
  stp x10, x11, [sp, #80]
  stp x12, x13, [sp, #96]
  stp x14, x15, [sp, #112]
  stp x16, x17, [sp, #128]
  stp x18, x19, [sp, #144]
  stp x20, x21, [sp, #160]
  stp x22, x23, [sp, #176]
  stp x24, x25, [sp, #192]
  stp x26, x27, [sp, #208]
  stp x28, x29, [sp, #224]
  str x30, [sp, #248]
  add x1, sp, #288
  str x1, [sp, #256]
  mrs x1, sp_el0
  str x1, [sp, #264]
  mrs x1, elr_el1
  str x1, [sp, #272]
  mrs x1, spsr_el1
  str x1, [sp, #280]
  mov x0, sp
  adrp x1, aarch64_irq_handler
  add x1, x1, :lo12:aarch64_irq_handler
  blr x1
  mov sp, x0
  ldp x0, x1, [sp, #0]
  ldp x2, x3, [sp, #16]
  ldp x4, x5, [sp, #32]
  ldp x6, x7, [sp, #48]
  ldp x8, x9, [sp, #64]
  ldp x10, x11, [sp, #80]
  ldp x12, x13, [sp, #96]
  ldp x14, x15, [sp, #112]
  ldp x16, x17, [sp, #128]
  ldp x18, x19, [sp, #144]
  ldp x20, x21, [sp, #160]
  ldp x22, x23, [sp, #176]
  ldp x24, x25, [sp, #192]
  ldp x26, x27, [sp, #208]
  ldp x28, x29, [sp, #224]
  ldr x30, [sp, #248]
  ldr x1, [sp, #264]
  msr sp_el0, x1
  ldr x1, [sp, #272]
  msr elr_el1, x1
  ldr x1, [sp, #280]
  msr spsr_el1, x1
  add sp, sp, #288
  eret
.size irq_entry_full, . - irq_entry_full

/* void context_switch(xaios_context_frame_t *old,
 *                     const xaios_context_frame_t *new_frame)
 * Cooperative context switch: saves callee-saved state to *old,
 * restores from *new_frame. Returns to caller in the NEW context.
 */
.global context_switch
.type context_switch, %function
context_switch:
  /* save callee-saved registers to old frame */
  stp x19, x20, [x0, #152]
  stp x21, x22, [x0, #168]
  stp x23, x24, [x0, #184]
  stp x25, x26, [x0, #200]
  stp x27, x28, [x0, #216]
  stp x29, x30, [x0, #232]
  mov x2, sp
  str x2, [x0, #248]
  /* restore callee-saved from new frame */
  ldp x19, x20, [x1, #152]
  ldp x21, x22, [x1, #168]
  ldp x23, x24, [x1, #184]
  ldp x25, x26, [x1, #200]
  ldp x27, x28, [x1, #216]
  ldp x29, x30, [x1, #232]
  ldr x2, [x1, #248]
  mov sp, x2
  ret /* returns via new frame's x30 */
.size context_switch, . - context_switch

/* uint64_t context_save(xaios_context_frame_t *frame)
 * Saves callee-saved regs and sp. Returns 0 (initial save). */
.global context_save
.type context_save, %function
context_save:
  stp x19, x20, [x0, #152]
  stp x21, x22, [x0, #168]
  stp x23, x24, [x0, #184]
  stp x25, x26, [x0, #200]
  stp x27, x28, [x0, #216]
  stp x29, x30, [x0, #232]
  mov x1, sp
  str x1, [x0, #248]
  mov x0, #0
  ret
.size context_save, . - context_save

/* void context_restore(const xaios_context_frame_t *frame)
 * Restores callee-saved regs and sp. Returns via saved x30. */
.global context_restore
.type context_restore, %function
context_restore:
  ldp x19, x20, [x0, #152]
  ldp x21, x22, [x0, #168]
  ldp x23, x24, [x0, #184]
  ldp x25, x26, [x0, #200]
  ldp x27, x28, [x0, #216]
  ldp x29, x30, [x0, #232]
  ldr x1, [x0, #248]
  mov sp, x1
  ret
.size context_restore, . - context_restore
