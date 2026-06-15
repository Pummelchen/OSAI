#include <osai_user.h>

int main(void) {
  u64 workers = 0;
  u64 checksum = 0;
  osai_log("/bin/smptest: validating SMP-visible scheduler state\n");
  if (osai_smp_run(4, 128, &workers, &checksum) < 0 || workers == 0 ||
      checksum == 0) {
    osai_log("/bin/smptest: smp worker syscall failed\n");
    return 1;
  }
  (void)osai_osctl("osctl ps");
  osai_log_u64("/bin/smptest: workers=", workers, "\n");
  osai_log_u64("/bin/smptest: checksum=", checksum, "\n");
  osai_log("/bin/smptest: app-requested SMP worker set passed\n");
  osai_log("/bin/smptest: complete\n");
  return 0;
}
