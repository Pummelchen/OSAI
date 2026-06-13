#include <osai/klog.h>
#include <osai/pmm.h>
#include <osai/smp.h>
#include <osai/telemetry.h>
#include <osai/timer.h>

void telemetry_emit_boot_summary(void) {
  klog("telemetry: boot_summary cpu_online=%u pmm_total=%lu pmm_free=%lu timer_hz=%lu\n",
       smp_online_count(), pmm_total_pages(), pmm_free_pages(),
       timer_frequency_hz());
}
