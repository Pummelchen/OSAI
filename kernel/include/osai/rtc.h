#ifndef OSAI_RTC_H
#define OSAI_RTC_H

#include <osai/types.h>

#define OSAI_PL031_RTC_BASE UINT64_C(0x01010000)

void rtc_init(void);
uint32_t rtc_read_epoch(void);
void rtc_self_test(void);

#endif
