#include <xaios/assert.h>
#include <xaios/icmpv6.h>
#include <xaios/ipv6.h>
#include <xaios/klog.h>

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

xaios_status_t icmpv6_process_echo_request(
    const uint8_t *frame, uint64_t frame_len,
    uint16_t *identifier, uint16_t *sequence,
    xaios_ip_addr_t *src, xaios_ip_addr_t *dst) {
  if (frame == 0 || frame_len < XAIOS_ICMPV6_MIN_FRAME ||
      identifier == 0 || sequence == 0) {
    return XAIOS_ERR_INVALID;
  }
  /* Verify ethertype is IPv6 (0x86DD) */
  if (get_be16(frame + 12) != XAIOS_IPV6_ETHERTYPE) {
    return XAIOS_ERR_INVALID;
  }
  /* Parse IPv6 header at offset 14 */
  uint16_t payload_length = 0;
  uint8_t next_header = 0;
  xaios_ip_addr_t ip_src;
  xaios_ip_addr_t ip_dst;
  if (ipv6_parse_header(frame + 14, frame_len - 14, &payload_length,
                        &next_header, &ip_src, &ip_dst) != 0) {
    return XAIOS_ERR_INVALID;
  }
  if (next_header != XAIOS_IPV6_NEXT_ICMPV6) {
    return XAIOS_ERR_INVALID;
  }
  /* ICMPv6 at offset 54 (14 + 40) */
  const uint8_t *icmpv6 = frame + XAIOS_ICMPV6_OFFSET;
  uint8_t type = icmpv6[0];
  if (type != XAIOS_ICMPV6_ECHO_REQUEST) {
    return XAIOS_ERR_INVALID;
  }
  /* Verify ICMPv6 length */
  uint64_t icmpv6_len = (uint64_t)payload_length;
  if (XAIOS_ICMPV6_OFFSET + icmpv6_len > frame_len || icmpv6_len < 8U) {
    return XAIOS_ERR_INVALID;
  }
  /* Verify ICMPv6 checksum using pseudo-header */
  uint16_t cksum = ipv6_pseudo_checksum(&ip_src, &ip_dst,
                                         XAIOS_IPV6_NEXT_ICMPV6,
                                         (uint32_t)icmpv6_len,
                                         icmpv6, (uint32_t)icmpv6_len);
  if (cksum != 0) {
    return XAIOS_ERR_INVALID;
  }
  *identifier = get_be16(icmpv6 + 4);
  *sequence = get_be16(icmpv6 + 6);
  if (src != 0) {
    *src = ip_src;
  }
  if (dst != 0) {
    *dst = ip_dst;
  }
  return XAIOS_OK;
}

xaios_status_t icmpv6_build_echo_reply(
    uint8_t *frame, uint64_t *frame_len,
    const uint8_t src_mac[6], const uint8_t dst_mac[6],
    const xaios_ip_addr_t *src_ip, const xaios_ip_addr_t *dst_ip,
    const uint8_t *request_frame, uint64_t request_len) {
  if (frame == 0 || frame_len == 0 || src_mac == 0 || dst_mac == 0 ||
      src_ip == 0 || dst_ip == 0 || request_frame == 0 ||
      request_len < XAIOS_ICMPV6_MIN_FRAME) {
    return XAIOS_ERR_INVALID;
  }
  /* Get ICMPv6 payload from request */
  const uint8_t *req_icmpv6 = request_frame + XAIOS_ICMPV6_OFFSET;
  uint16_t payload_length = get_be16(request_frame + 18); /* IPv6 payload len */
  uint64_t icmpv6_len = (uint64_t)payload_length;
  if (XAIOS_ICMPV6_OFFSET + icmpv6_len > request_len || icmpv6_len < 8U) {
    return XAIOS_ERR_INVALID;
  }

  uint64_t total = 14U + XAIOS_IPV6_HEADER_SIZE + icmpv6_len;
  bytes_zero(frame, total);

  /* Ethernet header */
  for (uint32_t i = 0; i < 6; ++i) {
    frame[i] = dst_mac[i];
    frame[6 + i] = src_mac[i];
  }
  put_be16(frame + 12, XAIOS_IPV6_ETHERTYPE);

  /* IPv6 header: next_header=58 (ICMPv6) */
  ipv6_build_header(frame + 14, (uint16_t)icmpv6_len,
                    XAIOS_IPV6_NEXT_ICMPV6, src_ip, dst_ip);

  /* ICMPv6 echo reply */
  uint8_t *icmpv6 = frame + XAIOS_ICMPV6_OFFSET;
  icmpv6[0] = XAIOS_ICMPV6_ECHO_REPLY; /* type = 129 */
  icmpv6[1] = 0;                       /* code */
  put_be16(icmpv6 + 2, 0);            /* checksum (compute later) */
  /* Copy identifier, sequence, and payload from request */
  for (uint64_t i = 4; i < icmpv6_len; ++i) {
    icmpv6[i] = req_icmpv6[i];
  }
  /* Compute ICMPv6 checksum with pseudo-header */
  uint16_t cksum = ipv6_pseudo_checksum(src_ip, dst_ip,
                                         XAIOS_IPV6_NEXT_ICMPV6,
                                         (uint32_t)icmpv6_len,
                                         icmpv6, (uint32_t)icmpv6_len);
  put_be16(icmpv6 + 2, cksum);

  *frame_len = total;
  return XAIOS_OK;
}

