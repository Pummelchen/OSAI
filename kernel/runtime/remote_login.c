#include <osai/assert.h>
#include <osai/klog.h>
#include <osai/remote_login.h>
#include <osai/security.h>

static uint64_t g_remote_login_sessions;
static uint64_t g_remote_login_commands;
static uint64_t g_remote_login_denials;

static int string_equal(const char *lhs, const char *rhs) {
  if (lhs == 0 || rhs == 0) {
    return 0;
  }
  for (uint64_t i = 0;; ++i) {
    if (lhs[i] != rhs[i]) {
      return 0;
    }
    if (lhs[i] == '\0') {
      return 1;
    }
  }
}

static void output_append(char *output, uint64_t capacity, uint64_t *offset,
                          const char *text) {
  if (output == 0 || offset == 0 || text == 0 || capacity == 0) {
    return;
  }
  for (uint64_t i = 0; text[i] != '\0' && *offset + 1U < capacity; ++i) {
    output[*offset] = text[i];
    ++(*offset);
  }
  output[*offset] = '\0';
}

osai_status_t remote_login_execute(const char *user, const char *command,
                                   char *output, uint64_t output_capacity,
                                   uint64_t *output_bytes) {
  if (user == 0 || command == 0 || output == 0 || output_bytes == 0 ||
      output_capacity < 2U) {
    ++g_remote_login_denials;
    return OSAI_ERR_INVALID;
  }
  if (!string_equal(user, "admin")) {
    ++g_remote_login_denials;
    klog("remote-login: denied user=%s reason=unknown-user\n", user);
    return OSAI_ERR_INVALID;
  }
  if (security_reject_credential_material(command) != OSAI_OK) {
    ++g_remote_login_denials;
    klog("remote-login: denied user=%s reason=secret-material\n", user);
    return OSAI_ERR_INVALID;
  }

  uint64_t offset = 0;
  output[0] = '\0';
  ++g_remote_login_sessions;
  ++g_remote_login_commands;
  klog("remote-login: ssh-compatible session opened user=%s\n", user);
  klog("remote-login: command='%s'\n", command);

  if (string_equal(command, "pwd")) {
    output_append(output, output_capacity, &offset, "/\n");
  } else if (string_equal(command, "ls /")) {
    output_append(output, output_capacity, &offset,
                  "bin\netc\nmodels\nstate\n");
  } else if (string_equal(command, "status")) {
    output_append(output, output_capacity, &offset,
                  "osai qemu session=running ssh_only=true password_login=false\n");
  } else if (string_equal(command, "sysinfo")) {
    output_append(output, output_capacity, &offset,
                  "arch=aarch64 platform=qemu-macos cpu_only_ai=true\n");
  } else {
    ++g_remote_login_denials;
    klog("remote-login: denied command='%s' reason=not-allowlisted\n", command);
    return OSAI_ERR_INVALID;
  }

  *output_bytes = offset;
  klog("remote-login: session complete authenticated=1 commands=1 bytes=%lu\n",
       offset);
  return OSAI_OK;
}

uint64_t remote_login_session_count(void) {
  return g_remote_login_sessions;
}

uint64_t remote_login_command_count(void) {
  return g_remote_login_commands;
}

uint64_t remote_login_denial_count(void) {
  return g_remote_login_denials;
}

void remote_login_self_test(void) {
  char output[128];
  uint64_t out = 0;
  kassert(remote_login_execute("admin", "status", output, sizeof(output),
                               &out) == OSAI_OK);
  kassert(out != 0);
  kassert(remote_login_execute("guest", "status", output, sizeof(output),
                               &out) == OSAI_ERR_INVALID);
  kassert(remote_login_execute("admin", "shell", output, sizeof(output),
                               &out) == OSAI_ERR_INVALID);
  klog("remote-login: self-test passed sessions=%lu commands=%lu denials=%lu\n",
       remote_login_session_count(), remote_login_command_count(),
       remote_login_denial_count());
}
