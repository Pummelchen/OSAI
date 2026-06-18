.section .text.entry, "ax"
.global kernel_entry
.type kernel_entry, %function
.global aarch64_enter_user
.type aarch64_enter_user, %function
.global aarch64_return_from_user_exit
.type aarch64_return_from_user_exit, %function

kernel_entry:
  msr daifset, #0xf
  mov x20, x0
  ldr x0, =__bss_start
  ldr x1, =__bss_end
  mov x2, #0
1:
  cmp x0, x1
  b.hs 2f
  str x2, [x0], #8
  b 1b
2:
  /* Seed stack canary from CNTVCT_EL0 */
  isb
  mrs x1, cntvct_el0
  ldr x2, =0x5841494F535F4341
  eor x1, x1, x2
  ldr x2, =g_stack_canary
  str x1, [x2]
  mov x0, x20
  bl kmain
3:
  wfe
  b 3b

aarch64_enter_user:
  ldr x2, =aarch64_user_saved_sp
  mov x3, sp
  str x3, [x2]
  ldr x2, =aarch64_user_saved_fp
  str x29, [x2]
  ldr x2, =aarch64_user_saved_lr
  str x30, [x2]
  mov x2, #0
  msr sp_el0, x1
  msr elr_el1, x0
  msr spsr_el1, x2
  eret

aarch64_return_from_user_exit:
  ldr x1, =aarch64_user_saved_sp
  ldr x1, [x1]
  mov sp, x1
  ldr x1, =aarch64_user_saved_fp
  ldr x29, [x1]
  ldr x1, =aarch64_user_saved_lr
  ldr x30, [x1]
  ret

.section .bss, "aw", %nobits
.balign 8
aarch64_user_saved_sp:
  .quad 0
aarch64_user_saved_fp:
  .quad 0
aarch64_user_saved_lr:
  .quad 0