xaios_status_t icmpv6_build_neighbor_advertisement(
    uint8_t *frame, uint64_t *frame_len,
    const uint8_t src_mac[6], const uint8_t dst_mac[6],
    const xaios_ip_addr_t *src_ip, const xaios_ip_addr_t *dst_ip,
    const xaios_ip_addr_t *target,
    const uint8_t *ns_frame, uint64_t ns_len) {
  if (frame == 0 || frame_len == 0 || src_mac == 0 || dst_mac == 0 ||
      src_ip == 0 || dst_ip == 0 || target == 0) {
    return XAIOS_ERR_INVALID;
  }
  (void)ns_frame;
  (void)ns_len;

  /*
   * ICMPv6 Neighbor Advertisement:
   *   Type (1) + Code (1) + Checksum (2) = 4 bytes header
   *   Flags+Reserved (4 bytes): R=0x80, S=0x40, O=0x20
   *   Target Address (16 bytes)
   *   Option: Target Link-Layer Address: Type=2, Length=1 (8 octets), MAC (6 bytes)
   *   Total ICMPv6 payload = 4 + 4 + 16 + 8 = 32 bytes
   */
  uint64_t icmpv6_len = 32U;
  uint64_t total = 14U + XAIOS_IPV6_HEADER_SIZE + icmpv6_len;
  bytes_zero(frame, total);

  /* Ethernet header */
  for (uint32_t i = 0; i < 6; ++i) {
    frame[i] = dst_mac[i];
    frame[6 + i] = src_mac[i];
  }
  put_be16(frame + 12, XAIOS_IPV6_ETHERTYPE);

  /* IPv6 header: hop_limit=255 per RFC 4861 */
  uint8_t *ipv6_hdr = frame + 14;
  ipv6_build_header(ipv6_hdr, (uint16_t)icmpv6_len,
                    XAIOS_IPV6_NEXT_ICMPV6, src_ip, dst_ip);
  ipv6_hdr[7] = 255U; /* override hop limit for NDP */

  /* ICMPv6 Neighbor Advertisement */
  uint8_t *icmpv6 = frame + XAIOS_ICMPV6_OFFSET;
  icmpv6[0] = XAIOS_ICMPV6_NEIGHBOR_ADVERT; /* type = 136 */
  icmpv6[1] = 0;                            /* code */
  put_be16(icmpv6 + 2, 0);                 /* checksum (later) */
  /* Flags: Router=0, Solicited=1, Override=1 */
  icmpv6[4] = 0x60U; /* S + O */
  icmpv6[5] = 0;
  icmpv6[6] = 0;
  icmpv6[7] = 0;
  /* Target address (16 bytes) */
  for (uint32_t i = 0; i < 16; ++i) {
    icmpv6[8 + i] = target->addr[i];
  }
  /* Option: Target Link-Layer Address */
  icmpv6[24] = 2;  /* type = Target Link-Layer Address */
  icmpv6[25] = 1;  /* length in units of 8 octets */
  for (uint32_t i = 0; i < 6; ++i) {
    icmpv6[26 + i] = src_mac[i];
  }

  /* Compute ICMPv6 checksum */
  uint16_t cksum = ipv6_pseudo_checksum(src_ip, dst_ip,
                                         XAIOS_IPV6_NEXT_ICMPV6,
                                         (uint32_t)icmpv6_len,
                                         icmpv6, (uint32_t)icmpv6_len);
  put_be16(icmpv6 + 2, cksum);

  *frame_len = total;
  return XAIOS_OK;
}

