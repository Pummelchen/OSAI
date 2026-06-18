#include <xaios_user.h>

int main(void) {
  xaios_log("/bin/hello: hello world from C userspace\n");
  xaios_log("/bin/hello: C toolchain and EL0 runtime integration passed\n");
  return 0;
}
