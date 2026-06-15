#include <osai_user.h>

int main(void) {
  const char payload[] = "osai-nettest";
  u64 echoed = 0;
  u64 round_trips = 0;
  osai_log("/bin/nettest: validating ethernet tcp udp network telemetry\n");
  if (osai_net_udp_echo(payload, osai_strlen(payload), &echoed) < 0 ||
      echoed != osai_strlen(payload)) {
    osai_log("/bin/nettest: udp echo syscall failed\n");
    return 1;
  }
  if (osai_net_tcp_connect(&round_trips) < 0 || round_trips != 2U) {
    osai_log("/bin/nettest: tcp connect syscall failed\n");
    return 1;
  }
  (void)osai_osctl("osctl net");
  osai_log_u64("/bin/nettest: udp_echo_bytes=", echoed, "\n");
  osai_log_u64("/bin/nettest: tcp_round_trips=", round_trips, "\n");
  osai_log("/bin/nettest: app-callable udp/tcp path passed\n");
  osai_log("/bin/nettest: complete\n");
  return 0;
}
