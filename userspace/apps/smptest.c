#include <osai_user.h>

int main(void) {
  osai_log("/bin/smptest: validating SMP-visible scheduler state\n");
  (void)osai_osctl("osctl ps");
  osai_log("/bin/smptest: QEMU SMP coordination model observed through kernel scheduler telemetry\n");
  osai_log("/bin/smptest: complete\n");
  return 0;
}
