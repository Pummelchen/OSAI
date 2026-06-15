#include <osai/assert.h>
#include <osai/klog.h>
#include <osai/network_stack.h>
#include <osai/timer.h>

#define NETWORK_ETHERTYPE_IPV4 UINT16_C(0x0800)
#define NETWORK_IP_PROTO_UDP UINT8_C(17)
#define NETWORK_IP_PROTO_TCP UINT8_C(6)

#define NETWORK_BUFFER_SIZE 1520U
#define NETWORK_MAX_SAMPLES 64U

#define NETWORK_TCP_CONNECTIONS 16U
#define NETWORK_UDP_FLOWS 16U
#define NETWORK_PACKET_DESCRIPTORS 16U
#define NETWORK_QUEUE_RING_SIZE 8U
#define NETWORK_UDP_IDLE_TIMEOUT_NS UINT64_C(2000000)
#define NETWORK_TCP_RETRANSMIT_NS UINT64_C(500000)
#define NETWORK_TCP_SYN_TIMEOUT_NS UINT64_C(1000000)
#define NETWORK_TCP_MAX_RETRANSMITS 2U

#define NETWORK_TCP_FLAG_FIN 0x01U
#define NETWORK_TCP_FLAG_SYN 0x02U
#define NETWORK_TCP_FLAG_RST 0x04U
#define NETWORK_TCP_FLAG_ACK 0x10U

typedef struct network_queue_binding {
  uint32_t queue_id;
  uint32_t cell_id;
  uint32_t core_mask;
  uint32_t in_use;
} network_queue_binding_t;

typedef struct network_queue_ring {
  uint32_t queue_id;
  uint32_t rx_depth;
  uint32_t tx_depth;
  uint64_t completed;
  uint64_t drops;
} network_queue_ring_t;

typedef enum network_packet_state {
  NETWORK_PACKET_FREE = 0,
  NETWORK_PACKET_RX_OWNED = 1,
  NETWORK_PACKET_TX_QUEUED = 2,
  NETWORK_PACKET_COMPLETE = 3,
  NETWORK_PACKET_DROPPED = 4,
} network_packet_state_t;

typedef struct network_packet_desc {
  network_packet_state_t state;
  uint32_t queue_id;
  uint32_t cell_id;
  uint16_t src_port;
  uint16_t dst_port;
  uint32_t src_address;
  uint32_t dst_address;
  uint64_t length;
  uint64_t created_ns;
} network_packet_desc_t;

typedef struct network_udp_flow {
  uint8_t active;
  uint32_t flow_id;
  uint32_t queue_id;
  uint32_t cell_id;
  uint16_t local_port;
  uint16_t remote_port;
  uint32_t local_address;
  uint32_t remote_address;
  uint64_t packets_rx;
  uint64_t packets_tx;
  uint64_t last_seen_ns;
} network_udp_flow_t;

typedef struct network_tcp_flow {
  osai_network_flow_state_t state;
  uint32_t flow_id;
  uint32_t queue_id;
  uint32_t cell_id;
  uint16_t local_port;
  uint16_t remote_port;
  uint32_t remote_address;
  uint32_t local_address;
  uint32_t remote_seq;
  uint32_t local_seq;
  uint64_t last_seen_ns;
  uint32_t retransmits;
  uint64_t packets_rx;
  uint64_t packets_tx;
} network_tcp_flow_t;

typedef struct network_ip4_header {
  uint8_t version_ihl;
  uint8_t tos;
  uint16_t total_length;
  uint16_t id;
  uint16_t flags_fragment_offset;
  uint8_t ttl;
  uint8_t protocol;
  uint16_t checksum;
  uint32_t source;
  uint32_t destination;
} network_ip4_header_t;

typedef struct network_udp_header {
  uint16_t source_port;
  uint16_t dest_port;
  uint16_t length;
  uint16_t checksum;
} network_udp_header_t;

typedef struct network_tcp_header {
  uint16_t source_port;
  uint16_t dest_port;
  uint32_t seq;
  uint32_t ack;
  uint8_t data_offset_reserved;
  uint8_t flags;
  uint16_t window_size;
  uint16_t checksum;
  uint16_t urgent_pointer;
} network_tcp_header_t;

static network_queue_binding_t g_queue_bindings[OSAI_NETWORK_MAX_QUEUE_BINDINGS];
static network_queue_ring_t g_queue_rings[OSAI_NETWORK_MAX_QUEUE_BINDINGS];
static uint64_t g_next_flow_id = 1U;
static network_packet_desc_t g_packet_descs[NETWORK_PACKET_DESCRIPTORS];
static network_udp_flow_t g_udp_flows[NETWORK_UDP_FLOWS];
static network_tcp_flow_t g_tcp_flows[NETWORK_TCP_CONNECTIONS];

static uint64_t g_udp_tx_count;
static uint64_t g_udp_rx_count;
static uint64_t g_udp_malformed_count;
static uint64_t g_udp_dropped_count;
static uint64_t g_udp_flow_hit_count;
static uint64_t g_udp_expired_count;
static uint64_t g_tcp_handshake_count;
static uint64_t g_tcp_reset_count;
static uint64_t g_tcp_timeout_count;
static uint64_t g_tcp_retransmit_count;
static uint64_t g_tcp_established_count;
static uint64_t g_tcp_closed_count;
static uint64_t g_queue_binding_count;
static uint64_t g_rx_packet_count;
static uint64_t g_tx_packet_count;
static uint64_t g_packet_drop_count;
static uint64_t g_packet_lifecycle_count;
static uint64_t g_queue_rx_enqueue_count;
static uint64_t g_queue_tx_enqueue_count;
static uint64_t g_queue_completion_count;
static uint64_t g_queue_backpressure_drop_count;
static uint64_t g_flow_core_mismatch_count;

static uint64_t g_udp_latency_samples[NETWORK_MAX_SAMPLES];
static uint64_t g_tcp_latency_samples[NETWORK_MAX_SAMPLES];
static uint32_t g_udp_latency_count;
static uint32_t g_tcp_latency_count;

static uint16_t read_u16_be(const uint8_t *bytes) {
  return (uint16_t)(((uint16_t)bytes[0] << 8U) | (uint16_t)bytes[1]);
}

static uint32_t read_u32_be(const uint8_t *bytes) {
  return ((uint32_t)bytes[0] << 24U) | ((uint32_t)bytes[1] << 16U) |
         ((uint32_t)bytes[2] << 8U) | (uint32_t)bytes[3];
}

static uint16_t checksum_u16(const uint8_t *data, uint32_t length) {
  uint64_t sum = 0;
  for (uint32_t i = 0; i + 1U < length; i += 2U) {
    sum += ((uint64_t)data[i] << 8U) | (uint64_t)data[i + 1U];
  }
  if ((length & 1U) != 0U) {
    sum += ((uint64_t)data[length - 1U] << 8U);
  }
  while ((sum >> 16U) != 0U) {
    sum = (sum & 0xFFFFU) + (sum >> 16U);
  }
  return (uint16_t)(~sum & 0xFFFFU);
}

static uint64_t percentile(uint64_t *samples, uint32_t count, uint32_t p) {
  if (count == 0U) {
    return 0;
  }
  uint64_t sorted[NETWORK_MAX_SAMPLES];
  for (uint32_t i = 0; i < count; ++i) {
    sorted[i] = samples[i];
  }

  for (uint32_t i = 0; i < count; ++i) {
    for (uint32_t j = i + 1U; j < count; ++j) {
      if (sorted[j] < sorted[i]) {
        uint64_t tmp = sorted[i];
        sorted[i] = sorted[j];
        sorted[j] = tmp;
      }
    }
  }

  uint32_t divisor = (p > 100U) ? 1000U : 100U;
  uint32_t index = (count * p) / divisor;
  if (index >= count) {
    index = count - 1U;
  }

  return sorted[index];
}

