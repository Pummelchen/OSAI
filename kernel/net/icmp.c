#include <osai/assert.h>
#include <osai/icmp.h>
#include <osai/ipv4.h>
#include <osai/klog.h>

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

osai_status_t icmp_process_echo_request(const uint8_t *frame,
                                         uint64_t frame_len,
                                         uint16_t *identifier,
                                         uint16_t *sequence) {
  if (frame == 0 || frame_len < 34U || identifier == 0 || sequence == 0) {
    return OSAI_ERR_INVALID;
  }
  /* Ethernet (14) + IPv4 (20) + ICMP header (8) = 42 minimum */
  if (frame_len < 42U) {
    return OSAI_ERR_INVALID;
  }
  /* Verify ethertype is IPv4 */
  if (get_be16(frame + 12) != 0x0800) {
    return OSAI_ERR_INVALID;
  }
  /* ICMP is at offset 34 (14 + 20) */
  const uint8_t *icmp = frame + 34U;
  uint8_t type = icmp[0];
  if (type != OSAI_ICMP_ECHO_REQUEST) {
    return OSAI_ERR_INVALID;
  }
  /* Verify ICMP checksum */
  uint16_t ip_total_len = get_be16(frame + 16);
  uint64_t icmp_len = (uint64_t)ip_total_len - OSAI_IPV4_HEADER_SIZE;
  if (34U + icmp_len > frame_len) {
    return OSAI_ERR_INVALID;
  }
  uint16_t cksum = ipv4_checksum(icmp, (uint32_t)icmp_len);
  if (cksum != 0) {
    return OSAI_ERR_INVALID;
  }
  *identifier = get_be16(icmp + 4);
  *sequence = get_be16(icmp + 6);
  return OSAI_OK;
}

osai_status_t icmp_build_echo_reply(uint8_t *frame, uint64_t *frame_len,
                                     const uint8_t src_mac[6],
                                     const uint8_t dst_mac[6],
                                     uint32_t src_ip, uint32_t dst_ip,
                                     const uint8_t *request_frame,
                                     uint64_t request_len) {
  if (frame == 0 || frame_len == 0 || src_mac == 0 || dst_mac == 0 ||
      request_frame == 0 || request_len < 42U) {
    return OSAI_ERR_INVALID;
  }

  /* Get ICMP payload from request */
  const uint8_t *req_icmp = request_frame + 34U;
  uint16_t ip_total_len = get_be16(request_frame + 16);
  uint64_t icmp_len = (uint64_t)ip_total_len - OSAI_IPV4_HEADER_SIZE;
  if (34U + icmp_len > request_len || icmp_len < 8U) {
    return OSAI_ERR_INVALID;
  }

  uint64_t total = 14U + OSAI_IPV4_HEADER_SIZE + icmp_len;
  bytes_zero(frame, total);

  /* Ethernet header */
  for (uint32_t i = 0; i < 6; ++i) {
    frame[i] = dst_mac[i];
    frame[6 + i] = src_mac[i];
  }
  put_be16(frame + 12, 0x0800);

  /* IPv4 header */
  ipv4_build_header(frame + 14, (uint16_t)(OSAI_IPV4_HEADER_SIZE + icmp_len),
                    OSAI_IPV4_PROTO_ICMP, src_ip, dst_ip);

  /* ICMP echo reply */
  uint8_t *icmp = frame + 34U;
  icmp[0] = OSAI_ICMP_ECHO_REPLY; /* type */
  icmp[1] = 0;                     /* code */
  put_be16(icmp + 2, 0);          /* checksum (compute later) */
  /* Copy identifier and sequence from request */
  icmp[4] = req_icmp[4];
  icmp[5] = req_icmp[5];
  icmp[6] = req_icmp[6];
  icmp[7] = req_icmp[7];
  /* Copy payload from request */
  for (uint64_t i = 8; i < icmp_len; ++i) {
    icmp[i] = req_icmp[i];
  }
  /* Compute ICMP checksum */
  uint16_t cksum = ipv4_checksum(icmp, (uint32_t)icmp_len);
  put_be16(icmp + 2, cksum);

  *frame_len = total;
  return OSAI_OK;
}

void icmp_self_test(void) {
  /* Build a fake echo request */
  uint8_t request[64];
  bytes_zero(request, sizeof(request));
  uint8_t src_mac[6] = {0x52, 0x54, 0x00, 0x12, 0x35, 0x02};
  uint8_t dst_mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x15};
  /* Ethernet */
  for (uint32_t i = 0; i < 6; ++i) {
    request[i] = dst_mac[i];
    request[6 + i] = src_mac[i];
  }
  put_be16(request + 12, 0x0800);
  /* IPv4 */
  ipv4_build_header(request + 14, 28, OSAI_IPV4_PROTO_ICMP, 0x0a000202,
                    0x0a00020f);
  /* ICMP echo request */
  request[34] = OSAI_ICMP_ECHO_REQUEST;
  request[35] = 0;
  put_be16(request + 36, 0); /* checksum */
  put_be16(request + 38, 0x1234); /* identifier */
  put_be16(request + 40, 0x0001); /* sequence */
  /* Compute ICMP checksum */
  uint16_t cksum = ipv4_checksum(request + 34, 8);
  put_be16(request + 36, cksum);

  uint16_t id = 0, seq = 0;
  kassert(icmp_process_echo_request(request, 42, &id, &seq) == OSAI_OK);
  kassert(id == 0x1234);
  kassert(seq == 0x0001);

  /* Build echo reply */
  uint8_t reply[64];
  uint64_t reply_len = 0;
  kassert(icmp_build_echo_reply(reply, &reply_len, dst_mac, src_mac,
                                 0x0a00020f, 0x0a000202, request, 42) == OSAI_OK);
  kassert(reply_len == 42);
  kassert(reply[34] == OSAI_ICMP_ECHO_REPLY);
  kassert(ipv4_checksum(reply + 34, 8) == 0);

  klog("icmp: self-test passed id=0x%04x seq=%u reply_len=%lu\n", id, seq,
       reply_len);
}
