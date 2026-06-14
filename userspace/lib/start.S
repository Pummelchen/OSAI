.section .text, "ax"
.global _start
.type _start, %function

_start:
  bl main
  mov x8, #2
  svc #0

1:
  wfe
  b 1b
