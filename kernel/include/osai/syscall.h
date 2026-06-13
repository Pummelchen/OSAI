#ifndef OSAI_SYSCALL_H
#define OSAI_SYSCALL_H

#include <osai/status.h>
#include <osai/types.h>

#define OSAI_SYSCALL_LOG UINT64_C(1)
#define OSAI_SYSCALL_EXIT UINT64_C(2)
#define OSAI_SYSCALL_OSCTL UINT64_C(3)

#define OSAI_CAP_LOG UINT64_C(1)
#define OSAI_CAP_EXIT UINT64_C(2)
#define OSAI_CAP_OSCTL UINT64_C(4)
#define OSAI_CAP_SERVICE_ROLLBACK UINT64_C(8)
#define OSAI_CAP_UPDATE UINT64_C(16)

uint64_t syscall_dispatch(uint64_t syscall, uint64_t arg0, uint64_t arg1,
                          uint64_t arg2);
void syscall_self_test(void);

#endif
