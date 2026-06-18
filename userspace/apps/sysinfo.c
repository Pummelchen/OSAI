#include <xaios_user.h>

int main(void) {
  xaios_log("/bin/sysinfo: XAIOS qemu-macos-aarch64 dev build\n");
  xaios_log("/bin/sysinfo: architecture=aarch64 machine=qemu-virt cpu_only_ai=true gpu_required=false\n");
  xaios_log("/bin/sysinfo: requesting kernel process, filesystem, network, and telemetry status\n");
  (void)xaios_osctl("osctl status");
  (void)xaios_osctl("osctl ps");
  (void)xaios_osctl("osctl fs");
  (void)xaios_osctl("osctl net");
  (void)xaios_osctl("osctl telemetry");
  xaios_log_u64("/bin/sysinfo: monotonic_nanos=", xaios_clock_nanos(), "\n");
  xaios_log("/bin/sysinfo: complete\n");
  return 0;
}
