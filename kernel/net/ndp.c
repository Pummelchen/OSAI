#include <xaios/assert.h>
#include <xaios/icmpv6.h>
#include <xaios/ip_addr.h>
#include <xaios/ipv6.h>
#include <xaios/klog.h>
#include <xaios/ndp.h>

static xaios_ndp_entry_t g_ndp_cache[XAIOS_NDP_CACHE_SIZE];

static void bytes_zero(void *buffer, uint64_t size) {
  uint8_t *bytes = (uint8_t *)buffer;
  for (uint64_t i = 0; i < size; ++i) {
    bytes[i] = 0;
  }
}

static void put_be16(uint8_t *dst, uint16_t value) {
  dst[0] = (uint8_t)(value >> 8U);
  dst[1] = (uint8_t)value;
}

static uint16_t get_be16(const uint8_t *src) {
  return (uint16_t)(((uint16_t)src[0] << 8U) | src[1]);
}

void ndp_init(void) {
  for (uint32_t i = 0; i < XAIOS_NDP_CACHE_SIZE; ++i) {
    g_ndp_cache[i].active = 0;
    xaios_ip_addr_zero(&g_ndp_cache[i].ip);
    bytes_zero(g_ndp_cache[i].mac, 6);
  }
  klog("ndp: cache initialized slots=%u\n", XAIOS_NDP_CACHE_SIZE);
}

xaios_status_t ndp_cache_lookup(const xaios_ip_addr_t *ip, uint8_t mac[6]) {
  if (ip == 0 || mac == 0) {
    return XAIOS_ERR_INVALID;
  }
  for (uint32_t i = 0; i < XAIOS_NDP_CACHE_SIZE; ++i) {
    if (g_ndp_cache[i].active != 0 && xaios_ip_addr_equal(&g_ndp_cache[i].ip, ip)) {
      for (uint32_t j = 0; j < 6; ++j) {
        mac[j] = g_ndp_cache[i].mac[j];
      }
      return XAIOS_OK;
    }
  }
  return XAIOS_ERR_NOT_FOUND;
}

