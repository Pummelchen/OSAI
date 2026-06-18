#include <xaios_user.h>

int main(void) {
  u64 workers = 0;
  u64 checksum = 0;
  u64 threads = 0;
  u64 thread_checksum = 0;
  xaios_log("/bin/smptest: validating SMP-visible scheduler state\n");
  if (xaios_smp_run(4, 128, &workers, &checksum) < 0 || workers == 0 ||
      checksum == 0) {
    xaios_log("/bin/smptest: smp worker syscall failed\n");
    return 1;
  }
  if (xaios_thread_group_run(6, 256, &threads, &thread_checksum) < 0 ||
      threads != 6 || thread_checksum == 0) {
    xaios_log("/bin/smptest: user thread group syscall failed\n");
    return 1;
  }
  (void)xaios_osctl("osctl ps");
  xaios_log_u64("/bin/smptest: workers=", workers, "\n");
  xaios_log_u64("/bin/smptest: checksum=", checksum, "\n");
  xaios_log_u64("/bin/smptest: user_threads=", threads, "\n");
  xaios_log_u64("/bin/smptest: thread_checksum=", thread_checksum, "\n");
  xaios_log("/bin/smptest: app-requested SMP worker set passed\n");
  xaios_log("/bin/smptest: POSIX-style arbitrary user thread group passed\n");
  xaios_log("/bin/smptest: complete\n");
  return 0;
}
