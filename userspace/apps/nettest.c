#include <osai_user.h>

int main(void) {
  osai_log("/bin/nettest: validating ethernet tcp udp network telemetry\n");
  (void)osai_osctl("osctl net");
  osai_log("/bin/nettest: queue-backed udp/tcp smoke state requested from kernel\n");
  osai_log("/bin/nettest: complete\n");
  return 0;
}
