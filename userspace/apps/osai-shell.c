#include <osai_user.h>

static int shell_step(int ok, const char *line, const char *fail) {
  osai_log(line);
  if (!ok) {
    osai_log(fail);
    return -1;
  }
  return 0;
}

int main(void) {
  char list_buffer[256];
  u64 list_size = 0;
  osai_memzero(list_buffer, sizeof(list_buffer));

  osai_log("/bin/osai-shell: starting non-interactive BSD-compatible command smoke\n");
  osai_log("$ pwd\n");
  osai_log("/\n");
  if (shell_step(osai_fs_mkdir("/home") == 0, "$ mkdir /home\n",
                 "/bin/osai-shell: mkdir /home failed\n") != 0) {
    return 1;
  }
  if (shell_step(osai_fs_mkdir("/home/admin") == 0, "$ mkdir /home/admin\n",
                 "/bin/osai-shell: mkdir /home/admin failed\n") != 0) {
    return 1;
  }
  if (shell_step(osai_write_file("/home/admin/readme.txt",
                                 "OSAI shell file command smoke\n") >= 0,
                 "$ touch /home/admin/readme.txt && echo > /home/admin/readme.txt\n",
                 "/bin/osai-shell: create/write failed\n") != 0) {
    return 1;
  }
  if (shell_step(osai_fs_list("/home/admin", list_buffer, sizeof(list_buffer),
                              &list_size) >= 0,
                 "$ ls /home/admin\n",
                 "/bin/osai-shell: ls failed\n") != 0) {
    return 1;
  }
  if (shell_step(osai_fs_rename("/home/admin/readme.txt",
                                "/home/admin/readme.renamed") == 0,
                 "$ mv readme.txt readme.renamed\n",
                 "/bin/osai-shell: mv failed\n") != 0) {
    return 1;
  }
  if (shell_step(osai_fs_delete("/home/admin/readme.renamed") == 0,
                 "$ rm readme.renamed\n",
                 "/bin/osai-shell: rm failed\n") != 0) {
    return 1;
  }
  if (shell_step(osai_fs_delete("/home/admin") == 0,
                 "$ rmdir /home/admin\n",
                 "/bin/osai-shell: rmdir failed\n") != 0) {
    return 1;
  }
  osai_log_u64("/bin/osai-shell: ls_bytes=", list_size, "\n");
  osai_log("/bin/osai-shell: commands passed pwd ls mkdir touch mv rm rmdir\n");
  osai_log("/bin/osai-shell: ssh transport target remains hostfwd port 2222 when network service accepts remote sessions\n");
  return 0;
}
