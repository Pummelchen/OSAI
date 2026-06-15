#include <osai_user.h>

static int cstr_eq(const char *lhs, const char *rhs) {
  if (lhs == 0 || rhs == 0) {
    return 0;
  }
  for (u64 i = 0;; ++i) {
    if (lhs[i] != rhs[i]) {
      return 0;
    }
    if (lhs[i] == '\0') {
      return 1;
    }
  }
}

static void log_command(const char *command) {
  osai_log("$ ");
  osai_log(command);
  osai_log("\n");
}

static int shell_run(const char *command, char *cwd, u64 cwd_size) {
  char buffer[256];
  u64 out_size = 0;
  osai_memzero(buffer, sizeof(buffer));

  log_command(command);
  if (cstr_eq(command, "pwd")) {
    osai_log(cwd);
    osai_log("\n");
    return 0;
  }
  if (cstr_eq(command, "mkdir /home")) {
    return osai_fs_mkdir("/home");
  }
  if (cstr_eq(command, "mkdir /home/admin")) {
    return osai_fs_mkdir("/home/admin");
  }
  if (cstr_eq(command, "cd /home/admin")) {
    osai_memzero(cwd, cwd_size);
    u64 offset = 0;
    osai_append_cstr(cwd, cwd_size, &offset, "/home/admin");
    return 0;
  }
  if (cstr_eq(command, "touch readme.txt")) {
    return osai_write_file("/home/admin/readme.txt", "") < 0 ? -1 : 0;
  }
  if (cstr_eq(command, "write readme.txt")) {
    return osai_write_file("/home/admin/readme.txt",
                           "OSAI interactive shell command smoke\n") < 0
               ? -1
               : 0;
  }
  if (cstr_eq(command, "cat readme.txt")) {
    if (osai_read_file("/home/admin/readme.txt", buffer,
                       sizeof(buffer) - 1U) < 0) {
      return -1;
    }
    osai_log(buffer);
    return 0;
  }
  if (cstr_eq(command, "ls")) {
    return osai_fs_list(cwd, buffer, sizeof(buffer), &out_size) < 0 ? -1 : 0;
  }
  if (cstr_eq(command, "mv readme.txt readme.renamed")) {
    return osai_fs_rename("/home/admin/readme.txt",
                          "/home/admin/readme.renamed");
  }
  if (cstr_eq(command, "rm readme.renamed")) {
    return osai_fs_delete("/home/admin/readme.renamed");
  }
  if (cstr_eq(command, "cd /")) {
    osai_memzero(cwd, cwd_size);
    cwd[0] = '/';
    cwd[1] = '\0';
    return 0;
  }
  if (cstr_eq(command, "rmdir /home/admin")) {
    return osai_fs_delete("/home/admin");
  }

  osai_log("/bin/osai-shell: unknown command\n");
  return -1;
}

int main(void) {
  char cwd[64];
  const char *commands[] = {
      "pwd",
      "mkdir /home",
      "mkdir /home/admin",
      "cd /home/admin",
      "pwd",
      "touch readme.txt",
      "write readme.txt",
      "cat readme.txt",
      "ls",
      "mv readme.txt readme.renamed",
      "rm readme.renamed",
      "cd /",
      "rmdir /home/admin",
  };

  osai_memzero(cwd, sizeof(cwd));
  cwd[0] = '/';
  cwd[1] = '\0';
  osai_log("/bin/osai-shell: starting scripted interactive command session\n");
  for (u64 i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i) {
    if (shell_run(commands[i], cwd, sizeof(cwd)) != 0) {
      osai_log("/bin/osai-shell: command failed\n");
      return 1;
    }
  }
  osai_log("/bin/osai-shell: commands passed pwd cd ls mkdir touch write cat mv rm rmdir\n");
  osai_log("/bin/osai-shell: command engine ready for QEMU remote-login surface\n");
  return 0;
}
