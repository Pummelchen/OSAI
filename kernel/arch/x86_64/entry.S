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
