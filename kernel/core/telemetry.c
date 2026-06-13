#include <osai/ai_cell.h>
#include <osai/arena.h>
#include <osai/kheap.h>
#include <osai/klog.h>
#include <osai/pmm.h>
#include <osai/smp.h>
#include <osai/telemetry.h>
#include <osai/timer.h>
#include <osai/virtio_blk.h>

void telemetry_emit_boot_summary(void) {
  klog("telemetry: boot_summary cpu_online=%u pmm_total=%lu pmm_free=%lu timer_hz=%lu\n",
       smp_online_count(), pmm_total_pages(), pmm_free_pages(),
       timer_frequency_hz());
  klog("telemetry: {\"cpu_count\":%u,\"pmm_total_pages\":%lu,\"pmm_free_pages\":%lu,\"kheap_pages\":%lu,\"kheap_bytes\":%lu,\"arena_active\":%lu,\"arena_committed_pages\":%lu,\"virtio_block_sectors\":%lu,\"ai_cell_transitions\":%lu}\n",
       smp_online_count(), pmm_total_pages(), pmm_free_pages(),
       kheap_pages_allocated(), kheap_bytes_allocated(),
       arena_active_count(), arena_committed_pages(),
       virtio_block_capacity_sectors(), ai_cell_transition_count());
}
