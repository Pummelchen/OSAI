#include <osai/ai_cell.h>
#include <osai/arena.h>
#include <osai/core_lease.h>
#include <osai/git_workspace.h>
#include <osai/kheap.h>
#include <osai/klog.h>
#include <osai/network_stack.h>
#include <osai/persistence.h>
#include <osai/pmm.h>
#include <osai/sandbox.h>
#include <osai/security.h>
#include <osai/smp.h>
#include <osai/source_index.h>
#include <osai/telemetry.h>
#include <osai/timer.h>
#include <osai/virtio_blk.h>

void telemetry_emit_boot_summary(void) {
  klog(
      "telemetry: boot_summary cpu_online=%u pmm_total=%lu pmm_free=%lu timer_hz=%lu\n",
      smp_online_count(), pmm_total_pages(), pmm_free_pages(),
      timer_frequency_hz());
  klog(
      "telemetry: {\"cpu_count\":%u,\"pmm_total_pages\":%lu,\"pmm_free_pages\":%lu,\"kheap_pages\":%lu,\"kheap_bytes\":%lu,\"arena_active\":%lu,\"arena_committed_pages\":%lu,\"sandbox_active\":%lu,\"sandbox_transitions\":%lu,\"persistence_snapshots\":%lu,\"persistence_rollbacks\":%lu,\"persistence_rejects\":%lu,\"hot_core_mask\":%u,\"irq_isolated_mask\":%u,\"migration_total\":%lu,\"context_switch_total\":%lu,\"source_index_active\":%lu,\"source_index_files\":%lu,\"source_index_symbols\":%lu,\"source_index_updates\":%lu,\"security_denied_ops\":%lu,\"security_credential_rejects\":%lu,\"security_signature_accepts\":%lu,\"security_signature_rejects\":%lu,\"virtio_block_sectors\":%lu,\"ai_cell_transitions\":%lu,\"git_workspace_active\":%lu,\"git_workspace_syncs\":%lu,\"git_workspace_applies\":%lu,\"git_workspace_reverts\":%lu,\"git_workspace_conflicts\":%lu,\"network_udp_tx\":%lu,\"network_udp_rx\":%lu,\"network_udp_malformed\":%lu,\"network_udp_dropped\":%lu,\"network_tcp_connections\":%lu,\"network_tcp_handshakes\":%lu,\"network_tcp_resets\":%lu,\"network_queue_bindings\":%lu,\"network_udp_p50\":%lu,\"network_udp_p95\":%lu,\"network_udp_p99\":%lu,\"network_tcp_p50\":%lu,\"network_tcp_p95\":%lu,\"network_tcp_p99\":%lu}\n",
      smp_online_count(), pmm_total_pages(), pmm_free_pages(),
      kheap_pages_allocated(), kheap_bytes_allocated(),
      arena_active_count(), arena_committed_pages(),
      sandbox_active_count(), sandbox_transition_count(),
      persistence_snapshot_count(), persistence_rollback_count(),
      persistence_reject_count(),
      smp_hot_core_mask(), smp_irq_isolated_mask(),
      core_lease_migration_count(),
      core_lease_involuntary_context_switch_count(),
      source_index_active_count(), source_index_total_file_records(),
      source_index_total_symbol_records(), source_index_total_updates(),
      security_denied_operation_count(), security_credential_reject_count(),
      security_signature_accept_count(), security_signature_reject_count(),
      virtio_block_capacity_sectors(), ai_cell_transition_count(),
      git_workspace_active_count(), git_workspace_sync_count(),
      git_workspace_apply_count(), git_workspace_revert_count(),
      git_workspace_conflict_count(),
      network_stack_udp_tx_count(), network_stack_udp_rx_count(),
      network_stack_udp_malformed_count(), network_stack_udp_dropped_count(),
      network_stack_tcp_connections(), network_stack_tcp_handshake_count(),
      network_stack_tcp_reset_count(), network_stack_queue_bindings(),
      network_stack_udp_latency_p50_ns(), network_stack_udp_latency_p95_ns(),
      network_stack_udp_latency_p99_ns(), network_stack_tcp_latency_p50_ns(),
      network_stack_tcp_latency_p95_ns(), network_stack_tcp_latency_p99_ns());
}
