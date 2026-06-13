.section .text, "ax"
.global _start
.type _start, %function

_start:
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

.section .rodata, "a"
msg_start:
  .ascii "/init: hello from ELF\n"
cmd_start:
  .ascii "service start /init"
cmd_status:
  .ascii "service status /init"
msg_done:
  .ascii "/init: service setup complete\n"
