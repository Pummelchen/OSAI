#ifndef OSAI_SYSCALL_H
#define OSAI_SYSCALL_H

#include <osai/status.h>
#include <osai/types.h>

#define OSAI_SYSCALL_LOG UINT64_C(1)
#define OSAI_SYSCALL_EXIT UINT64_C(2)
#define OSAI_SYSCALL_OSCTL UINT64_C(3)
#define OSAI_SYSCALL_READ_SERVICE_DESCRIPTOR UINT64_C(4)
#define OSAI_SYSCALL_SERVICE_STATUS UINT64_C(5)
#define OSAI_SYSCALL_SERVICE_START UINT64_C(6)
#define OSAI_SYSCALL_SERVICE_STOP UINT64_C(7)
#define OSAI_SYSCALL_SERVICE_RESTART UINT64_C(8)
#define OSAI_SYSCALL_SERVICE_ROLLBACK UINT64_C(9)
#define OSAI_SYSCALL_SERVICE_UPDATE UINT64_C(10)

#define OSAI_CAP_LOG UINT64_C(1)
#define OSAI_CAP_EXIT UINT64_C(2)
#define OSAI_CAP_OSCTL UINT64_C(4)
#define OSAI_CAP_SERVICE_ROLLBACK UINT64_C(8)
#define OSAI_CAP_UPDATE UINT64_C(16)
#define OSAI_CAP_FS_READ UINT64_C(32)
#define OSAI_CAP_SERVICE_CONTROL UINT64_C(64)
#define OSAI_CAP_ADMIN UINT64_C(128)

uint64_t syscall_dispatch(uint64_t syscall, uint64_t arg0, uint64_t arg1,
                          uint64_t arg2);
void syscall_self_test(void);
uint64_t syscall_control_plane_count(void);
uint64_t syscall_control_plane_denial_count(void);
uint64_t syscall_service_descriptor_read_count(void);

#endif
