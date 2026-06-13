#include <osai/klog.h>
#include <osai/user.h>
#include <osai/types.h>

extern char __user_init_entry[];
extern char __user_init_stack_top[];

void user_init_run(void) {
  uint64_t entry = (uint64_t)(uintptr_t)__user_init_entry;
  uint64_t stack = (uint64_t)(uintptr_t)__user_init_stack_top;
  uint64_t spsr = 0;

  klog("user: starting /init entry=0x%lx stack=0x%lx\n", entry, stack);

  __asm__ volatile(
      "msr sp_el0, %[stack]\n"
      "msr elr_el1, %[entry]\n"
      "msr spsr_el1, %[spsr]\n"
      "eret\n"
      :
      : [stack] "r"(stack), [entry] "r"(entry), [spsr] "r"(spsr)
      : "memory");
}
