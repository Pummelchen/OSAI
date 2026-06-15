#include <osai_user.h>

int main(void) {
  char output[160];
  u64 out = 0;
  osai_memzero(output, sizeof(output));
  osai_log("/bin/sshtest: validating ssh-compatible remote login session\n");
  if (osai_remote_login("admin", "status", output, sizeof(output), &out) < 0 ||
      out == 0) {
    osai_log("/bin/sshtest: remote login status command failed\n");
    return 1;
  }
  osai_log("/bin/sshtest: status output=");
  osai_log(output);
  osai_memzero(output, sizeof(output));
  out = 0;
  if (osai_remote_login("admin", "ls /", output, sizeof(output), &out) < 0 ||
      out == 0) {
    osai_log("/bin/sshtest: remote login ls command failed\n");
    return 1;
  }
  osai_log_u64("/bin/sshtest: remote_output_bytes=", out, "\n");
  osai_log("/bin/sshtest: interactive remote login command surface passed\n");
  osai_log("/bin/sshtest: complete\n");
  return 0;
}
