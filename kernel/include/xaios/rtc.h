#ifndef XAIOS_RTC_H
#define XAIOS_RTC_H

#include <xaios/types.h>

#define XAIOS_PL031_RTC_BASE UINT64_C(0x01010000)

void rtc_init(void);
uint32_t rtc_read_epoch(void);
void rtc_self_test(void);

#endif
