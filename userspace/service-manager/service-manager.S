.section .text, "ax"
.global _start
.type _start, %function

_start:
  adr x0, msg_hello
  mov x1, #33
  mov x8, #1
  svc #0

  adr x0, descriptor_buffer
  mov x1, #256
  mov x8, #4
  svc #0
  cmn x0, #1
  b.eq manager_failed

  adr x0, msg_descriptor_loaded
  mov x1, #36
  mov x8, #1
  svc #0

  adr x0, service_init
  mov x1, #5
  mov x8, #9
  svc #0
  cmn x0, #1
  b.ne manager_failed

  adr x0, update_signature
  mov x1, #18
  mov x8, #10
  svc #0
  cmn x0, #1
  b.ne manager_failed

  adr x0, msg_missing_caps
  mov x1, #50
  mov x8, #1
  svc #0

  adr x0, cmd_define_child
  mov x1, #59
  mov x8, #3
  svc #0
  cmn x0, #1
  b.eq manager_failed

  adr x0, service_child
  mov x1, #17
  mov x8, #6
  svc #0
  cmn x0, #1
  b.eq manager_failed

  adr x0, service_child
  mov x1, #17
  mov x8, #5
  svc #0
  cmn x0, #1
  b.eq manager_failed

  adr x0, service_child
  mov x1, #17
  mov x8, #8
  svc #0
  cmn x0, #1
  b.ne manager_failed

  adr x0, msg_child_supervised
  mov x1, #43
  mov x8, #1
  svc #0

  adr x0, cmd_configure_child
  mov x1, #76
  mov x8, #3
  svc #0
  cmn x0, #1
  b.eq manager_failed

  adr x0, cmd_log_child
  mov x1, #40
  mov x8, #3
  svc #0
  cmn x0, #1
  b.eq manager_failed

  adr x0, cmd_crash_child
  mov x1, #38
  mov x8, #3
  svc #0
  cmn x0, #1
  b.eq manager_failed

  adr x0, service_child
  mov x1, #17
  mov x8, #5
  svc #0
  cmn x0, #1
  b.eq manager_failed

  adr x0, msg_child_crash_supervised
  mov x1, #41
  mov x8, #1
  svc #0

  adr x0, cmd_admin_policy
  mov x1, #12
  mov x8, #3
  svc #0
  cmn x0, #1
  b.eq manager_failed

  adr x0, cmd_admin_status_child
  mov x1, #30
  mov x8, #3
  svc #0
  cmn x0, #1
  b.eq manager_failed

  adr x0, cmd_admin_export_child
  mov x1, #30
  mov x8, #3
  svc #0
  cmn x0, #1
  b.eq manager_failed

  adr x0, cmd_admin_logs_child
  mov x1, #28
  mov x8, #3
  svc #0
  cmn x0, #1
  b.eq manager_failed

  adr x0, msg_admin_ready
  mov x1, #40
  mov x8, #1
  svc #0

  adr x0, cmd_admin_safe_status
  mov x1, #24
  mov x8, #3
  svc #0
  cmn x0, #1
  b.eq manager_failed

  adr x0, cmd_admin_safe_shell
  mov x1, #23
  mov x8, #3
  svc #0
  cmn x0, #1
  b.ne manager_failed

  adr x0, msg_admin_safe
  mov x1, #44
  mov x8, #1
  svc #0

  adr x0, cmd_osctl_status
  adr x1, cmd_osctl_status_end
  sub x1, x1, x0
  mov x8, #3
  svc #0
  cmn x0, #1
  b.eq manager_failed

  adr x0, cmd_osctl_ps
  adr x1, cmd_osctl_ps_end
  sub x1, x1, x0
  mov x8, #3
  svc #0
  cmn x0, #1
  b.eq manager_failed

  adr x0, cmd_osctl_services
  adr x1, cmd_osctl_services_end
  sub x1, x1, x0
  mov x8, #3
  svc #0
  cmn x0, #1
  b.eq manager_failed

  adr x0, cmd_osctl_cells
  adr x1, cmd_osctl_cells_end
  sub x1, x1, x0
  mov x8, #3
  svc #0
  cmn x0, #1
  b.eq manager_failed

  adr x0, cmd_osctl_fs
  adr x1, cmd_osctl_fs_end
  sub x1, x1, x0
  mov x8, #3
  svc #0
  cmn x0, #1
  b.eq manager_failed

  adr x0, cmd_osctl_net
  adr x1, cmd_osctl_net_end
  sub x1, x1, x0
  mov x8, #3
  svc #0
  cmn x0, #1
  b.eq manager_failed

  adr x0, cmd_osctl_telemetry
  adr x1, cmd_osctl_telemetry_end
  sub x1, x1, x0
  mov x8, #3
  svc #0
  cmn x0, #1
  b.eq manager_failed

  adr x0, cmd_osctl_update
  adr x1, cmd_osctl_update_end
  sub x1, x1, x0
  mov x8, #3
  svc #0
  cmn x0, #1
  b.eq manager_failed

  adr x0, cmd_osctl_rollback
  adr x1, cmd_osctl_rollback_end
  sub x1, x1, x0
  mov x8, #3
  svc #0
  cmn x0, #1
  b.eq manager_failed

  adr x0, msg_osctl_ready
  adr x1, msg_osctl_ready_end
  sub x1, x1, x0
  mov x8, #1
  svc #0

  adr x0, msg_done
  mov x1, #41
  mov x8, #1
  svc #0

  mov x0, #0
  mov x8, #2
  svc #0

