.section .user.text, "ax"
.global __user_init_entry
.type __user_init_entry, %function

__user_init_entry:
  adr x0, __user_init_message
  mov x1, #22
  mov x8, #1
  svc #0

  mov x0, #0
  mov x8, #2
  svc #0

1:
  wfe
  b 1b

.section .user.rodata, "a"
__user_init_message:
  .ascii "/init: hello from EL0\n"

.section .user.stack, "aw", %nobits
.balign 16
.global __user_init_stack_bottom
__user_init_stack_bottom:
  .skip 4096
.global __user_init_stack_top
__user_init_stack_top:
