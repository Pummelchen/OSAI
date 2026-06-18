#ifndef XAIOS_KLOG_H
#define XAIOS_KLOG_H

#include <xaios/boot_info.h>
#include <xaios/klog_ring.h>

void klog_init(const xaios_boot_info_t *boot);
void klog(const char *fmt, ...);
void klog_level(xaios_log_level_t level, const char *fmt, ...);
void klog_puts(const char *message);
void klog_write(const char *message, uint64_t length);

#endif
