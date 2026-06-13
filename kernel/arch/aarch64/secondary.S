.section .text.secondary, "ax"
.global aarch64_secondary_entry
.type aarch64_secondary_entry, %function

aarch64_secondary_entry:
  msr daifset, #0xf
  ldr x1, =g_secondary_stacks
  mov x2, #4096
  madd x1, x0, x2, x1
  add x1, x1, x2
  mov sp, x1
  bl smp_secondary_main
1:
  wfe
  b 1b
