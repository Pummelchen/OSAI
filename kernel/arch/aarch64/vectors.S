.section .text.vectors, "ax"
.balign 2048
.global __exception_vectors
.type __exception_vectors, %function

.macro vector_entry kind
  .balign 128
  msr daifset, #0xf
  mov x0, #\kind
  mrs x1, esr_el1
  mrs x2, elr_el1
  mrs x3, far_el1
  bl aarch64_exception_entry
1:
  wfe
  b 1b
.endm

__exception_vectors:
  vector_entry 0
  vector_entry 1
  vector_entry 2
  vector_entry 3
  vector_entry 4
  vector_entry 5
  vector_entry 6
  vector_entry 7
  vector_entry 8
  vector_entry 9
  vector_entry 10
  vector_entry 11
  vector_entry 12
  vector_entry 13
  vector_entry 14
  vector_entry 15
