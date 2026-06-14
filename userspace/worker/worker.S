.section .text, "ax"
.global _start
.type _start, %function

_start:
  adr x0, msg_start
  adr x1, msg_start_end
  sub x1, x1, x0
  mov x8, #1
  svc #0
  cmn x0, #1
  b.eq failed

  mov x0, #0
  mov x8, #2
  svc #0

1:
  wfe
  b 1b

failed:
  mov x0, #1
  mov x8, #2
  svc #0
  b 1b

.section .rodata, "a"
msg_start:
  .ascii "/worker: scheduled child process ran\n"
msg_start_end:
