#include <osai_user.h>

int main(void) {
  osai_log("/bin/hello: hello world from C userspace\n");
  osai_log("/bin/hello: C toolchain and EL0 runtime integration passed\n");
  return 0;
}
