.section .text.entry, "ax"
.global kernel_entry
.type kernel_entry, %function

kernel_entry:
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
  mov x0, x20
  bl kmain
3:
  wfe
  b 3b
