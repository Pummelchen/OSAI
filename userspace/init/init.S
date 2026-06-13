.section .text, "ax"
.global _start
.type _start, %function

_start:
  mov x0, #0
  mov x1, #0
  mov x8, #99
  svc #0
  cmn x0, #1
  b.ne bad_syscall_failed

  mov x0, #0
  mov x1, #8
  mov x8, #1
  svc #0
  cmn x0, #1
  b.ne bad_syscall_failed

  adr x0, msg_bad_syscalls
  mov x1, #32
  mov x8, #1
  svc #0

  adr x0, msg_start
  mov x1, #22
  mov x8, #1
  svc #0

  adr x0, cmd_start
  mov x1, #19
  mov x8, #3
  svc #0

  adr x0, cmd_status
  mov x1, #20
  mov x8, #3
  svc #0

  adr x0, msg_done
  mov x1, #30
  mov x8, #1
  svc #0

  mov x0, #0
  mov x8, #2
  svc #0

1:
  wfe
  b 1b

bad_syscall_failed:
  adr x0, msg_bad_failed
  mov x1, #32
  mov x8, #1
  svc #0
  mov x0, #1
  mov x8, #2
  svc #0
  b 1b

.section .rodata, "a"
msg_bad_syscalls:
  .ascii "/init: bad syscall tests passed\n"
msg_bad_failed:
  .ascii "/init: bad syscall tests failed\n"
msg_start:
  .ascii "/init: hello from ELF\n"
cmd_start:
  .ascii "service start /init"
cmd_status:
  .ascii "service status /init"
msg_done:
  .ascii "/init: service setup complete\n"