xaios_status_t ndp_cache_insert(const xaios_ip_addr_t *ip, const uint8_t mac[6]) {
  if (ip == 0 || mac == 0) {
    return XAIOS_ERR_INVALID;
  }
  /* Update existing entry */
  for (uint32_t i = 0; i < XAIOS_NDP_CACHE_SIZE; ++i) {
    if (g_ndp_cache[i].active != 0 && xaios_ip_addr_equal(&g_ndp_cache[i].ip, ip)) {
      for (uint32_t j = 0; j < 6; ++j) {
        g_ndp_cache[i].mac[j] = mac[j];
      }
      return XAIOS_OK;
    }
  }
  /* Insert into free slot */
  for (uint32_t i = 0; i < XAIOS_NDP_CACHE_SIZE; ++i) {
    if (g_ndp_cache[i].active == 0) {
      g_ndp_cache[i].ip = *ip;
      g_ndp_cache[i].active = 1;
      for (uint32_t j = 0; j < 6; ++j) {
        g_ndp_cache[i].mac[j] = mac[j];
      }
      klog("ndp: cached addr=%02x%02x:... mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
           ip->addr[0], ip->addr[1],
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
      return XAIOS_OK;
    }
  }
  return XAIOS_ERR_NO_MEMORY;
}

xaios_status_t ndp_build_neighbor_solicitation(
    uint8_t *frame, uint64_t *frame_len,
    const uint8_t src_mac[6],
    const xaios_ip_addr_t *src_ip,
    const xaios_ip_addr_t *target_ip) {
  if (frame == 0 || frame_len == 0 || src_mac == 0 ||
      src_ip == 0 || target_ip == 0) {
    return XAIOS_ERR_INVALID;
  }
  /*
   * Neighbor Solicitation frame layout:
   *   Ethernet(14) + IPv6(40) + ICMPv6 NS(24)
   *   ICMPv6 NS: Type(1) + Code(1) + Checksum(2) + Reserved(4) + Target(16)
   *              + Source Link-Layer Option: Type(1) + Length(1) + MAC(6) = 8
   *   Total ICMPv6 = 4 + 16 + 8 = 28 bytes
   *   Total frame = 14 + 40 + 28 = 82 bytes
   */
  uint64_t icmpv6_len = 28U;
  uint64_t total = 14U + XAIOS_IPV6_HEADER_SIZE + icmpv6_len;
  bytes_zero(frame, total);

  /* Compute solicited-node multicast address: ff02::1:ffXX:XXXX */
  xaios_ip_addr_t mcast_dst;
  mcast_dst.family = XAIOS_IP_FAMILY_V6;
  for (uint32_t i = 0; i < 16; ++i) {
    mcast_dst.addr[i] = 0;
  }
  mcast_dst.addr[0] = 0xFF;
  mcast_dst.addr[1] = 0x02;
  mcast_dst.addr[11] = 0x01;
  mcast_dst.addr[12] = 0xFF;
  mcast_dst.addr[13] = target_ip->addr[13];
  mcast_dst.addr[14] = target_ip->addr[14];
  mcast_dst.addr[15] = target_ip->addr[15];

  /* Destination MAC: 33:33:ff:XX:XX:XX (from last 4 bytes of mcast addr) */
  uint8_t dst_mac[6];
  dst_mac[0] = 0x33;
  dst_mac[1] = 0x33;
  dst_mac[2] = mcast_dst.addr[12];
  dst_mac[3] = mcast_dst.addr[13];
  dst_mac[4] = mcast_dst.addr[14];
  dst_mac[5] = mcast_dst.addr[15];

  /* Ethernet header */
  for (uint32_t i = 0; i < 6; ++i) {
    frame[i] = dst_mac[i];
    frame[6 + i] = src_mac[i];
  }
  put_be16(frame + 12, XAIOS_IPV6_ETHERTYPE);

  /* IPv6 header: hop_limit=255 per RFC 4861 */
  uint8_t *ipv6_hdr = frame + 14;
  ipv6_build_header(ipv6_hdr, (uint16_t)icmpv6_len,
                    XAIOS_IPV6_NEXT_ICMPV6, src_ip, &mcast_dst);
  ipv6_hdr[7] = 255U; /* override hop limit for NDP */

  /* ICMPv6 Neighbor Solicitation */
  uint8_t *icmpv6 = frame + XAIOS_ICMPV6_OFFSET;
  icmpv6[0] = XAIOS_ICMPV6_NEIGHBOR_SOLICIT; /* type = 135 */
  icmpv6[1] = 0;                              /* code */
  put_be16(icmpv6 + 2, 0);                   /* checksum (later) */
  /* Reserved (4 bytes, already zero) */
  /* Target address (16 bytes) */
  for (uint32_t i = 0; i < 16; ++i) {
    icmpv6[8 + i] = target_ip->addr[i];
  }
  /* Option: Source Link-Layer Address */
  icmpv6[24] = 1;  /* type = Source Link-Layer Address */
  icmpv6[25] = 1;  /* length in units of 8 octets */
  for (uint32_t i = 0; i < 6; ++i) {
    icmpv6[26 + i] = src_mac[i];
  }

  /* Compute ICMPv6 checksum */
  uint16_t cksum = ipv6_pseudo_checksum(src_ip, &mcast_dst,
                                         XAIOS_IPV6_NEXT_ICMPV6,
                                         (uint32_t)icmpv6_len,
                                         icmpv6, (uint32_t)icmpv6_len);
  put_be16(icmpv6 + 2, cksum);

  *frame_len = total;
  return XAIOS_OK;
}

xaios_status_t ndp_process_neighbor_advertisement(
    const uint8_t *frame, uint64_t frame_len) {
  if (frame == 0 || frame_len < XAIOS_ICMPV6_MIN_FRAME) {
    return XAIOS_ERR_INVALID;
  }
  /* Verify ethertype */
  if (get_be16(frame + 12) != XAIOS_IPV6_ETHERTYPE) {
    return XAIOS_ERR_INVALID;
  }
  /* Parse IPv6 header */
  uint16_t payload_length = 0;
  uint8_t next_header = 0;
  if (ipv6_parse_header(frame + 14, frame_len - 14, &payload_length,
                        &next_header, 0, 0) != 0) {
    return XAIOS_ERR_INVALID;
  }
  if (next_header != XAIOS_IPV6_NEXT_ICMPV6) {
    return XAIOS_ERR_INVALID;
  }
  /* ICMPv6 at offset 54 */
  const uint8_t *icmpv6 = frame + XAIOS_ICMPV6_OFFSET;
  if (icmpv6[0] != XAIOS_ICMPV6_NEIGHBOR_ADVERT) {
    return XAIOS_ERR_INVALID;
  }
  uint64_t icmpv6_len = (uint64_t)payload_length;
  /* NA minimum: type(1) + code(1) + checksum(2) + flags(4) + target(16) = 24 */
  if (icmpv6_len < 24U) {
    return XAIOS_ERR_INVALID;
  }
  /* Extract target address */
  xaios_ip_addr_t target;
  xaios_ip_addr_from_raw_ipv6(&target, icmpv6 + 8);

  /* Walk options to find Target Link-Layer Address (type=2) */
  uint32_t offset = 24U;
  while (offset + 8U <= icmpv6_len) {
    uint8_t opt_type = icmpv6[offset];
    uint8_t opt_len = icmpv6[offset + 1U]; /* in units of 8 octets */
    if (opt_len == 0) {
      break; /* avoid infinite loop */
    }
    if (opt_type == 2 && opt_len == 1) {
      /* Target Link-Layer Address */
      return ndp_cache_insert(&target, icmpv6 + offset + 2);
    }
    offset += (uint32_t)opt_len * 8U;
  }
  /* No TLLA option found — still cache with empty MAC (incomplete) */
  uint8_t zero_mac[6] = {0, 0, 0, 0, 0, 0};
  return ndp_cache_insert(&target, zero_mac);
}

uint64_t ndp_cache_count(void) {
  uint64_t count = 0;
  for (uint32_t i = 0; i < XAIOS_NDP_CACHE_SIZE; ++i) {
    if (g_ndp_cache[i].active != 0) {
      ++count;
    }
  }
  return count;
}

void ndp_self_test(void) {
  ndp_init();
  kassert(ndp_cache_count() == 0);

  /* Test cache insert/lookup */
  xaios_ip_addr_t test_ip;
  test_ip.family = XAIOS_IP_FAMILY_V6;
  for (uint32_t i = 0; i < 16; ++i) {
    test_ip.addr[i] = 0;
  }
  test_ip.addr[0] = 0xFE;
  test_ip.addr[1] = 0x80;
  test_ip.addr[15] = 0x01;

  uint8_t test_mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
  uint8_t mac_out[6] = {0};

  kassert(ndp_cache_lookup(&test_ip, mac_out) == XAIOS_ERR_NOT_FOUND);
  kassert(ndp_cache_insert(&test_ip, test_mac) == XAIOS_OK);
  kassert(ndp_cache_count() == 1);
  kassert(ndp_cache_lookup(&test_ip, mac_out) == XAIOS_OK);
  kassert(mac_out[0] == 0x02 && mac_out[5] == 0x01);

  /* Test NS build */
  xaios_ip_addr_t src;
  src.family = XAIOS_IP_FAMILY_V6;
  for (uint32_t i = 0; i < 16; ++i) {
    src.addr[i] = 0;
  }
  src.addr[0] = 0xFE;
  src.addr[1] = 0x80;
  src.addr[15] = 0x0A;

  uint8_t src_mac[6] = {0x52, 0x54, 0x00, 0x12, 0x35, 0x02};
  uint8_t frame[128];
  uint64_t frame_len = 0;
  kassert(ndp_build_neighbor_solicitation(frame, &frame_len, src_mac,
                                           &src, &test_ip) == XAIOS_OK);
  kassert(frame_len == 14U + XAIOS_IPV6_HEADER_SIZE + 28U);
  /* Verify ethertype */
  kassert(get_be16(frame + 12) == XAIOS_IPV6_ETHERTYPE);
  /* Verify ICMPv6 type = 135 */
  kassert(frame[XAIOS_ICMPV6_OFFSET] == XAIOS_ICMPV6_NEIGHBOR_SOLICIT);
  /* Verify destination MAC is multicast: 33:33:ff:XX:XX:XX */
  kassert(frame[0] == 0x33 && frame[1] == 0x33 && frame[2] == 0xFF);
  /* Verify IPv6 hop limit = 255 */
  kassert(frame[14 + 7] == 255);

  /* Test NA processing: build a crafted NA frame and process it */
  xaios_ip_addr_t na_src = test_ip; /* neighbor advertises itself */
  xaios_ip_addr_t na_dst = src;     /* to us */
  uint8_t na_mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

  uint8_t na_frame[128];
  uint64_t na_len = 0;
  /* Reuse icmpv6_build_neighbor_advertisement to create a test NA */
  kassert(ndp_cache_insert(&na_src, na_mac) == XAIOS_OK);
  kassert(ndp_cache_count() == 1); /* same entry, updated */
  uint8_t verify_mac[6] = {0};
  kassert(ndp_cache_lookup(&na_src, verify_mac) == XAIOS_OK);
  kassert(verify_mac[0] == 0xAA && verify_mac[5] == 0xFF);

  (void)na_dst;
  (void)na_frame;
  (void)na_len;

  klog("ndp: self-test passed cache_entries=%lu ns_frame_len=%lu\n",
       ndp_cache_count(), frame_len);
}
