#ifndef OSAI_NETWORK_STACK_H
#define OSAI_NETWORK_STACK_H

#include <osai/status.h>
#include <osai/types.h>

#define OSAI_NETWORK_MAX_QUEUE_BINDINGS 4U
#define OSAI_NETWORK_QUEUE_ID_INVALID UINT32_C(0xffffffff)
#define OSAI_NETWORK_PROTOCOL_UDP UINT64_C(17)
#define OSAI_NETWORK_PROTOCOL_TCP UINT64_C(6)

typedef enum osai_network_flow_state {
  OSAI_NETWORK_FLOW_FREE = 0,
  OSAI_NETWORK_FLOW_SYN_RECV = 1,
  OSAI_NETWORK_FLOW_ESTABLISHED = 2,
  OSAI_NETWORK_FLOW_CLOSED = 3,
} osai_network_flow_state_t;

void network_stack_init(void);
osai_status_t network_stack_bind_queue(uint32_t cell_id, uint32_t queue_id,
                                       uint32_t core_mask);
osai_status_t network_stack_release_queue(uint32_t queue_id, uint32_t cell_id);
osai_status_t network_stack_process_udp_frame(const uint8_t *frame,
                                            uint64_t frame_len);
osai_status_t network_stack_process_tcp_frame(const uint8_t *frame,
                                            uint64_t frame_len);
osai_status_t network_stack_app_udp_echo(const uint8_t *payload,
                                         uint64_t payload_len,
                                         uint64_t *echoed_bytes);
osai_status_t network_stack_app_tcp_connect(uint64_t *round_trips);
osai_status_t network_stack_external_session(uint64_t protocol, uint64_t port,
                                             const uint8_t *payload,
                                             uint64_t payload_len,
                                             char *output,
                                             uint64_t output_capacity,
                                             uint64_t *output_bytes);
uint64_t network_stack_expire_udp_flows(uint64_t now_ns);
uint64_t network_stack_retransmit_tcp_flows(uint64_t now_ns);
uint64_t network_stack_expire_tcp_flows(uint64_t now_ns);

uint64_t network_stack_udp_tx_count(void);
uint64_t network_stack_udp_rx_count(void);
uint64_t network_stack_udp_malformed_count(void);
uint64_t network_stack_udp_dropped_count(void);
uint64_t network_stack_udp_flow_count(void);
uint64_t network_stack_udp_flow_hit_count(void);
uint64_t network_stack_udp_expired_count(void);
uint64_t network_stack_tcp_connections(void);
uint64_t network_stack_tcp_handshake_count(void);
uint64_t network_stack_tcp_reset_count(void);
uint64_t network_stack_tcp_timeout_count(void);
uint64_t network_stack_tcp_retransmit_count(void);
uint64_t network_stack_tcp_established_count(void);
uint64_t network_stack_tcp_closed_count(void);
uint64_t network_stack_queue_bindings(void);
uint64_t network_stack_rx_packet_count(void);
uint64_t network_stack_tx_packet_count(void);
uint64_t network_stack_packet_drop_count(void);
uint64_t network_stack_packet_lifecycle_count(void);
uint64_t network_stack_queue_rx_enqueue_count(void);
uint64_t network_stack_queue_tx_enqueue_count(void);
uint64_t network_stack_queue_completion_count(void);
uint64_t network_stack_queue_backpressure_drop_count(void);
uint64_t network_stack_flow_core_mismatch_count(void);
uint64_t network_stack_udp_latency_p50_ns(void);
uint64_t network_stack_udp_latency_p95_ns(void);
uint64_t network_stack_udp_latency_p99_ns(void);
uint64_t network_stack_udp_latency_p999_ns(void);
uint64_t network_stack_tcp_latency_p50_ns(void);
uint64_t network_stack_tcp_latency_p95_ns(void);
uint64_t network_stack_tcp_latency_p99_ns(void);
uint64_t network_stack_tcp_latency_p999_ns(void);
void network_stack_self_test(void);

/* Persistent network mode: real TX/RX via VirtIO-net */
void network_init_persistent(void);
void network_poll_tick(void);
uint64_t network_poll_tick_count(void);
uint64_t network_icmp_reply_count(void);
uint64_t network_arp_reply_sent_count(void);

#endif