static void bytes_zero(void *buffer, uint64_t size) {
  uint8_t *bytes = (uint8_t *)buffer;
  for (uint64_t i = 0; i < size; ++i) {
    bytes[i] = 0;
  }
}

static void record_latency(uint64_t *samples, uint32_t *count, uint64_t value) {
  if (*count < NETWORK_MAX_SAMPLES) {
    samples[*count] = value;
    ++(*count);
  }
}

static network_queue_binding_t *find_binding(uint32_t queue_id) {
  for (uint32_t i = 0; i < OSAI_NETWORK_MAX_QUEUE_BINDINGS; ++i) {
    if (g_queue_bindings[i].in_use != 0 &&
        g_queue_bindings[i].queue_id == queue_id) {
      return &g_queue_bindings[i];
    }
  }
  return 0;
}

static network_queue_ring_t *find_queue_ring(uint32_t queue_id) {
  for (uint32_t i = 0; i < OSAI_NETWORK_MAX_QUEUE_BINDINGS; ++i) {
    if (g_queue_rings[i].queue_id == queue_id) {
      return &g_queue_rings[i];
    }
  }
  return 0;
}

static uint32_t active_binding_count(void) {
  uint32_t active = 0;
  for (uint32_t i = 0; i < OSAI_NETWORK_MAX_QUEUE_BINDINGS; ++i) {
    if (g_queue_bindings[i].in_use != 0) {
      ++active;
    }
  }
  return active;
}

static network_queue_binding_t *binding_by_active_index(uint32_t index) {
  for (uint32_t i = 0; i < OSAI_NETWORK_MAX_QUEUE_BINDINGS; ++i) {
    if (g_queue_bindings[i].in_use != 0) {
      if (index == 0U) {
        return &g_queue_bindings[i];
      }
      --index;
    }
  }
  return 0;
}

static network_queue_binding_t *select_binding_for_flow(uint16_t local_port,
                                                        uint16_t remote_port,
                                                        uint32_t local_address,
                                                        uint32_t remote_address) {
  uint32_t active = active_binding_count();
  if (active == 0U) {
    return 0;
  }
  uint32_t hash = (uint32_t)local_port ^ ((uint32_t)remote_port << 3U) ^
                  local_address ^ (remote_address >> 8U);
  return binding_by_active_index(hash % active);
}

static void queue_ring_reset(uint32_t queue_id) {
  network_queue_ring_t *ring = find_queue_ring(queue_id);
  if (ring == 0) {
    return;
  }
  ring->rx_depth = 0;
  ring->tx_depth = 0;
  ring->completed = 0;
  ring->drops = 0;
}

static int queue_ring_rx_enqueue(uint32_t queue_id) {
  network_queue_ring_t *ring = find_queue_ring(queue_id);
  if (ring == 0 || ring->rx_depth >= NETWORK_QUEUE_RING_SIZE) {
    ++g_queue_backpressure_drop_count;
    if (ring != 0) {
      ++ring->drops;
    }
    return 0;
  }
  ++ring->rx_depth;
  ++g_queue_rx_enqueue_count;
  return 1;
}

static void queue_ring_rx_complete(uint32_t queue_id) {
  network_queue_ring_t *ring = find_queue_ring(queue_id);
  if (ring != 0 && ring->rx_depth > 0U) {
    --ring->rx_depth;
  }
}

static int queue_ring_tx_enqueue(uint32_t queue_id) {
  network_queue_ring_t *ring = find_queue_ring(queue_id);
  if (ring == 0 || ring->tx_depth >= NETWORK_QUEUE_RING_SIZE) {
    ++g_queue_backpressure_drop_count;
    if (ring != 0) {
      ++ring->drops;
    }
    return 0;
  }
  ++ring->tx_depth;
  ++g_queue_tx_enqueue_count;
  return 1;
}

static void queue_ring_tx_complete(uint32_t queue_id) {
  network_queue_ring_t *ring = find_queue_ring(queue_id);
  if (ring != 0) {
    if (ring->tx_depth > 0U) {
      --ring->tx_depth;
    }
    ++ring->completed;
    ++g_queue_completion_count;
  }
}

static network_packet_desc_t *alloc_packet_desc(uint32_t queue_id,
                                                uint64_t length,
                                                uint64_t now_ns) {
  network_queue_binding_t *binding = find_binding(queue_id);
  if (binding == 0 || length == 0 || length > NETWORK_BUFFER_SIZE) {
    ++g_packet_drop_count;
    return 0;
  }
  if (queue_ring_rx_enqueue(queue_id) == 0) {
    ++g_packet_drop_count;
    return 0;
  }

  for (uint32_t i = 0; i < NETWORK_PACKET_DESCRIPTORS; ++i) {
    if (g_packet_descs[i].state == NETWORK_PACKET_FREE ||
        g_packet_descs[i].state == NETWORK_PACKET_COMPLETE ||
        g_packet_descs[i].state == NETWORK_PACKET_DROPPED) {
      g_packet_descs[i].state = NETWORK_PACKET_RX_OWNED;
      g_packet_descs[i].queue_id = queue_id;
      g_packet_descs[i].cell_id = binding->cell_id;
      g_packet_descs[i].src_port = 0;
      g_packet_descs[i].dst_port = 0;
      g_packet_descs[i].src_address = 0;
      g_packet_descs[i].dst_address = 0;
      g_packet_descs[i].length = length;
      g_packet_descs[i].created_ns = now_ns;
      ++g_rx_packet_count;
      ++g_packet_lifecycle_count;
      return &g_packet_descs[i];
    }
  }

  queue_ring_rx_complete(queue_id);
  ++g_packet_drop_count;
  return 0;
}

static void packet_mark_dropped(network_packet_desc_t *packet);

static void packet_mark_tx(network_packet_desc_t *packet) {
  if (packet != 0 && packet->state == NETWORK_PACKET_RX_OWNED) {
    if (queue_ring_tx_enqueue(packet->queue_id) == 0) {
      packet_mark_dropped(packet);
      return;
    }
    queue_ring_rx_complete(packet->queue_id);
    packet->state = NETWORK_PACKET_TX_QUEUED;
    ++g_tx_packet_count;
    ++g_packet_lifecycle_count;
  }
}

static void packet_mark_complete(network_packet_desc_t *packet) {
  if (packet != 0 && packet->state == NETWORK_PACKET_TX_QUEUED) {
    queue_ring_tx_complete(packet->queue_id);
    packet->state = NETWORK_PACKET_COMPLETE;
    ++g_packet_lifecycle_count;
  }
}

static void packet_mark_dropped(network_packet_desc_t *packet) {
  if (packet != 0 && packet->state != NETWORK_PACKET_DROPPED) {
    if (packet->state == NETWORK_PACKET_RX_OWNED) {
      queue_ring_rx_complete(packet->queue_id);
    } else if (packet->state == NETWORK_PACKET_TX_QUEUED) {
      queue_ring_tx_complete(packet->queue_id);
    }
    packet->state = NETWORK_PACKET_DROPPED;
    ++g_packet_drop_count;
    ++g_packet_lifecycle_count;
  }
}

static uint32_t ip4_addr_host_order(uint32_t network_order_address) {
  const uint8_t *src = (const uint8_t *)&network_order_address;
  return (uint32_t)src[0] | ((uint32_t)src[1] << 8U) |
         ((uint32_t)src[2] << 16U) | ((uint32_t)src[3] << 24U);
}

