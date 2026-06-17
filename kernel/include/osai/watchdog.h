#ifndef OSAI_WATCHDOG_H
#define OSAI_WATCHDOG_H

#include <osai/types.h>

#define OSAI_WATCHDOG_TIMEOUT_SECONDS UINT32_C(10)
#define OSAI_BOOT_COUNTER_PATH "/var/boot_counter"
#define OSAI_BOOT_THRESHOLD UINT32_C(3)

void watchdog_init(void);
void watchdog_kick(void);
void watchdog_trigger_reset(void);
uint32_t watchdog_is_active(void);
void watchdog_self_test(void);

uint32_t boot_counter_read(void);
void boot_counter_increment(void);
void boot_counter_reset(void);
uint32_t boot_in_recovery_mode(void);

#endif
