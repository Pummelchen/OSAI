#ifndef OSAI_TIMER_H
#define OSAI_TIMER_H

#include <osai/types.h>

void timer_init(void);
uint64_t timer_counter(void);
uint64_t timer_frequency_hz(void);
uint64_t timer_now_ns(void);
void timer_self_test(void);

#endif