static int eth_frame_has_ipv4(const uint8_t *frame, uint64_t frame_len) {
  const network_ip4_header_t *ip = (const network_ip4_header_t *)(frame + 14U);
  if (frame_len < (14U + 20U)) {
    return 0;
  }
  if (read_u16_be(frame + 12U) != NETWORK_ETHERTYPE_IPV4) {
    return 0;
  }
  if ((ip->version_ihl >> 4U) != 4U) {
    return 0;
  }
  return 1;
}

static int parse_udp(const uint8_t *frame, uint64_t frame_len,
                    uint16_t *src_port, uint16_t *dst_port,
                    uint16_t *payload_len, uint32_t *src_address,
                    uint32_t *dst_address) {
  if (!eth_frame_has_ipv4(frame, frame_len)) {
    return 0;
  }

  const network_ip4_header_t *ip = (const network_ip4_header_t *)(frame + 14U);
  const uint16_t ip_header_words = (uint16_t)(ip->version_ihl & 0x0fU);
  const uint64_t ip_len = (uint64_t)read_u16_be((const uint8_t *)&ip->total_length);
  const uint32_t ip_header_bytes = (uint32_t)ip_header_words * 4U;
  if (ip->protocol != NETWORK_IP_PROTO_UDP) {
    return 0;
  }
  if (ip_header_bytes < 20U || ip_len < ip_header_bytes) {
    return 0;
  }

  const network_udp_header_t *udp =
      (const network_udp_header_t *)((const uint8_t *)ip + ip_header_bytes);
  const uint64_t udp_start = 14U + (uint64_t)ip_header_bytes;
  const uint64_t udp_end = 14U + ip_len;
  if (udp_end > frame_len || ip_len < ip_header_bytes + 8U) {
    return 0;
  }

  const uint16_t udp_length = read_u16_be((const uint8_t *)&udp->length);
  if (udp_length < 8U || udp_start + 8U > udp_end || udp_start + (uint64_t)udp_length > udp_end) {
    return 0;
  }

  if ((ip->checksum != 0U) &&
      (checksum_u16((const uint8_t *)ip, ip_header_bytes) != 0U)) {
    return 0;
  }

  *src_port = read_u16_be((const uint8_t *)&udp->source_port);
  *dst_port = read_u16_be((const uint8_t *)&udp->dest_port);
  *payload_len = udp_length;
  *src_address = ip4_addr_host_order(ip->source);
  *dst_address = ip4_addr_host_order(ip->destination);
  return 1;
}

static int parse_tcp(const uint8_t *frame, uint64_t frame_len, uint16_t *src_port,
                    uint16_t *dst_port, uint32_t *seq, uint32_t *ack,
                    uint8_t *flags) {
  if (!eth_frame_has_ipv4(frame, frame_len)) {
    return 0;
  }

  const network_ip4_header_t *ip = (const network_ip4_header_t *)(frame + 14U);
  const uint16_t ip_header_words = (uint16_t)(ip->version_ihl & 0x0fU);
  const uint64_t ip_len = (uint64_t)read_u16_be((const uint8_t *)&ip->total_length);
  const uint64_t ip_header_bytes = (uint64_t)ip_header_words * 4U;
  if (ip->protocol != NETWORK_IP_PROTO_TCP) {
    return 0;
  }
  if (ip_header_bytes < 20U || ip_len < ip_header_bytes + 20U) {
    return 0;
  }
  if (14U + ip_len > frame_len) {
    return 0;
  }

  const network_tcp_header_t *tcp =
      (const network_tcp_header_t *)((const uint8_t *)ip + ip_header_bytes);
  const uint16_t data_offset_words = (uint16_t)(tcp->data_offset_reserved >> 4U);
  if (data_offset_words < 5U) {
    return 0;
  }
  const uint64_t tcp_header_bytes = (uint64_t)data_offset_words * 4U;
  const uint64_t tcp_payload_len =
      ip_len - ip_header_bytes - (uint64_t)tcp_header_bytes;
  const uint16_t tcp_len = (uint16_t)ip_len;

  (void)tcp_payload_len;
  (void)tcp_len;

  if (tcp_header_bytes > ip_len) {
    return 0;
  }

  if ((ip->checksum != 0U) &&
      (checksum_u16((const uint8_t *)ip, (uint16_t)ip_header_bytes) != 0U)) {
    return 0;
  }

  *src_port = read_u16_be((const uint8_t *)&tcp->source_port);
  *dst_port = read_u16_be((const uint8_t *)&tcp->dest_port);
  *seq = read_u32_be((const uint8_t *)&tcp->seq);
  *ack = read_u32_be((const uint8_t *)&tcp->ack);
  *flags = tcp->flags;
  return 1;
}

static network_tcp_flow_t *find_flow_by_ports(uint16_t local_port,
                                              uint16_t remote_port,
                                              uint32_t remote_address) {
  for (uint32_t i = 0; i < NETWORK_TCP_CONNECTIONS; ++i) {
    if (g_tcp_flows[i].state != OSAI_NETWORK_FLOW_FREE &&
        g_tcp_flows[i].local_port == local_port &&
        g_tcp_flows[i].remote_port == remote_port &&
        g_tcp_flows[i].remote_address == remote_address) {
      return &g_tcp_flows[i];
    }
  }
  return 0;
}

static network_udp_flow_t *find_udp_flow(uint16_t local_port,
                                         uint16_t remote_port,
                                         uint32_t local_address,
                                         uint32_t remote_address) {
  for (uint32_t i = 0; i < NETWORK_UDP_FLOWS; ++i) {
    if (g_udp_flows[i].active != 0 &&
        g_udp_flows[i].local_port == local_port &&
        g_udp_flows[i].remote_port == remote_port &&
        g_udp_flows[i].local_address == local_address &&
        g_udp_flows[i].remote_address == remote_address) {
      return &g_udp_flows[i];
    }
  }
  return 0;
}

static network_udp_flow_t *alloc_udp_flow(uint32_t queue_id, uint32_t cell_id,
                                          uint16_t local_port,
                                          uint16_t remote_port,
                                          uint32_t local_address,
                                          uint32_t remote_address,
                                          uint64_t now_ns) {
  network_udp_flow_t *flow = find_udp_flow(local_port, remote_port,
                                           local_address, remote_address);
  if (flow != 0) {
    ++g_udp_flow_hit_count;
    flow->last_seen_ns = now_ns;
    return flow;
  }
  for (uint32_t i = 0; i < NETWORK_UDP_FLOWS; ++i) {
    if (g_udp_flows[i].active == 0) {
      g_udp_flows[i].active = 1;
      g_udp_flows[i].flow_id = (uint32_t)(g_next_flow_id++);
      if (g_udp_flows[i].flow_id == 0U) {
        g_udp_flows[i].flow_id = 1U;
        g_next_flow_id = 2U;
      }
      g_udp_flows[i].queue_id = queue_id;
      g_udp_flows[i].cell_id = cell_id;
      g_udp_flows[i].local_port = local_port;
      g_udp_flows[i].remote_port = remote_port;
      g_udp_flows[i].local_address = local_address;
      g_udp_flows[i].remote_address = remote_address;
      g_udp_flows[i].packets_rx = 0;
      g_udp_flows[i].packets_tx = 0;
      g_udp_flows[i].last_seen_ns = now_ns;
      klog("network: udp flow id=%u queue=%u cell=%u local=%u remote=%u\n",
           g_udp_flows[i].flow_id, queue_id, cell_id, local_port, remote_port);
      return &g_udp_flows[i];
    }
  }
  return 0;
}

