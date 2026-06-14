#include <osai_user.h>

int main(void) {
  osai_log("/bin/sysinfo: OSAI qemu-macos-aarch64 dev build\n");
  osai_log("/bin/sysinfo: architecture=aarch64 machine=qemu-virt cpu_only_ai=true gpu_required=false\n");
  osai_log("/bin/sysinfo: requesting kernel process, filesystem, network, and telemetry status\n");
  (void)osai_osctl("osctl status");
  (void)osai_osctl("osctl ps");
  (void)osai_osctl("osctl fs");
  (void)osai_osctl("osctl net");
  (void)osai_osctl("osctl telemetry");
  osai_log_u64("/bin/sysinfo: monotonic_nanos=", osai_clock_nanos(), "\n");
  osai_log("/bin/sysinfo: complete\n");
  return 0;
}
