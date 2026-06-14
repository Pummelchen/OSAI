#include <osai/ai_cell.h>
#include <osai/arena.h>
#include <osai/core_lease.h>
#include <osai/cpu_ai_runtime.h>
#include <osai/git_workspace.h>
#include <osai/kheap.h>
#include <osai/mutable_fs.h>
#include <osai/klog.h>
#include <osai/network_stack.h>
#include <osai/persistence.h>
#include <osai/pmm.h>
#include <osai/sandbox.h>
#include <osai/security.h>
#include <osai/service.h>
#include <osai/smp.h>
#include <osai/source_index.h>
#include <osai/syscall.h>
#include <osai/telemetry.h>
#include <osai/timer.h>
#include <osai/user.h>
#include <osai/virtio_blk.h>

void telemetry_emit_boot_summary(void) {
  klog(
      "telemetry: boot_summary cpu_online=%u pmm_total=%lu pmm_free=%lu timer_hz=%lu\n",
      smp_online_count(), pmm_total_pages(), pmm_free_pages(),
      timer_frequency_hz());
  klog(
      "telemetry: {\"cpu_count\":%u,\"pmm_total_pages\":%lu,\"pmm_free_pages\":%lu,\"kheap_pages\":%lu,\"kheap_bytes\":%lu,\"arena_active\":%lu,\"arena_committed_pages\":%lu,\"sandbox_active\":%lu,\"sandbox_transitions\":%lu,\"persistence_snapshots\":%lu,\"persistence_rollbacks\":%lu,\"persistence_rejects\":%lu,\"persistence_disk_writes\":%lu,\"persistence_disk_loads\":%lu,\"persistence_boot_loads\":%lu,\"persistence_checksum_errors\":%lu,\"mutable_fs_mounts\":%lu,\"mutable_fs_formats\":%lu,\"mutable_fs_boot_loads\":%lu,\"mutable_fs_files\":%lu,\"mutable_fs_directories\":%lu,\"mutable_fs_writes\":%lu,\"mutable_fs_reads\":%lu,\"mutable_fs_deletes\":%lu,\"mutable_fs_commits\":%lu,\"mutable_fs_rollbacks\":%lu,\"mutable_fs_replays\":%lu,\"mutable_fs_journal_writes\":%lu,\"mutable_fs_allocations\":%lu,\"mutable_fs_frees\":%lu,\"mutable_fs_multi_sector_files\":%lu,\"mutable_fs_state_records\":%lu,\"mutable_fs_rejects\":%lu,\"mutable_fs_checksum_errors\":%lu,\"hot_core_mask\":%u,\"irq_isolated_mask\":%u,\"migration_total\":%lu,\"context_switch_total\":%lu,\"source_index_active\":%lu,\"source_index_files\":%lu,\"source_index_symbols\":%lu,\"source_index_updates\":%lu,\"security_denied_ops\":%lu,\"security_capability_denials\":%lu,\"security_fs_denials\":%lu,\"security_workspace_denials\":%lu,\"security_sandbox_denials\":%lu,\"security_rollback_denials\":%lu,\"security_update_policy_rejects\":%lu,\"security_credential_rejects\":%lu,\"security_signature_accepts\":%lu,\"security_signature_rejects\":%lu,\"virtio_block_sectors\":%lu,\"ai_cell_transitions\":%lu,\"cpu_ai_model_loads\":%lu,\"cpu_ai_model_load_failures\":%lu,\"cpu_ai_tokenizer_calls\":%lu,\"cpu_ai_runtime_calls\":%lu,\"cpu_ai_kv_writes\":%lu,\"cpu_ai_shared_weight_binds\":%lu,\"cpu_ai_gpu_rejects\":%lu,\"git_workspace_active\":%lu,\"git_workspace_syncs\":%lu,\"git_workspace_applies\":%lu,\"git_workspace_reverts\":%lu,\"git_workspace_conflicts\":%lu,\"network_udp_tx\":%lu,\"network_udp_rx\":%lu,\"network_udp_malformed\":%lu,\"network_udp_dropped\":%lu,\"network_udp_flows\":%lu,\"network_udp_flow_hits\":%lu,\"network_udp_expired\":%lu,\"network_tcp_connections\":%lu,\"network_tcp_handshakes\":%lu,\"network_tcp_resets\":%lu,\"network_tcp_timeouts\":%lu,\"network_tcp_retransmits\":%lu,\"network_tcp_established\":%lu,\"network_tcp_closed\":%lu,\"network_queue_bindings\":%lu,\"network_rx_packets\":%lu,\"network_tx_packets\":%lu,\"network_packet_drops\":%lu,\"network_packet_lifecycle\":%lu,\"network_queue_rx_enqueues\":%lu,\"network_queue_tx_enqueues\":%lu,\"network_queue_completions\":%lu,\"network_queue_backpressure_drops\":%lu,\"network_flow_core_mismatches\":%lu,\"network_udp_p50\":%lu,\"network_udp_p95\":%lu,\"network_udp_p99\":%lu,\"network_udp_p999\":%lu,\"network_tcp_p50\":%lu,\"network_tcp_p95\":%lu,\"network_tcp_p99\":%lu,\"network_tcp_p999\":%lu,\"service_child_descriptors\":%lu,\"service_tree_edges\":%lu,\"service_transitions\":%lu,\"service_restarts\":%lu,\"service_crashes\":%lu,\"service_cleanups\":%lu,\"service_log_records\":%lu,\"control_plane_syscalls\":%lu,\"control_plane_denials\":%lu,\"service_descriptor_reads\":%lu,\"user_process_transitions\":%lu,\"user_process_loaded\":%lu,\"user_process_running\":%lu,\"user_process_exited\":%lu,\"user_process_failed\":%lu,\"user_process_reclaims\":%lu}\n",
      smp_online_count(), pmm_total_pages(), pmm_free_pages(),
      kheap_pages_allocated(), kheap_bytes_allocated(),
      arena_active_count(), arena_committed_pages(),
      sandbox_active_count(), sandbox_transition_count(),
      persistence_snapshot_count(), persistence_rollback_count(),
      persistence_reject_count(), persistence_disk_write_count(),
      persistence_disk_load_count(), persistence_disk_boot_load_count(),
      persistence_checksum_error_count(),
      mutable_fs_mount_count(), mutable_fs_format_count(),
      mutable_fs_boot_load_count(), mutable_fs_file_count(),
      mutable_fs_directory_count(), mutable_fs_write_count(),
      mutable_fs_read_count(), mutable_fs_delete_count(),
      mutable_fs_commit_count(), mutable_fs_rollback_count(),
      mutable_fs_replay_count(), mutable_fs_journal_write_count(),
      mutable_fs_allocation_count(), mutable_fs_free_count(),
      mutable_fs_multi_sector_file_count(), mutable_fs_state_record_count(),
      mutable_fs_reject_count(), mutable_fs_checksum_error_count(),
      smp_hot_core_mask(), smp_irq_isolated_mask(),
      core_lease_migration_count(),
      core_lease_involuntary_context_switch_count(),
      source_index_active_count(), source_index_total_file_records(),
      source_index_total_symbol_records(), source_index_total_updates(),
      security_denied_operation_count(), security_capability_denial_count(),
      security_fs_denial_count(), security_workspace_denial_count(),
      security_sandbox_denial_count(), security_rollback_denial_count(),
      security_update_policy_reject_count(), security_credential_reject_count(),
      security_signature_accept_count(), security_signature_reject_count(),
      virtio_block_capacity_sectors(), ai_cell_transition_count(),
      cpu_ai_runtime_model_load_count(),
      cpu_ai_runtime_model_load_failure_count(),
      cpu_ai_runtime_tokenizer_call_count(),
      cpu_ai_runtime_runtime_call_count(),
      cpu_ai_runtime_kv_write_count(),
      cpu_ai_runtime_shared_weight_bind_count(),
      cpu_ai_runtime_gpu_reject_count(),
      git_workspace_active_count(), git_workspace_sync_count(),
      git_workspace_apply_count(), git_workspace_revert_count(),
      git_workspace_conflict_count(),
      network_stack_udp_tx_count(), network_stack_udp_rx_count(),
      network_stack_udp_malformed_count(), network_stack_udp_dropped_count(),
      network_stack_udp_flow_count(), network_stack_udp_flow_hit_count(),
      network_stack_udp_expired_count(),
      network_stack_tcp_connections(), network_stack_tcp_handshake_count(),
      network_stack_tcp_reset_count(), network_stack_tcp_timeout_count(),
      network_stack_tcp_retransmit_count(),
      network_stack_tcp_established_count(), network_stack_tcp_closed_count(),
      network_stack_queue_bindings(), network_stack_rx_packet_count(),
      network_stack_tx_packet_count(), network_stack_packet_drop_count(),
      network_stack_packet_lifecycle_count(),
      network_stack_queue_rx_enqueue_count(),
      network_stack_queue_tx_enqueue_count(),
      network_stack_queue_completion_count(),
      network_stack_queue_backpressure_drop_count(),
      network_stack_flow_core_mismatch_count(),
      network_stack_udp_latency_p50_ns(), network_stack_udp_latency_p95_ns(),
      network_stack_udp_latency_p99_ns(), network_stack_udp_latency_p999_ns(),
      network_stack_tcp_latency_p50_ns(), network_stack_tcp_latency_p95_ns(),
      network_stack_tcp_latency_p99_ns(), network_stack_tcp_latency_p999_ns(),
      service_child_descriptor_count(), service_tree_edge_count(),
      service_transition_count(), service_restart_count(),
      service_crash_count(), service_cleanup_count(),
      service_log_record_count(),
      syscall_control_plane_count(), syscall_control_plane_denial_count(),
      syscall_service_descriptor_read_count(),
      user_process_transition_count(), user_process_loaded_count(),
      user_process_running_count(), user_process_exited_count(),
      user_process_failed_count(), user_process_reclaim_count());
}