static network_tcp_flow_t *alloc_tcp_flow(void) {
  for (uint32_t i = 0; i < NETWORK_TCP_CONNECTIONS; ++i) {
    if (g_tcp_flows[i].state == OSAI_NETWORK_FLOW_FREE) {
      g_tcp_flows[i].state = OSAI_NETWORK_FLOW_SYN_RECV;
      g_tcp_flows[i].retransmits = 0;
      g_tcp_flows[i].packets_rx = 0;
      g_tcp_flows[i].packets_tx = 0;
      return &g_tcp_flows[i];
    }
  }
  return 0;
}

void network_stack_init(void) {
  for (uint32_t i = 0; i < OSAI_NETWORK_MAX_QUEUE_BINDINGS; ++i) {
    g_queue_bindings[i].cell_id = 0;
    g_queue_bindings[i].queue_id = OSAI_NETWORK_QUEUE_ID_INVALID;
    g_queue_bindings[i].core_mask = 0;
    g_queue_bindings[i].in_use = 0;
    g_queue_rings[i].queue_id = i;
    g_queue_rings[i].rx_depth = 0;
    g_queue_rings[i].tx_depth = 0;
    g_queue_rings[i].completed = 0;
    g_queue_rings[i].drops = 0;
  }

  for (uint32_t i = 0; i < NETWORK_TCP_CONNECTIONS; ++i) {
    g_tcp_flows[i].state = OSAI_NETWORK_FLOW_FREE;
    g_tcp_flows[i].flow_id = 0;
    g_tcp_flows[i].queue_id = OSAI_NETWORK_QUEUE_ID_INVALID;
    g_tcp_flows[i].cell_id = 0;
    g_tcp_flows[i].local_port = 0;
    g_tcp_flows[i].remote_port = 0;
    g_tcp_flows[i].remote_address = 0;
    g_tcp_flows[i].local_address = 0;
    g_tcp_flows[i].remote_seq = 0;
    g_tcp_flows[i].local_seq = 0;
    g_tcp_flows[i].last_seen_ns = 0;
    g_tcp_flows[i].retransmits = 0;
    g_tcp_flows[i].packets_rx = 0;
    g_tcp_flows[i].packets_tx = 0;
  }

  for (uint32_t i = 0; i < NETWORK_UDP_FLOWS; ++i) {
    g_udp_flows[i].active = 0;
    g_udp_flows[i].flow_id = 0;
    g_udp_flows[i].queue_id = OSAI_NETWORK_QUEUE_ID_INVALID;
    g_udp_flows[i].cell_id = 0;
    g_udp_flows[i].local_port = 0;
    g_udp_flows[i].remote_port = 0;
    g_udp_flows[i].local_address = 0;
    g_udp_flows[i].remote_address = 0;
    g_udp_flows[i].packets_rx = 0;
    g_udp_flows[i].packets_tx = 0;
    g_udp_flows[i].last_seen_ns = 0;
  }

  for (uint32_t i = 0; i < NETWORK_PACKET_DESCRIPTORS; ++i) {
    g_packet_descs[i].state = NETWORK_PACKET_FREE;
    g_packet_descs[i].queue_id = OSAI_NETWORK_QUEUE_ID_INVALID;
    g_packet_descs[i].cell_id = 0;
    g_packet_descs[i].src_port = 0;
    g_packet_descs[i].dst_port = 0;
    g_packet_descs[i].src_address = 0;
    g_packet_descs[i].dst_address = 0;
    g_packet_descs[i].length = 0;
    g_packet_descs[i].created_ns = 0;
  }

  g_next_flow_id = 1U;
  g_udp_tx_count = 0;
  g_udp_rx_count = 0;
  g_udp_malformed_count = 0;
  g_udp_dropped_count = 0;
  g_udp_flow_hit_count = 0;
  g_udp_expired_count = 0;
  g_tcp_handshake_count = 0;
  g_tcp_reset_count = 0;
  g_tcp_timeout_count = 0;
  g_tcp_retransmit_count = 0;
  g_tcp_established_count = 0;
  g_tcp_closed_count = 0;
  g_udp_latency_count = 0;
  g_tcp_latency_count = 0;
  g_queue_binding_count = 0;
  g_rx_packet_count = 0;
  g_tx_packet_count = 0;
  g_packet_drop_count = 0;
  g_packet_lifecycle_count = 0;
  g_queue_rx_enqueue_count = 0;
  g_queue_tx_enqueue_count = 0;
  g_queue_completion_count = 0;
  g_queue_backpressure_drop_count = 0;
  g_flow_core_mismatch_count = 0;

  for (uint32_t i = 0; i < NETWORK_MAX_SAMPLES; ++i) {
    g_udp_latency_samples[i] = 0;
    g_tcp_latency_samples[i] = 0;
  }

  klog("network: stack initialized\n");
}

osai_status_t network_stack_bind_queue(uint32_t cell_id, uint32_t queue_id,
                                       uint32_t core_mask) {
  if (queue_id >= OSAI_NETWORK_MAX_QUEUE_BINDINGS || core_mask == 0 ||
      cell_id == UINT32_C(0xffffffff)) {
    return OSAI_ERR_INVALID;
  }

  for (uint32_t i = 0; i < OSAI_NETWORK_MAX_QUEUE_BINDINGS; ++i) {
    if (g_queue_bindings[i].in_use != 0 &&
        g_queue_bindings[i].queue_id == queue_id) {
      return OSAI_ERR_BUSY;
    }
  }

  for (uint32_t i = 0; i < OSAI_NETWORK_MAX_QUEUE_BINDINGS; ++i) {
    if (g_queue_bindings[i].in_use == 0) {
      g_queue_bindings[i].in_use = 1;
      g_queue_bindings[i].cell_id = cell_id;
      g_queue_bindings[i].queue_id = queue_id;
      g_queue_bindings[i].core_mask = core_mask;
      queue_ring_reset(queue_id);
      ++g_queue_binding_count;
      klog("network: bound queue=%u cell=%u core_mask=0x%x\n", queue_id,
           cell_id, core_mask);
      return OSAI_OK;
    }
  }

  return OSAI_ERR_NO_MEMORY;
}

osai_status_t network_stack_release_queue(uint32_t queue_id, uint32_t cell_id) {
  for (uint32_t i = 0; i < OSAI_NETWORK_MAX_QUEUE_BINDINGS; ++i) {
    if (g_queue_bindings[i].in_use != 0 &&
        g_queue_bindings[i].queue_id == queue_id &&
        g_queue_bindings[i].cell_id == cell_id) {
      g_queue_bindings[i].in_use = 0;
      g_queue_bindings[i].cell_id = 0;
      g_queue_bindings[i].queue_id = OSAI_NETWORK_QUEUE_ID_INVALID;
      g_queue_bindings[i].core_mask = 0;
      queue_ring_reset(queue_id);
      g_queue_binding_count =
          (g_queue_binding_count == 0U) ? 0U : (g_queue_binding_count - 1U);
      klog("network: released queue=%u cell=%u\n", queue_id, cell_id);
      return OSAI_OK;
    }
  }
  return OSAI_ERR_NOT_FOUND;
}

