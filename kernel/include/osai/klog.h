#ifndef OSAI_KLOG_H
#define OSAI_KLOG_H

#include <osai/boot_info.h>

void klog_init(const osai_boot_info_t *boot);
void klog(const char *fmt, ...);
void klog_puts(const char *message);

#endif
