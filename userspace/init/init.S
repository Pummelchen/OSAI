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

  adr x0, cmd_configure
  mov x1, #63
  mov x8, #3
  svc #0

  adr x0, cmd_log_ready
  mov x1, #31
  mov x8, #3
  svc #0

  adr x0, msg_policy_ready
  mov x1, #36
  mov x8, #1
  svc #0

  adr x0, cmd_status
  mov x1, #20
  mov x8, #3
  svc #0

  adr x0, cmd_restart
  mov x1, #21
  mov x8, #3
  svc #0
  cmn x0, #1
  b.ne manager_policy_failed

  adr x0, msg_restart_denied
  mov x1, #32
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

  adr x0, cmd_define_child
  mov x1, #59
  mov x8, #3
  svc #0

  adr x0, cmd_start_child
  mov x1, #31
  mov x8, #3
  svc #0

  adr x0, cmd_status_child
  mov x1, #32
  mov x8, #3
  svc #0

  adr x0, msg_child_supervised
  mov x1, #32
  mov x8, #1
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

manager_policy_failed:
  adr x0, msg_manager_failed
  mov x1, #36
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
msg_manager_failed:
  .ascii "/init: service manager policy failed\n"
msg_start:
  .ascii "/init: hello from ELF\n"
msg_policy_ready:
  .ascii "/init: service manager policy ready\n"
msg_restart_denied:
  .ascii "/init: restart denied by policy\n"
cmd_configure:
  .ascii "service configure /init restart=never log=serial max_restarts=0"
cmd_log_ready:
  .ascii "service log /init manager-ready"
cmd_restart:
  .ascii "service restart /init"
cmd_start:
  .ascii "service start /init"
cmd_status:
  .ascii "service status /init"
cmd_define_child:
  .ascii "service define /svc/source-index parent=/init restart=never"
cmd_start_child:
  .ascii "service start /svc/source-index"
cmd_status_child:
  .ascii "service status /svc/source-index"
msg_child_supervised:
  .ascii "/init: child service supervised\n"
msg_done:
  .ascii "/init: service setup complete\n"
