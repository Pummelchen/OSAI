#include <osai/assert.h>
#include <osai/klog.h>
#include <osai/timer.h>

static uint64_t g_timer_frequency_hz;

static uint64_t read_cntfrq_el0(void) {
  uint64_t value = 0;
  __asm__ volatile("mrs %[value], cntfrq_el0" : [value] "=r"(value));
  return value;
}

uint64_t timer_counter(void) {
  uint64_t value = 0;
  __asm__ volatile("isb\nmrs %[value], cntpct_el0" : [value] "=r"(value));
  return value;
}

uint64_t timer_frequency_hz(void) {
  return g_timer_frequency_hz;
}

uint64_t timer_now_ns(void) {
  uint64_t counter = timer_counter();
  if (g_timer_frequency_hz == 0) {
    return 0;
  }

  return (counter / g_timer_frequency_hz) * UINT64_C(1000000000) +
         ((counter % g_timer_frequency_hz) * UINT64_C(1000000000)) /
             g_timer_frequency_hz;
}

void timer_init(void) {
  g_timer_frequency_hz = read_cntfrq_el0();
  klog("timer: generic counter frequency=%lu Hz\n", g_timer_frequency_hz);
  kassert(g_timer_frequency_hz != 0);
}

void timer_self_test(void) {
  uint64_t c0 = timer_counter();
  uint64_t n0 = timer_now_ns();

  for (volatile uint64_t spin = 0; spin < UINT64_C(100000); ++spin) {
  }

  uint64_t c1 = timer_counter();
  uint64_t n1 = timer_now_ns();

  kassert(c1 >= c0);
  kassert(n1 >= n0);
  klog("timer: monotonic self-test passed counter_delta=%lu ns_delta=%lu\n",
       c1 - c0, n1 - n0);
}
