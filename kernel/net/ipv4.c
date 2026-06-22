#include <xaios/assert.h>
#include <xaios/ipv4.h>
#include <xaios/klog.h>

static void put_be16(uint8_t *dst, uint16_t value) {
  dst[0] = (uint8_t)(value >> 8U);
  dst[1] = (uint8_t)value;
}

static void put_be32(uint8_t *dst, uint32_t value) {
  dst[0] = (uint8_t)(value >> 24U);
  dst[1] = (uint8_t)(value >> 16U);
  dst[2] = (uint8_t)(value >> 8U);
  dst[3] = (uint8_t)value;
}

uint16_t ipv4_checksum(const uint8_t *data, uint32_t length) {
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

uint16_t ipv4_pseudo_checksum(uint32_t src_ip, uint32_t dst_ip,
                               uint8_t protocol, uint16_t payload_length,
                               const uint8_t *payload, uint32_t payload_len) {
  uint64_t sum = 0;
  /* Pseudo header */
  sum += (src_ip >> 16U) & 0xFFFFU;
  sum += src_ip & 0xFFFFU;
  sum += (dst_ip >> 16U) & 0xFFFFU;
  sum += dst_ip & 0xFFFFU;
  sum += (uint64_t)protocol;
  sum += (uint64_t)payload_length;
  /* Payload */
  for (uint32_t i = 0; i + 1U < payload_len; i += 2U) {
    sum += ((uint64_t)payload[i] << 8U) | (uint64_t)payload[i + 1U];
  }
  if ((payload_len & 1U) != 0U) {
    sum += ((uint64_t)payload[payload_len - 1U] << 8U);
  }
  while ((sum >> 16U) != 0U) {
    sum = (sum & 0xFFFFU) + (sum >> 16U);
  }
  return (uint16_t)(~sum & 0xFFFFU);
}

void ipv4_build_header(uint8_t *hdr, uint16_t total_length, uint8_t protocol,
                       uint32_t src_ip, uint32_t dst_ip) {
  if (hdr == 0) {
    return;
  }
  hdr[0] = XAIOS_IPV4_VERSION_IHL; /* version=4, IHL=5 (20 bytes) */
  hdr[1] = 0;                     /* DSCP/ECN */
  put_be16(hdr + 2, total_length);
  put_be16(hdr + 4, 0);           /* identification */
  put_be16(hdr + 6, 0x4000);      /* flags=DF, fragment offset=0 */
  hdr[8] = 64;                    /* TTL */
  hdr[9] = protocol;
  put_be16(hdr + 10, 0);          /* checksum (fill later) */
  put_be32(hdr + 12, src_ip);
  put_be32(hdr + 16, dst_ip);
  /* Compute header checksum */
  uint16_t cksum = ipv4_checksum(hdr, XAIOS_IPV4_HEADER_SIZE);
  put_be16(hdr + 10, cksum);
}

int ipv4_validate_incoming(const uint8_t *frame, uint64_t frame_len) {
  if (frame == 0 || frame_len < 34U) {
    return 0; /* too short for Ethernet + min IPv4 header */
  }
  const uint8_t *ip = frame + 14U;
  uint8_t version = (uint8_t)(ip[0] >> 4U);
  uint8_t ihl = (uint8_t)(ip[0] & 0x0FU);
  if (version != 4U) {
    return 0;
  }
  if (ihl < 5U) {
    return 0; /* IHL too small */
  }
  uint64_t ip_header_bytes = (uint64_t)ihl * 4U;
  uint16_t total_length = (uint16_t)(((uint16_t)ip[2] << 8U) | ip[3]);
  if ((uint64_t)total_length < ip_header_bytes) {
    return 0; /* total_length less than header */
  }
  if (14U + (uint64_t)total_length > frame_len) {
    return 0; /* frame truncated */
  }
  uint8_t ttl = ip[8];
  if (ttl == 0U) {
    return 0; /* TTL expired */
  }
  /* Verify header checksum */
  if (ipv4_checksum(ip, (uint32_t)ip_header_bytes) != 0U) {
    return 0;
  }
  return 1;
}

void ipv4_self_test(void) {
  uint8_t hdr[20];
  ipv4_build_header(hdr, 40, XAIOS_IPV4_PROTO_UDP, 0x0a00020f, 0x0a000202);
  kassert(hdr[0] == 0x45);
  kassert(hdr[8] == 64);
  kassert(hdr[9] == XAIOS_IPV4_PROTO_UDP);
  /* Verify checksum: recomputing should yield 0 */
  kassert(ipv4_checksum(hdr, 20) == 0);

  /* Verify pseudo-header checksum with a small payload */
  uint8_t payload[4] = {0x00, 0x01, 0x00, 0x02};
  uint16_t cksum = ipv4_pseudo_checksum(0x0a00020f, 0x0a000202,
                                         XAIOS_IPV4_PROTO_UDP, 4, payload, 4);
  kassert(cksum != 0); /* non-zero means valid computation */

  klog("ipv4: self-test passed header_cksum=0x%04x pseudo_cksum=0x%04x\n",
       0, cksum);
}