1:
  wfe
  b 1b

manager_failed:
  adr x0, msg_failed
  mov x1, #33
  mov x8, #1
  svc #0
  mov x0, #1
  mov x8, #2
  svc #0
  b 1b

.section .rodata, "a"
msg_hello:
  .ascii "/service-manager: hello from ELF\n"
msg_descriptor_loaded:
  .ascii "/service-manager: descriptor loaded\n"
msg_missing_caps:
  .ascii "/service-manager: missing capability tests passed\n"
msg_child_supervised:
  .ascii "/service-manager: child service supervised\n"
msg_child_crash_supervised:
  .ascii "/service-manager: child crash supervised\n"
msg_admin_ready:
  .ascii "/service-manager: admin status exported\n"
msg_admin_safe:
  .ascii "/service-manager: remote-safe checks passed\n"
msg_osctl_ready:
  .ascii "/service-manager: osctl command surface passed\n"
msg_osctl_ready_end:
msg_done:
  .ascii "/service-manager: control plane complete\n"
msg_failed:
  .ascii "/service-manager: control failed\n"
cmd_define_child:
  .ascii "service define /svc/source-index parent=/init restart=never"
cmd_configure_child:
  .ascii "service configure /svc/source-index restart=always log=serial max_restarts=2"
cmd_log_child:
  .ascii "service log /svc/source-index crash-test"
cmd_crash_child:
  .ascii "service crash /svc/source-index code=7"
cmd_admin_policy:
  .ascii "admin policy"
cmd_admin_status_child:
  .ascii "admin status /svc/source-index"
cmd_admin_export_child:
  .ascii "admin export /svc/source-index"
cmd_admin_logs_child:
  .ascii "admin logs /svc/source-index"
cmd_admin_safe_status:
  .ascii "admin remote-safe status"
cmd_admin_safe_shell:
  .ascii "admin remote-safe shell"
cmd_osctl_status:
  .ascii "osctl status"
cmd_osctl_status_end:
cmd_osctl_ps:
  .ascii "osctl ps"
cmd_osctl_ps_end:
cmd_osctl_services:
  .ascii "osctl services"
cmd_osctl_services_end:
cmd_osctl_cells:
  .ascii "osctl cells"
cmd_osctl_cells_end:
cmd_osctl_fs:
  .ascii "osctl fs"
cmd_osctl_fs_end:
cmd_osctl_net:
  .ascii "osctl net"
cmd_osctl_net_end:
cmd_osctl_telemetry:
  .ascii "osctl telemetry"
cmd_osctl_telemetry_end:
cmd_osctl_update:
  .ascii "osctl update"
cmd_osctl_update_end:
cmd_osctl_rollback:
  .ascii "osctl rollback"
cmd_osctl_rollback_end:
service_child:
  .ascii "/svc/source-index"
service_init:
  .ascii "/init"
update_signature:
  .ascii "signed-update-test"

.section .bss, "aw", %nobits
.balign 16
descriptor_buffer:
  .skip 256