osai_status_t network_stack_process_udp_frame(const uint8_t *frame,
                                            uint64_t frame_len) {
  if (frame == 0 || frame_len < 34U) {
    ++g_udp_dropped_count;
    ++g_udp_malformed_count;
    ++g_packet_drop_count;
    return OSAI_ERR_INVALID;
  }

  uint64_t start = timer_now_ns();
  uint16_t src_port = 0;
  uint16_t dst_port = 0;
  uint16_t payload_len = 0;
  uint32_t src_address = 0;
  uint32_t dst_address = 0;

  if (parse_udp(frame, frame_len, &src_port, &dst_port, &payload_len,
                &src_address, &dst_address) == 0) {
    ++g_udp_dropped_count;
    ++g_udp_malformed_count;
    ++g_packet_drop_count;
    return OSAI_ERR_INVALID;
  }

  if (src_port == 0 || dst_port == 0 || payload_len == 0) {
    ++g_udp_dropped_count;
    ++g_packet_drop_count;
    return OSAI_ERR_INVALID;
  }

  network_udp_flow_t *existing =
      find_udp_flow(dst_port, src_port, dst_address, src_address);
  network_queue_binding_t *binding =
      existing != 0 ? find_binding(existing->queue_id)
                    : select_binding_for_flow(dst_port, src_port, dst_address,
                                              src_address);
  if (binding == 0) {
    ++g_udp_dropped_count;
    ++g_packet_drop_count;
    return OSAI_ERR_NOT_FOUND;
  }

  network_packet_desc_t *packet =
      alloc_packet_desc(binding->queue_id, frame_len, start);
  if (packet == 0) {
    ++g_udp_dropped_count;
    return OSAI_ERR_NO_MEMORY;
  }

  packet->src_port = src_port;
  packet->dst_port = dst_port;
  packet->src_address = src_address;
  packet->dst_address = dst_address;
  network_udp_flow_t *flow =
      alloc_udp_flow(binding->queue_id, binding->cell_id, dst_port, src_port,
                     dst_address, src_address, start);
  if (flow == 0) {
    ++g_udp_dropped_count;
    packet_mark_dropped(packet);
    return OSAI_ERR_NO_MEMORY;
  }
  if (flow->queue_id != binding->queue_id || flow->cell_id != binding->cell_id) {
    ++g_flow_core_mismatch_count;
    packet_mark_dropped(packet);
    return OSAI_ERR_BUSY;
  }
  ++flow->packets_rx;
  ++g_udp_rx_count;
  packet_mark_tx(packet);
  ++flow->packets_tx;
  ++g_udp_tx_count;
  packet_mark_complete(packet);
  record_latency(g_udp_latency_samples, &g_udp_latency_count, timer_now_ns() - start);
  return OSAI_OK;
}

osai_status_t network_stack_process_tcp_frame(const uint8_t *frame,
                                            uint64_t frame_len) {
  if (frame == 0 || frame_len < 54U) {
    ++g_tcp_reset_count;
    ++g_packet_drop_count;
    return OSAI_ERR_INVALID;
  }

  uint64_t start = timer_now_ns();
  uint16_t src_port = 0;
  uint16_t dst_port = 0;
  uint32_t seq = 0;
  uint32_t ack = 0;
  uint8_t flags = 0;

  if (parse_tcp(frame, frame_len, &src_port, &dst_port, &seq, &ack, &flags) ==
      0) {
    ++g_tcp_reset_count;
    ++g_packet_drop_count;
    return OSAI_ERR_INVALID;
  }

  const network_ip4_header_t *ip =
      (const network_ip4_header_t *)(frame + 14U);
  uint32_t remote_address = ip4_addr_host_order(ip->source);
  uint32_t local_address = ip4_addr_host_order(ip->destination);

  network_tcp_flow_t *flow = find_flow_by_ports(dst_port, src_port, remote_address);
  network_queue_binding_t *binding =
      flow != 0 ? find_binding(flow->queue_id)
                : select_binding_for_flow(dst_port, src_port, local_address,
                                          remote_address);
  if (binding == 0) {
    ++g_tcp_reset_count;
    ++g_packet_drop_count;
    return OSAI_ERR_NOT_FOUND;
  }

  network_packet_desc_t *packet =
      alloc_packet_desc(binding->queue_id, frame_len, start);
  if (packet == 0) {
    ++g_tcp_reset_count;
    return OSAI_ERR_NO_MEMORY;
  }

  packet->src_port = src_port;
  packet->dst_port = dst_port;
  packet->src_address = remote_address;
  packet->dst_address = local_address;

  if ((flags & NETWORK_TCP_FLAG_RST) != 0U) {
    if (flow != 0) {
      flow->state = OSAI_NETWORK_FLOW_CLOSED;
      ++g_tcp_reset_count;
      ++g_tcp_closed_count;
    }
    packet_mark_dropped(packet);
    return OSAI_ERR_INVALID;
  }

  if (flow == 0 && (flags & NETWORK_TCP_FLAG_SYN) != 0U) {
    flow = alloc_tcp_flow();
    if (flow == 0) {
      ++g_tcp_reset_count;
      packet_mark_dropped(packet);
      return OSAI_ERR_NO_MEMORY;
    }
    flow->flow_id = (uint32_t)(g_next_flow_id++);
    if (flow->flow_id == 0U) {
      flow->flow_id = 1U;
      g_next_flow_id = 2U;
    }
    flow->local_port = dst_port;
    flow->remote_port = src_port;
    flow->queue_id = binding->queue_id;
    flow->cell_id = binding->cell_id;
    flow->remote_address = remote_address;
    flow->local_address = local_address;
    flow->remote_seq = seq;
    flow->local_seq = 0;
    flow->last_seen_ns = start;
    flow->state = OSAI_NETWORK_FLOW_SYN_RECV;
    flow->retransmits = 0;
    flow->packets_rx = 1;
    flow->packets_tx = 1;
    ++g_tcp_handshake_count;
    packet_mark_tx(packet);
    packet_mark_complete(packet);
    record_latency(g_tcp_latency_samples, &g_tcp_latency_count,
                  timer_now_ns() - start);
    return OSAI_OK;
  }

  if (flow != 0 && flow->state == OSAI_NETWORK_FLOW_SYN_RECV &&
      (flags & NETWORK_TCP_FLAG_ACK) != 0U) {
    flow->state = OSAI_NETWORK_FLOW_ESTABLISHED;
    flow->local_seq = ack;
    flow->last_seen_ns = start;
    ++flow->packets_rx;
    ++flow->packets_tx;
    ++g_tcp_handshake_count;
    ++g_tcp_established_count;
    packet_mark_tx(packet);
    packet_mark_complete(packet);
    record_latency(g_tcp_latency_samples, &g_tcp_latency_count,
                  timer_now_ns() - start);
    return OSAI_OK;
  }

  if (flow != 0 && flow->state == OSAI_NETWORK_FLOW_ESTABLISHED) {
    (void)ack;
    (void)seq;
    flow->last_seen_ns = start;
    ++flow->packets_rx;
    ++flow->packets_tx;
    ++g_tcp_handshake_count;
    packet_mark_tx(packet);
    packet_mark_complete(packet);
    record_latency(g_tcp_latency_samples, &g_tcp_latency_count,
                  timer_now_ns() - start);
    return OSAI_OK;
  }

  ++g_tcp_reset_count;
  packet_mark_dropped(packet);
  return OSAI_ERR_INVALID;
}

uint64_t network_stack_expire_udp_flows(uint64_t now_ns) {
  uint64_t expired = 0;
  for (uint32_t i = 0; i < NETWORK_UDP_FLOWS; ++i) {
    if (g_udp_flows[i].active != 0 &&
        now_ns > g_udp_flows[i].last_seen_ns &&
        now_ns - g_udp_flows[i].last_seen_ns >= NETWORK_UDP_IDLE_TIMEOUT_NS) {
      g_udp_flows[i].active = 0;
      ++g_udp_expired_count;
      ++expired;
      klog("network: udp flow id=%u expired queue=%u cell=%u rx=%lu tx=%lu\n",
           g_udp_flows[i].flow_id, g_udp_flows[i].queue_id,
           g_udp_flows[i].cell_id, g_udp_flows[i].packets_rx,
           g_udp_flows[i].packets_tx);
    }
  }
  return expired;
}

