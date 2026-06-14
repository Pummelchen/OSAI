.section .text.entry, "ax"
.global kernel_entry
.type kernel_entry, @function

kernel_entry:
  cli
  mov %rcx, %r12
  lea __bss_start(%rip), %rdi
  lea __bss_end(%rip), %rcx
  sub %rdi, %rcx
  xor %rax, %rax
  shr $3, %rcx
  rep stosq
  mov %r12, %rdi
  call x86_64_kmain

1:
  hlt
  jmp 1b

.macro OSAI_ISR_NOERR vector
.global x86_64_isr_\vector
.type x86_64_isr_\vector, @function
x86_64_isr_\vector:
  pushq $0
  pushq $\vector
  jmp x86_64_isr_common
.endm

.macro OSAI_ISR_ERR vector
.global x86_64_isr_\vector
.type x86_64_isr_\vector, @function
x86_64_isr_\vector:
  pushq $\vector
  jmp x86_64_isr_common
.endm

OSAI_ISR_NOERR 0
OSAI_ISR_NOERR 1
OSAI_ISR_NOERR 2
OSAI_ISR_NOERR 3
OSAI_ISR_NOERR 4
OSAI_ISR_NOERR 5
OSAI_ISR_NOERR 6
OSAI_ISR_NOERR 7
OSAI_ISR_ERR 8
OSAI_ISR_NOERR 9
OSAI_ISR_ERR 10
OSAI_ISR_ERR 11
OSAI_ISR_ERR 12
OSAI_ISR_ERR 13
OSAI_ISR_ERR 14
OSAI_ISR_NOERR 15
OSAI_ISR_NOERR 16
OSAI_ISR_ERR 17
OSAI_ISR_NOERR 18
OSAI_ISR_NOERR 19
OSAI_ISR_NOERR 20
OSAI_ISR_ERR 21
OSAI_ISR_NOERR 22
OSAI_ISR_NOERR 23
OSAI_ISR_NOERR 24
OSAI_ISR_NOERR 25
OSAI_ISR_NOERR 26
OSAI_ISR_NOERR 27
OSAI_ISR_NOERR 28
OSAI_ISR_ERR 29
OSAI_ISR_ERR 30
OSAI_ISR_NOERR 31

.global x86_64_isr_common
.type x86_64_isr_common, @function
x86_64_isr_common:
  pushq %rax
  pushq %rbx
  pushq %rcx
  pushq %rdx
  pushq %rsi
  pushq %rdi
  pushq %rbp
  pushq %r8
  pushq %r9
  pushq %r10
  pushq %r11
  movq %rsp, %rdi
  call x86_64_exception_entry
  popq %r11
  popq %r10
  popq %r9
  popq %r8
  popq %rbp
  popq %rdi
  popq %rsi
  popq %rdx
  popq %rcx
  popq %rbx
  popq %rax
  addq $16, %rsp
  iretq