void icmpv6_self_test(void) {
  /* Build a fake echo request: fe80::1 -> fe80::2 */
  uint8_t request[96];
  bytes_zero(request, sizeof(request));

  uint8_t src_mac[6] = {0x52, 0x54, 0x00, 0x12, 0x35, 0x02};
  uint8_t dst_mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x15};

  xaios_ip_addr_t src;
  src.family = XAIOS_IP_FAMILY_V6;
  for (uint32_t i = 0; i < 16; ++i) {
    src.addr[i] = 0;
  }
  src.addr[0] = 0xFE;
  src.addr[1] = 0x80;
  src.addr[15] = 0x01;

  xaios_ip_addr_t dst;
  dst.family = XAIOS_IP_FAMILY_V6;
  for (uint32_t i = 0; i < 16; ++i) {
    dst.addr[i] = 0;
  }
  dst.addr[0] = 0xFE;
  dst.addr[1] = 0x80;
  dst.addr[15] = 0x02;

  /* Ethernet */
  for (uint32_t i = 0; i < 6; ++i) {
    request[i] = dst_mac[i];
    request[6 + i] = src_mac[i];
  }
  put_be16(request + 12, XAIOS_IPV6_ETHERTYPE);

  /* IPv6 header: payload = 8 bytes (ICMPv6 echo header only) */
  uint16_t icmpv6_len = 8;
  ipv6_build_header(request + 14, icmpv6_len, XAIOS_IPV6_NEXT_ICMPV6,
                    &src, &dst);

  /* ICMPv6 echo request: type=128, code=0, checksum=0, id=0xABCD, seq=0x0042 */
  uint8_t *icmpv6 = request + XAIOS_ICMPV6_OFFSET;
  icmpv6[0] = XAIOS_ICMPV6_ECHO_REQUEST;
  icmpv6[1] = 0;
  put_be16(icmpv6 + 2, 0); /* checksum placeholder */
  put_be16(icmpv6 + 4, 0xABCD); /* identifier */
  put_be16(icmpv6 + 6, 0x0042); /* sequence */

  /* Compute checksum */
  uint16_t cksum = ipv6_pseudo_checksum(&src, &dst,
                                         XAIOS_IPV6_NEXT_ICMPV6,
                                         (uint32_t)icmpv6_len,
                                         icmpv6, (uint32_t)icmpv6_len);
  put_be16(icmpv6 + 2, cksum);

  /* Test process_echo_request */
  uint64_t request_len = 14U + XAIOS_IPV6_HEADER_SIZE + icmpv6_len;
  uint16_t id = 0, seq = 0;
  xaios_ip_addr_t parsed_src;
  xaios_ip_addr_t parsed_dst;
  kassert(icmpv6_process_echo_request(request, request_len, &id, &seq,
                                       &parsed_src, &parsed_dst) == XAIOS_OK);
  kassert(id == 0xABCD);
  kassert(seq == 0x0042);
  kassert(xaios_ip_addr_equal(&parsed_src, &src));
  kassert(xaios_ip_addr_equal(&parsed_dst, &dst));

  /* Build echo reply */
  uint8_t reply[96];
  uint64_t reply_len = 0;
  kassert(icmpv6_build_echo_reply(reply, &reply_len, dst_mac, src_mac,
                                   &dst, &src, request, request_len) == XAIOS_OK);
  kassert(reply_len == request_len);
  kassert(reply[XAIOS_ICMPV6_OFFSET] == XAIOS_ICMPV6_ECHO_REPLY);
  /* Verify reply checksum */
  uint8_t *reply_icmpv6 = reply + XAIOS_ICMPV6_OFFSET;
  uint16_t reply_cksum = ipv6_pseudo_checksum(&dst, &src,
                                               XAIOS_IPV6_NEXT_ICMPV6,
                                               (uint32_t)icmpv6_len,
                                               reply_icmpv6,
                                               (uint32_t)icmpv6_len);
  kassert(reply_cksum == 0);

  /* Build Neighbor Advertisement */
  xaios_ip_addr_t target = src; /* target is fe80::1 */
  uint8_t na_frame[128];
  uint64_t na_len = 0;
  kassert(icmpv6_build_neighbor_advertisement(na_frame, &na_len,
            dst_mac, src_mac, &dst, &src, &target, 0, 0) == XAIOS_OK);
  kassert(na_len == 14U + XAIOS_IPV6_HEADER_SIZE + 32U);
  kassert(na_frame[XAIOS_ICMPV6_OFFSET] == XAIOS_ICMPV6_NEIGHBOR_ADVERT);
  /* Verify NA checksum */
  uint8_t *na_icmpv6 = na_frame + XAIOS_ICMPV6_OFFSET;
  uint16_t na_cksum = ipv6_pseudo_checksum(&dst, &src,
                                            XAIOS_IPV6_NEXT_ICMPV6,
                                            32, na_icmpv6, 32);
  kassert(na_cksum == 0);

  klog("icmpv6: self-test passed echo_id=0x%04x seq=%u reply_len=%lu na_len=%lu\n",
       id, seq, reply_len, na_len);
}