uint64_t network_stack_retransmit_tcp_flows(uint64_t now_ns) {
  uint64_t retransmitted = 0;
  for (uint32_t i = 0; i < NETWORK_TCP_CONNECTIONS; ++i) {
    if (g_tcp_flows[i].state == OSAI_NETWORK_FLOW_SYN_RECV &&
        now_ns > g_tcp_flows[i].last_seen_ns &&
        now_ns - g_tcp_flows[i].last_seen_ns >= NETWORK_TCP_RETRANSMIT_NS &&
        g_tcp_flows[i].retransmits < NETWORK_TCP_MAX_RETRANSMITS) {
      ++g_tcp_flows[i].retransmits;
      ++g_tcp_flows[i].packets_tx;
      g_tcp_flows[i].last_seen_ns = now_ns;
      ++g_tcp_retransmit_count;
      ++retransmitted;
      klog("network: tcp flow id=%u retransmit=%u queue=%u cell=%u\n",
           g_tcp_flows[i].flow_id, g_tcp_flows[i].retransmits,
           g_tcp_flows[i].queue_id, g_tcp_flows[i].cell_id);
    }
  }
  return retransmitted;
}

uint64_t network_stack_expire_tcp_flows(uint64_t now_ns) {
  uint64_t expired = 0;
  for (uint32_t i = 0; i < NETWORK_TCP_CONNECTIONS; ++i) {
    if (g_tcp_flows[i].state == OSAI_NETWORK_FLOW_SYN_RECV &&
        now_ns > g_tcp_flows[i].last_seen_ns &&
        now_ns - g_tcp_flows[i].last_seen_ns >= NETWORK_TCP_SYN_TIMEOUT_NS) {
      g_tcp_flows[i].state = OSAI_NETWORK_FLOW_CLOSED;
      ++g_tcp_timeout_count;
      ++g_tcp_closed_count;
      ++g_packet_drop_count;
      ++expired;
      klog("network: tcp flow id=%u timeout queue=%u cell=%u\n",
           g_tcp_flows[i].flow_id, g_tcp_flows[i].queue_id,
           g_tcp_flows[i].cell_id);
    }
  }
  return expired;
}

uint64_t network_stack_udp_tx_count(void) {
  return g_udp_tx_count;
}

uint64_t network_stack_udp_rx_count(void) {
  return g_udp_rx_count;
}

uint64_t network_stack_udp_malformed_count(void) {
  return g_udp_malformed_count;
}

uint64_t network_stack_udp_dropped_count(void) {
  return g_udp_dropped_count;
}

uint64_t network_stack_udp_flow_count(void) {
  uint64_t active = 0;
  for (uint32_t i = 0; i < NETWORK_UDP_FLOWS; ++i) {
    if (g_udp_flows[i].active != 0) {
      ++active;
    }
  }
  return active;
}

uint64_t network_stack_udp_flow_hit_count(void) {
  return g_udp_flow_hit_count;
}

uint64_t network_stack_udp_expired_count(void) {
  return g_udp_expired_count;
}

uint64_t network_stack_tcp_connections(void) {
  uint64_t active = 0;
  for (uint32_t i = 0; i < NETWORK_TCP_CONNECTIONS; ++i) {
    if (g_tcp_flows[i].state == OSAI_NETWORK_FLOW_ESTABLISHED) {
      ++active;
    }
  }
  return active;
}

uint64_t network_stack_tcp_handshake_count(void) {
  return g_tcp_handshake_count;
}

uint64_t network_stack_tcp_reset_count(void) {
  return g_tcp_reset_count;
}

uint64_t network_stack_tcp_timeout_count(void) {
  return g_tcp_timeout_count;
}

uint64_t network_stack_tcp_retransmit_count(void) {
  return g_tcp_retransmit_count;
}

uint64_t network_stack_tcp_established_count(void) {
  return g_tcp_established_count;
}

uint64_t network_stack_tcp_closed_count(void) {
  return g_tcp_closed_count;
}

uint64_t network_stack_queue_bindings(void) {
  return g_queue_binding_count;
}

uint64_t network_stack_rx_packet_count(void) {
  return g_rx_packet_count;
}

uint64_t network_stack_tx_packet_count(void) {
  return g_tx_packet_count;
}

uint64_t network_stack_packet_drop_count(void) {
  return g_packet_drop_count;
}

uint64_t network_stack_packet_lifecycle_count(void) {
  return g_packet_lifecycle_count;
}

uint64_t network_stack_queue_rx_enqueue_count(void) {
  return g_queue_rx_enqueue_count;
}

uint64_t network_stack_queue_tx_enqueue_count(void) {
  return g_queue_tx_enqueue_count;
}

uint64_t network_stack_queue_completion_count(void) {
  return g_queue_completion_count;
}

uint64_t network_stack_queue_backpressure_drop_count(void) {
  return g_queue_backpressure_drop_count;
}

uint64_t network_stack_flow_core_mismatch_count(void) {
  return g_flow_core_mismatch_count;
}

uint64_t network_stack_udp_latency_p50_ns(void) {
  return percentile(g_udp_latency_samples, g_udp_latency_count, 50U);
}

uint64_t network_stack_udp_latency_p95_ns(void) {
  return percentile(g_udp_latency_samples, g_udp_latency_count, 95U);
}

uint64_t network_stack_udp_latency_p99_ns(void) {
  return percentile(g_udp_latency_samples, g_udp_latency_count, 99U);
}

uint64_t network_stack_udp_latency_p999_ns(void) {
  return percentile(g_udp_latency_samples, g_udp_latency_count, 999U);
}

uint64_t network_stack_tcp_latency_p50_ns(void) {
  return percentile(g_tcp_latency_samples, g_tcp_latency_count, 50U);
}

uint64_t network_stack_tcp_latency_p95_ns(void) {
  return percentile(g_tcp_latency_samples, g_tcp_latency_count, 95U);
}

uint64_t network_stack_tcp_latency_p99_ns(void) {
  return percentile(g_tcp_latency_samples, g_tcp_latency_count, 99U);
}

uint64_t network_stack_tcp_latency_p999_ns(void) {
  return percentile(g_tcp_latency_samples, g_tcp_latency_count, 999U);
}

static void emit_latency_snapshot(uint64_t *udp50, uint64_t *udp95,
                                 uint64_t *udp99, uint64_t *udp999,
                                 uint64_t *tcp50, uint64_t *tcp95,
                                 uint64_t *tcp99, uint64_t *tcp999) {
  *udp50 = network_stack_udp_latency_p50_ns();
  *udp95 = network_stack_udp_latency_p95_ns();
  *udp99 = network_stack_udp_latency_p99_ns();
  *udp999 = network_stack_udp_latency_p999_ns();
  *tcp50 = network_stack_tcp_latency_p50_ns();
  *tcp95 = network_stack_tcp_latency_p95_ns();
  *tcp99 = network_stack_tcp_latency_p99_ns();
  *tcp999 = network_stack_tcp_latency_p999_ns();
}

static void build_app_udp_frame(uint8_t *frame, uint64_t payload_len) {
  bytes_zero(frame, NETWORK_BUFFER_SIZE);
  frame[12U] = 0x08;
  frame[13U] = 0x00;
  frame[14U] = 0x45;
  frame[15U] = 0x00;
  const uint16_t total = (uint16_t)(20U + 8U + payload_len);
  frame[16U] = (uint8_t)(total >> 8U);
  frame[17U] = (uint8_t)total;
  frame[22U] = 64;
  frame[23U] = NETWORK_IP_PROTO_UDP;
  frame[26U] = 10;
  frame[27U] = 0;
  frame[28U] = 2;
  frame[29U] = 15;
  frame[30U] = 10;
  frame[31U] = 0;
  frame[32U] = 2;
  frame[33U] = 2;
  frame[34U] = 0x60;
  frame[35U] = 0x01;
  frame[36U] = 0x22;
  frame[37U] = 0xB8;
  const uint16_t udp_len = (uint16_t)(8U + payload_len);
  frame[38U] = (uint8_t)(udp_len >> 8U);
  frame[39U] = (uint8_t)udp_len;
}

