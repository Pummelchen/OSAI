#include <xaios/assert.h>
#include <xaios/ipv4.h>
#include <xaios/klog.h>
#include <xaios/routing.h>

static routing_entry_t g_routing_table[ROUTING_TABLE_SIZE];

void routing_init(void) {
  for (uint32_t i = 0; i < ROUTING_TABLE_SIZE; ++i) {
    g_routing_table[i].active = 0;
    g_routing_table[i].dest_network = 0;
    g_routing_table[i].netmask = 0;
    g_routing_table[i].gateway = 0;
  }

  /* Pre-populate: local subnet 10.0.2.0/24 direct
   * XAIOS_IPV4_GUEST_IP = 0x0a00020f (network order) = 10.0.2.15
   * host order = 0x0f02000a, but we store as big-endian uint32
   * to match ip header byte order. Actually, let's use the same
   * representation as the IP headers (network byte order). */
  uint32_t local_net = 0x0a000200U;   /* 10.0.2.0 network byte order */
  uint32_t local_mask = 0xffffff00U;  /* 255.255.255.0 */
  routing_add(local_net, local_mask, 0);

  /* Default route via gateway 10.0.2.2 */
  uint32_t gateway = XAIOS_IPV4_GATEWAY; /* already network byte order */
  routing_add(0x00000000U, 0x00000000U, gateway);

  klog("routing: initialized (local=%08x/24 gw=%08x)\n",
       local_net, gateway);
}

xaios_status_t routing_add(uint32_t dest_network, uint32_t netmask,
                            uint32_t gateway) {
  for (uint32_t i = 0; i < ROUTING_TABLE_SIZE; ++i) {
    if (g_routing_table[i].active == 0) {
      g_routing_table[i].dest_network = dest_network & netmask;
      g_routing_table[i].netmask = netmask;
      g_routing_table[i].gateway = gateway;
      g_routing_table[i].active = 1;
      return XAIOS_OK;
    }
  }
  return XAIOS_ERR_NO_MEMORY;
}

uint32_t routing_lookup(uint32_t dest_ip) {
  uint32_t best_match = UINT32_MAX;
  uint32_t best_prefix_len = 0;
  int found = 0;

  for (uint32_t i = 0; i < ROUTING_TABLE_SIZE; ++i) {
    if (g_routing_table[i].active == 0) {
      continue;
    }
    uint32_t masked = dest_ip & g_routing_table[i].netmask;
    if (masked == g_routing_table[i].dest_network) {
      /* Count prefix length (number of 1-bits in netmask) */
      uint32_t mask = g_routing_table[i].netmask;
      uint32_t prefix_len = 0;
      while (mask != 0) {
        prefix_len += (mask & 1U);
        mask >>= 1U;
      }
      /* Longest prefix match */
      if (prefix_len > best_prefix_len || !found) {
        best_match = i;
        best_prefix_len = prefix_len;
        found = 1;
      }
    }
  }

  if (!found) {
    return 0; /* no route */
  }
  if (g_routing_table[best_match].gateway == 0) {
    return dest_ip; /* direct delivery */
  }
  return g_routing_table[best_match].gateway;
}

void routing_self_test(void) {
  /* Verify local subnet lookup: 10.0.2.5 should be direct */
  uint32_t local_host = 0x0a000205U; /* 10.0.2.5 */
  uint32_t next_hop = routing_lookup(local_host);
  kassert(next_hop == local_host); /* direct = dest itself */

  /* Verify external address goes through gateway */
  uint32_t external = 0x08080808U; /* 8.8.8.8 */
  next_hop = routing_lookup(external);
  kassert(next_hop == XAIOS_IPV4_GATEWAY);

  /* Verify 10.0.2.15 (our IP) is direct */
  next_hop = routing_lookup(XAIOS_IPV4_GUEST_IP);
  kassert(next_hop == XAIOS_IPV4_GUEST_IP);

  klog("routing: self-test passed\n");
}
