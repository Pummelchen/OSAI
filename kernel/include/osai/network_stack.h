#ifndef OSAI_NETWORK_STACK_H
#define OSAI_NETWORK_STACK_H

#include <osai/status.h>
#include <osai/types.h>

#define OSAI_NETWORK_MAX_QUEUE_BINDINGS 4U
#define OSAI_NETWORK_QUEUE_ID_INVALID UINT32_C(0xffffffff)

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

uint64_t network_stack_udp_tx_count(void);
uint64_t network_stack_udp_rx_count(void);
uint64_t network_stack_udp_malformed_count(void);
uint64_t network_stack_udp_dropped_count(void);
uint64_t network_stack_tcp_connections(void);
uint64_t network_stack_tcp_handshake_count(void);
uint64_t network_stack_tcp_reset_count(void);
uint64_t network_stack_queue_bindings(void);
uint64_t network_stack_udp_latency_p50_ns(void);
uint64_t network_stack_udp_latency_p95_ns(void);
uint64_t network_stack_udp_latency_p99_ns(void);
uint64_t network_stack_tcp_latency_p50_ns(void);
uint64_t network_stack_tcp_latency_p95_ns(void);
uint64_t network_stack_tcp_latency_p99_ns(void);
void network_stack_self_test(void);

#endif