static void build_app_tcp_frame(uint8_t *frame, uint8_t flags,
                                uint16_t remote_port) {
  bytes_zero(frame, NETWORK_BUFFER_SIZE);
  frame[12U] = 0x08;
  frame[13U] = 0x00;
  frame[14U] = 0x45;
  frame[15U] = 0x00;
  frame[16U] = 0x00;
  frame[17U] = 0x2c;
  frame[22U] = 64;
  frame[23U] = NETWORK_IP_PROTO_TCP;
  frame[26U] = 10;
  frame[27U] = 0;
  frame[28U] = 2;
  frame[29U] = 15;
  frame[30U] = 10;
  frame[31U] = 0;
  frame[32U] = 2;
  frame[33U] = 2;
  frame[34U] = (uint8_t)(remote_port >> 8U);
  frame[35U] = (uint8_t)remote_port;
  frame[36U] = 0x00;
  frame[37U] = 0x16;
  frame[41U] = 1;
  frame[46U] = 0x60;
  frame[47U] = flags;
}

osai_status_t network_stack_app_udp_echo(const uint8_t *payload,
                                         uint64_t payload_len,
                                         uint64_t *echoed_bytes) {
  if (payload == 0 || echoed_bytes == 0 || payload_len == 0 ||
      payload_len > 64U) {
    return OSAI_ERR_INVALID;
  }

  uint8_t frame[NETWORK_BUFFER_SIZE];
  build_app_udp_frame(frame, payload_len);
  for (uint64_t i = 0; i < payload_len; ++i) {
    frame[42U + i] = payload[i];
  }

  const uint32_t queue_id = 3U;
  const uint32_t cell_id = 3U;
  if (network_stack_bind_queue(cell_id, queue_id, 0x8U) != OSAI_OK) {
    return OSAI_ERR_BUSY;
  }
  osai_status_t status = network_stack_process_udp_frame(frame, 42U + payload_len);
  kassert(network_stack_release_queue(queue_id, cell_id) == OSAI_OK);
  if (status != OSAI_OK) {
    return status;
  }
  *echoed_bytes = payload_len;
  klog("network: app udp echo payload=%lu queue=%u cell=%u\n",
       payload_len, queue_id, cell_id);
  return OSAI_OK;
}

osai_status_t network_stack_app_tcp_connect(uint64_t *round_trips) {
  if (round_trips == 0) {
    return OSAI_ERR_INVALID;
  }

  const uint32_t queue_id = 3U;
  const uint32_t cell_id = 3U;
  uint8_t syn[NETWORK_BUFFER_SIZE];
  uint8_t ack[NETWORK_BUFFER_SIZE];
  uint8_t rst[NETWORK_BUFFER_SIZE];
  const uint16_t remote_port = 0x6010U;

  if (network_stack_bind_queue(cell_id, queue_id, 0x8U) != OSAI_OK) {
    return OSAI_ERR_BUSY;
  }
  build_app_tcp_frame(syn, NETWORK_TCP_FLAG_SYN, remote_port);
  build_app_tcp_frame(ack, NETWORK_TCP_FLAG_ACK, remote_port);
  build_app_tcp_frame(rst, NETWORK_TCP_FLAG_RST, remote_port);

  osai_status_t status = network_stack_process_tcp_frame(syn, 58U);
  if (status == OSAI_OK) {
    status = network_stack_process_tcp_frame(ack, 58U);
  }
  if (status == OSAI_OK) {
    status = network_stack_process_tcp_frame(rst, 58U);
  }
  kassert(network_stack_release_queue(queue_id, cell_id) == OSAI_OK);
  if (status != OSAI_OK && status != OSAI_ERR_INVALID) {
    return status;
  }
  *round_trips = 2U;
  klog("network: app tcp connect-close queue=%u cell=%u round_trips=%lu\n",
       queue_id, cell_id, *round_trips);
  return OSAI_OK;
}

