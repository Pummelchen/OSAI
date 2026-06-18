#include <xaios_user.h>

static int check_result(const char *command, const char *label, char *output,
                       u64 output_size) {
  u64 out = 0;
  if (xaios_remote_login("admin", command, output, output_size, &out) < 0) {
    xaios_log(label);
    return 1;
  }
  return 0;
}

int main(void) {
  char output[320];
  xaios_memzero(output, sizeof(output));

  xaios_log("/bin/sshtest: validating ssh-compatible remote login command surface\n");

  if (check_result("pwd", "/bin/sshtest: pwd failed\n", output,
                   sizeof(output)) != 0) {
    return 1;
  }
  if (check_result("cd", "/bin/sshtest: cd failed\n", output, sizeof(output)) != 0) {
    return 1;
  }
  if (check_result("help", "/bin/sshtest: help failed\n", output,
                   sizeof(output)) != 0) {
    return 1;
  }
  if (check_result("status", "/bin/sshtest: status failed\n", output,
                   sizeof(output)) != 0) {
    return 1;
  }
  if (check_result("sysinfo", "/bin/sshtest: sysinfo failed\n", output,
                   sizeof(output)) != 0) {
    return 1;
  }
  if (check_result("mkdir /state/remote-login-test",
                   "/bin/sshtest: mkdir failed\n", output, sizeof(output)) != 0) {
    return 1;
  }
  if (check_result("cd /state/remote-login-test",
                   "/bin/sshtest: cd failed\n", output, sizeof(output)) != 0) {
    return 1;
  }
  if (check_result("touch readme.txt", "/bin/sshtest: touch failed\n", output,
                   sizeof(output)) != 0) {
    return 1;
  }
  if (check_result("write readme.txt hello world", "/bin/sshtest: write failed\n",
                   output, sizeof(output)) != 0) {
    return 1;
  }
  if (check_result("cat readme.txt", "/bin/sshtest: cat failed\n", output,
                   sizeof(output)) != 0) {
    return 1;
  }
  if (check_result("echo hello world", "/bin/sshtest: echo failed\n", output,
                   sizeof(output)) != 0) {
    return 1;
  }
  if (check_result("ls -a", "/bin/sshtest: ls -a failed\n", output,
                   sizeof(output)) != 0) {
    return 1;
  }
  if (check_result("ls -l /", "/bin/sshtest: ls -l / failed\n", output,
                   sizeof(output)) != 0) {
    return 1;
  }
  if (check_result("ls -la /", "/bin/sshtest: ls -la / failed\n", output,
                   sizeof(output)) != 0) {
    return 1;
  }
  if (check_result("ls -al /", "/bin/sshtest: ls -al / failed\n", output,
                   sizeof(output)) != 0) {
    return 1;
  }
  if (check_result("ls -a -l /", "/bin/sshtest: ls -a -l / failed\n", output,
                   sizeof(output)) != 0) {
    return 1;
  }
  if (check_result("l /", "/bin/sshtest: l failed\n", output, sizeof(output)) != 0) {
    return 1;
  }
  if (check_result("ll /", "/bin/sshtest: ll failed\n", output,
                   sizeof(output)) != 0) {
    return 1;
  }
  if (check_result("la /", "/bin/sshtest: la failed\n", output,
                   sizeof(output)) != 0) {
    return 1;
  }
  if (check_result("cp readme.txt readme.copy", "/bin/sshtest: cp failed\n",
                   output, sizeof(output)) != 0) {
    return 1;
  }
  if (check_result("ls", "/bin/sshtest: ls failed\n", output, sizeof(output)) !=
      0) {
    return 1;
  }
  if (check_result("grep readme readme.txt",
                   "/bin/sshtest: grep failed\n", output, sizeof(output)) != 0) {
    return 1;
  }
  if (check_result("grep readme /state/remote-login-test/readme.txt",
                   "/bin/sshtest: grep absolute path failed\n", output,
                   sizeof(output)) != 0) {
    return 1;
  }
  if (check_result("find . -name readme.copy",
                   "/bin/sshtest: find failed\n", output, sizeof(output)) != 0) {
    return 1;
  }
  if (check_result("find /state/remote-login-test -name *.txt",
                   "/bin/sshtest: find wildcard failed\n", output,
                   sizeof(output)) != 0) {
    return 1;
  }
  if (check_result("head readme.copy", "/bin/sshtest: head failed\n", output,
                   sizeof(output)) != 0) {
    return 1;
  }
  if (check_result("head -n 1 readme.copy",
                   "/bin/sshtest: head -n failed\n", output,
                   sizeof(output)) != 0) {
    return 1;
  }
  if (check_result("tail readme.copy", "/bin/sshtest: tail failed\n", output,
                   sizeof(output)) != 0) {
    return 1;
  }
  if (check_result("tail -n 1 readme.copy",
                   "/bin/sshtest: tail -n failed\n", output,
                   sizeof(output)) != 0) {
    return 1;
  }
  if (check_result("stat readme.copy", "/bin/sshtest: stat failed\n", output,
                   sizeof(output)) != 0) {
    return 1;
  }
  if (check_result("echo shell command surface check",
                   "/bin/sshtest: echo failed\n", output, sizeof(output)) != 0) {
    return 1;
  }
  if (check_result("cpio -o -O backup.cpio readme.txt readme.copy",
                   "/bin/sshtest: cpio create failed\n", output, sizeof(output)) !=
      0) {
    return 1;
  }
  if (check_result("tar -cf backup.tar readme.txt readme.copy",
                   "/bin/sshtest: tar create failed\n", output, sizeof(output)) != 0) {
    return 1;
  }
  if (check_result("tar -tf backup.tar",
                   "/bin/sshtest: tar list failed\n", output,
                   sizeof(output)) != 0) {
    return 1;
  }
  if (check_result("mv readme.txt readme.renamed.txt",
                   "/bin/sshtest: mv failed\n", output, sizeof(output)) != 0) {
    return 1;
  }
  if (check_result("rm readme.renamed.txt", "/bin/sshtest: rm failed\n",
                   output, sizeof(output)) != 0) {
    return 1;
  }
  if (check_result("rm readme.copy", "/bin/sshtest: rm copy failed\n", output,
                   sizeof(output)) != 0) {
    return 1;
  }
  if (check_result("cpio -i -I backup.cpio", "/bin/sshtest: cpio extract failed\n",
                   output, sizeof(output)) != 0) {
    return 1;
  }
  if (check_result("tar -xf backup.tar", "/bin/sshtest: tar extract failed\n",
                   output, sizeof(output)) != 0) {
    return 1;
  }
  if (check_result("rm readme.txt", "/bin/sshtest: rm failed\n", output, sizeof(output)) !=
      0) {
    return 1;
  }
  if (check_result("rm readme.copy", "/bin/sshtest: rm copy failed\n", output,
                   sizeof(output)) != 0) {
    return 1;
  }
  if (check_result("rm backup.cpio", "/bin/sshtest: rm backup.cpio failed\n", output,
                   sizeof(output)) != 0) {
    return 1;
  }
  if (check_result("rm backup.tar", "/bin/sshtest: rm backup.tar failed\n", output,
                   sizeof(output)) != 0) {
    return 1;
  }
  if (check_result("rmdir /state/remote-login-test", "/bin/sshtest: rmdir failed\n",
                   output, sizeof(output)) != 0) {
    return 1;
  }
  if (check_result("stat /state", "/bin/sshtest: stat failed\n", output,
                   sizeof(output)) != 0) {
    return 1;
  }
  if (check_result("cd /", "/bin/sshtest: final cd failed\n", output,
                   sizeof(output)) != 0) {
    return 1;
  }
  if (check_result("ls /", "/bin/sshtest: final ls failed\n", output,
                   sizeof(output)) != 0) {
    return 1;
  }
  if (check_result("exit", "/bin/sshtest: exit failed\n", output,
                   sizeof(output)) != 0) {
    return 1;
  }

  xaios_log("/bin/sshtest: interactive remote login command surface passed\n");
  xaios_log("/bin/sshtest: complete\n");
  return 0;
}