void network_stack_self_test(void) {
  uint8_t frame_udp[NETWORK_BUFFER_SIZE];
  uint8_t frame_udp_bad[NETWORK_BUFFER_SIZE];
  uint8_t frame_tcp_syn[NETWORK_BUFFER_SIZE];
  uint8_t frame_tcp_syn_ack[NETWORK_BUFFER_SIZE];
  uint8_t frame_tcp_timeout[NETWORK_BUFFER_SIZE];
  bytes_zero(frame_udp, sizeof(frame_udp));
  bytes_zero(frame_udp_bad, sizeof(frame_udp_bad));
  bytes_zero(frame_tcp_syn, sizeof(frame_tcp_syn));
  bytes_zero(frame_tcp_syn_ack, sizeof(frame_tcp_syn_ack));
  bytes_zero(frame_tcp_timeout, sizeof(frame_tcp_timeout));

  network_stack_init();

  kassert(network_stack_bind_queue(0, 1, 0x2U) == OSAI_OK);
  kassert(network_stack_bind_queue(0, 1, 0x2U) == OSAI_ERR_BUSY);
  kassert(network_stack_bind_queue(1, 2, 0x4U) == OSAI_OK);
  kassert(network_stack_bind_queue(2, 2, 0x8U) == OSAI_ERR_BUSY);

  kassert(network_stack_release_queue(2, 1) == OSAI_OK);
  kassert(network_stack_bind_queue(1, 2, 0x4U) == OSAI_OK);

  kassert(network_stack_queue_bindings() == 2U);

  frame_udp[12U] = 0x08;
  frame_udp[13U] = 0x00;
  frame_udp[14U] = 0x45;
  frame_udp[15U] = 0x00;
  frame_udp[16U] = 0x00;
  frame_udp[17U] = 0x20;
  frame_udp[18U] = 0x00;
  frame_udp[19U] = 0x00;
  frame_udp[20U] = 0x00;
  frame_udp[21U] = 0x00;
  frame_udp[22U] = 64;
  frame_udp[23U] = NETWORK_IP_PROTO_UDP;
  frame_udp[24U] = 0x00;
  frame_udp[25U] = 0x00;
  frame_udp[26U] = 10;
  frame_udp[27U] = 0;
  frame_udp[28U] = 2;
  frame_udp[29U] = 15;
  frame_udp[30U] = 10;
  frame_udp[31U] = 0;
  frame_udp[32U] = 2;
  frame_udp[33U] = 2;
  frame_udp[34U] = 0x12;
  frame_udp[35U] = 0x34;
  frame_udp[36U] = 0x56;
  frame_udp[37U] = 0x78;
  frame_udp[38U] = 0x00;
  frame_udp[39U] = 0x0C;
  frame_udp[40U] = 0x00;
  frame_udp[41U] = 0x00;
  frame_udp[42U] = 1;
  frame_udp[43U] = 2;
  frame_udp[44U] = 3;
  frame_udp[45U] = 4;

  kassert(network_stack_process_udp_frame(frame_udp, 46U) == OSAI_OK);
  kassert(network_stack_process_udp_frame(frame_udp, 46U) == OSAI_OK);
  kassert(g_udp_rx_count == 2U);
  kassert(network_stack_udp_flow_hit_count() == 1U);
  kassert(network_stack_udp_flow_count() == 1U);
  kassert(network_stack_expire_udp_flows(timer_now_ns() +
                                         NETWORK_UDP_IDLE_TIMEOUT_NS + 1U) ==
          1U);
  kassert(network_stack_udp_expired_count() == 1U);
  kassert(network_stack_udp_flow_count() == 0U);
  kassert(network_stack_process_udp_frame(frame_udp, 46U) == OSAI_OK);
  kassert(g_udp_rx_count == 3U);
  kassert(network_stack_udp_flow_count() == 1U);
  frame_udp_bad[13] = 0x06;
  kassert(network_stack_process_udp_frame(frame_udp_bad, 4U) == OSAI_ERR_INVALID);
  kassert(g_udp_dropped_count == 1U);
  kassert(g_udp_malformed_count == 1U);

  frame_tcp_syn[12] = 0x08;
  frame_tcp_syn[13] = 0x00;
  frame_tcp_syn[14] = 0x45;
  frame_tcp_syn[15] = 0x00;
  frame_tcp_syn[16] = 0x00;
  frame_tcp_syn[17] = 0x2c;
  frame_tcp_syn[18] = 0x00;
  frame_tcp_syn[19] = 0x00;
  frame_tcp_syn[20] = 0x00;
  frame_tcp_syn[21] = 0x40;
  frame_tcp_syn[22] = 64;
  frame_tcp_syn[23] = NETWORK_IP_PROTO_TCP;
  frame_tcp_syn[26] = 10;
  frame_tcp_syn[27] = 0;
  frame_tcp_syn[28] = 2;
  frame_tcp_syn[29] = 15;
  frame_tcp_syn[30] = 10;
  frame_tcp_syn[31] = 0;
  frame_tcp_syn[32] = 2;
  frame_tcp_syn[33] = 2;

  frame_tcp_syn[34] = 0x1f;
  frame_tcp_syn[35] = 0x90;
  frame_tcp_syn[36] = 0x00;
  frame_tcp_syn[37] = 0x50;
  frame_tcp_syn[38] = 0;
  frame_tcp_syn[39] = 0;
  frame_tcp_syn[40] = 0;
  frame_tcp_syn[41] = 1;
  frame_tcp_syn[42] = 0;
  frame_tcp_syn[43] = 0;
  frame_tcp_syn[44] = 0;
  frame_tcp_syn[45] = 0;
  frame_tcp_syn[46] = 0x60; /* offset 6 words */
  frame_tcp_syn[47] = NETWORK_TCP_FLAG_SYN;

  kassert(network_stack_process_tcp_frame(frame_tcp_syn, 58U) == OSAI_OK);
  kassert(network_stack_tcp_handshake_count() == 1U);
  kassert(network_stack_tcp_connections() == 0U);

  frame_tcp_syn_ack[14] = 0x45;
  for (uint32_t i = 0; i < 58U; ++i) {
    frame_tcp_syn_ack[i] = frame_tcp_syn[i];
  }
  frame_tcp_syn_ack[14] = frame_tcp_syn[14];
  frame_tcp_syn_ack[23] = NETWORK_IP_PROTO_TCP;
  frame_tcp_syn_ack[34] = 0x1f;
  frame_tcp_syn_ack[35] = 0x90;
  frame_tcp_syn_ack[36] = 0x00;
  frame_tcp_syn_ack[37] = 0x50;
  frame_tcp_syn_ack[38] = 0;
  frame_tcp_syn_ack[39] = 0;
  frame_tcp_syn_ack[40] = 0;
  frame_tcp_syn_ack[41] = 0;
  frame_tcp_syn_ack[42] = 0;
  frame_tcp_syn_ack[43] = 0;
  frame_tcp_syn_ack[44] = 0;
  frame_tcp_syn_ack[45] = 0;
  frame_tcp_syn_ack[46] = 0x60; /* offset 6 words */
  frame_tcp_syn_ack[47] = NETWORK_TCP_FLAG_ACK;

  kassert(network_stack_process_tcp_frame(frame_tcp_syn_ack, 58U) == OSAI_OK);
  kassert(network_stack_tcp_connections() == 1U);
  kassert(network_stack_tcp_established_count() == 1U);

  for (uint32_t i = 0; i < 58U; ++i) {
    frame_tcp_timeout[i] = frame_tcp_syn[i];
  }
  frame_tcp_timeout[35] = 0x91;
  kassert(network_stack_process_tcp_frame(frame_tcp_timeout, 58U) == OSAI_OK);
  kassert(network_stack_retransmit_tcp_flows(timer_now_ns() +
                                             NETWORK_TCP_RETRANSMIT_NS + 1U) ==
          1U);
  kassert(network_stack_tcp_retransmit_count() == 1U);
  kassert(network_stack_expire_tcp_flows(timer_now_ns() +
                                         NETWORK_TCP_RETRANSMIT_NS +
                                         NETWORK_TCP_SYN_TIMEOUT_NS + 2U) ==
          1U);
  kassert(network_stack_tcp_timeout_count() == 1U);
  kassert(network_stack_tcp_closed_count() == 1U);
  kassert(network_stack_tcp_connections() == 1U);

  kassert(network_stack_release_queue(1, 0) == OSAI_OK);
  kassert(network_stack_release_queue(2, 1) == OSAI_OK);
  kassert(network_stack_queue_bindings() == 0U);

  kassert(network_stack_udp_tx_count() == 3U);
  kassert(network_stack_udp_rx_count() == 3U);
  kassert(network_stack_tcp_reset_count() == 0U);
  kassert(network_stack_rx_packet_count() == 6U);
  kassert(network_stack_tx_packet_count() == 6U);
  kassert(network_stack_packet_drop_count() == 2U);
  kassert(network_stack_packet_lifecycle_count() == 18U);
  kassert(network_stack_queue_rx_enqueue_count() == 6U);
  kassert(network_stack_queue_tx_enqueue_count() == 6U);
  kassert(network_stack_queue_completion_count() == 6U);
  kassert(network_stack_queue_backpressure_drop_count() == 0U);
  kassert(network_stack_flow_core_mismatch_count() == 0U);

  uint64_t udp50;
  uint64_t udp95;
  uint64_t udp99;
  uint64_t udp999;
  uint64_t tcp50;
  uint64_t tcp95;
  uint64_t tcp99;
  uint64_t tcp999;
  emit_latency_snapshot(&udp50, &udp95, &udp99, &udp999, &tcp50, &tcp95,
                        &tcp99, &tcp999);

  klog(
      "network: queue-backed udp/tcp self-test passed rx=%lu tx=%lu drops=%lu "
      "lifecycle=%lu udp_flows=%lu udp_hits=%lu udp_expired=%lu "
      "tcp_timeouts=%lu tcp_retransmits=%lu queue_rx=%lu queue_tx=%lu "
      "queue_done=%lu backpressure=%lu flow_mismatch=%lu udp_p50=%lu p95=%lu "
      "p99=%lu p999=%lu tcp_p50=%lu p95=%lu p99=%lu p999=%lu\n",
      network_stack_rx_packet_count(), network_stack_tx_packet_count(),
      network_stack_packet_drop_count(), network_stack_packet_lifecycle_count(),
      network_stack_udp_flow_count(), network_stack_udp_flow_hit_count(),
      network_stack_udp_expired_count(), network_stack_tcp_timeout_count(),
      network_stack_tcp_retransmit_count(),
      network_stack_queue_rx_enqueue_count(),
      network_stack_queue_tx_enqueue_count(),
      network_stack_queue_completion_count(),
      network_stack_queue_backpressure_drop_count(),
      network_stack_flow_core_mismatch_count(),
      udp50, udp95, udp99, udp999, tcp50, tcp95, tcp99, tcp999);
}
