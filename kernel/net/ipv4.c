#include <xaios/assert.h>
#include <xaios/ipv4.h>
#include <xaios/klog.h>

#define XAIOS_IPV4_FRAG_BUCKETS 8U

static ipv4_frag_bucket_t g_frag_buckets[XAIOS_IPV4_FRAG_BUCKETS];

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

static uint16_t get_be16(const uint8_t *src) {
  return (uint16_t)(((uint16_t)src[0] << 8U) | src[1]);
}

static uint32_t get_be32(const uint8_t *src) {
  return ((uint32_t)src[0] << 24U) | ((uint32_t)src[1] << 16U) |
         ((uint32_t)src[2] << 8U) | (uint32_t)src[3];
}

static void bytes_copy(void *dst, const void *src, uint64_t n) {
  uint8_t *d = (uint8_t *)dst;
  const uint8_t *s = (const uint8_t *)src;
  for (uint64_t i = 0; i < n; ++i) { d[i] = s[i]; }
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
  sum += (src_ip >> 16U) & 0xFFFFU;
  sum += src_ip & 0xFFFFU;
  sum += (dst_ip >> 16U) & 0xFFFFU;
  sum += dst_ip & 0xFFFFU;
  sum += (uint64_t)protocol;
  sum += (uint64_t)payload_length;
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
  if (hdr == 0) { return; }
  hdr[0] = XAIOS_IPV4_VERSION_IHL;
  hdr[1] = 0;
  put_be16(hdr + 2, total_length);
  put_be16(hdr + 4, 0);
  put_be16(hdr + 6, 0x4000);
  hdr[8] = 64;
  hdr[9] = protocol;
  put_be16(hdr + 10, 0);
  put_be32(hdr + 12, src_ip);
  put_be32(hdr + 16, dst_ip);
  uint16_t cksum = ipv4_checksum(hdr, XAIOS_IPV4_HEADER_SIZE);
  put_be16(hdr + 10, cksum);
}

int ipv4_validate_incoming(const uint8_t *frame, uint64_t frame_len) {
  if (frame == 0 || frame_len < 34U) { return 0; }
  const uint8_t *ip = frame + 14U;
  uint8_t version = (uint8_t)(ip[0] >> 4U);
  uint8_t ihl = (uint8_t)(ip[0] & 0x0FU);
  if (version != 4U) { return 0; }
  if (ihl < 5U) { return 0; }
  uint64_t ip_header_bytes = (uint64_t)ihl * 4U;
  uint16_t total_length = (uint16_t)(((uint16_t)ip[2] << 8U) | ip[3]);
  if ((uint64_t)total_length < ip_header_bytes) { return 0; }
  if (14U + (uint64_t)total_length > frame_len) { return 0; }
  uint8_t ttl = ip[8];
  if (ttl == 0U) { return 0; }
  if (ipv4_checksum(ip, (uint32_t)ip_header_bytes) != 0U) { return 0; }
  return 1;
}

/* B1: Fragment an outgoing IPv4 frame into 1400-byte payload chunks */
xaios_status_t ipv4_fragment(const uint8_t *frame, uint64_t frame_len,
                              uint8_t *out_buf, uint64_t *out_len,
                              uint64_t out_capacity) {
  if (frame == 0 || out_buf == 0 || out_len == 0 || frame_len < 34U) {
    return XAIOS_ERR_INVALID;
  }

  uint16_t total_len = get_be16(frame + 16);
  uint64_t payload_len = (uint64_t)total_len - XAIOS_IPV4_HEADER_SIZE;

  if (payload_len <= 1400U) {
    if (out_capacity < frame_len) { return XAIOS_ERR_NO_MEMORY; }
    bytes_copy(out_buf, frame, frame_len);
    *out_len = frame_len;
    return XAIOS_OK;
  }

  uint16_t id = get_be16(frame + 18);
  uint32_t src_ip = get_be32(frame + 12);
  uint32_t dst_ip = get_be32(frame + 16);
  uint8_t protocol = frame[23];

  uint64_t per_frag = 1392U; /* multiple of 8, leaves room for headers */
  uint64_t offset = 0;
  uint64_t written = 0;

  while (offset < payload_len) {
    uint64_t this_frag = payload_len - offset;
    if (this_frag > per_frag) { this_frag = per_frag; }
    uint64_t frag_total = 14U + XAIOS_IPV4_HEADER_SIZE + this_frag;
    if (written + frag_total > out_capacity) { return XAIOS_ERR_NO_MEMORY; }

    for (uint32_t i = 0; i < 6; ++i) {
      out_buf[written + i] = frame[i];
      out_buf[written + 6 + i] = frame[6 + i];
    }
    put_be16(out_buf + written + 12, 0x0800);

    uint8_t *frag_ip = out_buf + written + 14U;
    frag_ip[0] = XAIOS_IPV4_VERSION_IHL;
    frag_ip[1] = 0;
    put_be16(frag_ip + 2, (uint16_t)(XAIOS_IPV4_HEADER_SIZE + this_frag));
    put_be16(frag_ip + 4, id);
    uint16_t off_val = (uint16_t)(offset / 8U);
    int more = (offset + this_frag < payload_len) ? 1 : 0;
    put_be16(frag_ip + 6, (uint16_t)((more ? XAIOS_IPV4_FLAG_MF : 0) | off_val));
    frag_ip[8] = 64;
    frag_ip[9] = protocol;
    put_be16(frag_ip + 10, 0);
    put_be32(frag_ip + 12, src_ip);
    put_be32(frag_ip + 16, dst_ip);
    uint16_t cksum = ipv4_checksum(frag_ip, XAIOS_IPV4_HEADER_SIZE);
    put_be16(frag_ip + 10, cksum);

    bytes_copy(out_buf + written + 34U,
               frame + 14U + XAIOS_IPV4_HEADER_SIZE + offset, this_frag);
    *out_len = written + frag_total;
    written += frag_total;
    offset += this_frag;
  }

  return XAIOS_OK;
}

/* B1: Check if frame is a fragment (MF set or offset > 0) */
int ipv4_is_fragment(const uint8_t *frame, uint64_t frame_len) {
  if (frame == 0 || frame_len < 34U) { return 0; }
  uint16_t flags_off = get_be16(frame + 20);
  if ((flags_off & XAIOS_IPV4_FLAG_MF) != 0 ||
      (flags_off & XAIOS_IPV4_OFFSET_MASK) != 0) {
    return 1;
  }
  return 0;
}

/* B1: Reassemble fragments — stores fragment data, returns OK when complete */
xaios_status_t ipv4_reassemble(uint8_t *frame, uint64_t *frame_len) {
  if (frame == 0 || frame_len == 0 || *frame_len < 34U) {
    return XAIOS_ERR_INVALID;
  }

  uint8_t *ip = frame + 14U;
  uint16_t id = get_be16(frame + 18);
  uint32_t src_ip = get_be32(frame + 12);
  uint8_t ihl = (uint8_t)(ip[0] & 0x0FU);
  uint64_t ip_hdr_bytes = (uint64_t)ihl * 4U;
  uint16_t flags_off = get_be16(frame + 20);
  uint16_t offset = (uint16_t)(flags_off & XAIOS_IPV4_OFFSET_MASK);
  int mf = ((flags_off & XAIOS_IPV4_FLAG_MF) != 0) ? 1 : 0;
  uint16_t frag_data_len = (uint16_t)(*frame_len - 14U - ip_hdr_bytes);

  int bucket_idx = -1;
  int free_slot = -1;
  for (uint32_t i = 0; i < XAIOS_IPV4_FRAG_BUCKETS; ++i) {
    if (g_frag_buckets[i].active != 0 &&
        g_frag_buckets[i].id == id &&
        g_frag_buckets[i].src_ip == src_ip) {
      bucket_idx = (int)i;
      break;
    }
    if (g_frag_buckets[i].active == 0 && free_slot < 0) {
      free_slot = (int)i;
    }
  }

  if (bucket_idx < 0) {
    if (free_slot < 0) {
      uint64_t oldest = UINT64_MAX;
      uint32_t oldest_i = 0;
      for (uint32_t i = 0; i < XAIOS_IPV4_FRAG_BUCKETS; ++i) {
        if (g_frag_buckets[i].active != 0 &&
            g_frag_buckets[i].first_arrival_ns < oldest) {
          oldest = g_frag_buckets[i].first_arrival_ns;
          oldest_i = i;
        }
      }
      g_frag_buckets[oldest_i].active = 0;
      bucket_idx = (int)oldest_i;
    } else {
      bucket_idx = free_slot;
    }
    g_frag_buckets[bucket_idx].active = 1;
    g_frag_buckets[bucket_idx].src_ip = src_ip;
    g_frag_buckets[bucket_idx].id = id;
    g_frag_buckets[bucket_idx].frag_count = 0;
    g_frag_buckets[bucket_idx].total_len = 0;
  }

  ipv4_frag_bucket_t *b = &g_frag_buckets[bucket_idx];
  if (b->frag_count >= XAIOS_IPV4_MAX_FRAG) {
    return XAIOS_ERR_NO_MEMORY;
  }

  uint32_t slot = b->frag_count;
  b->frag_offsets[slot] = offset;
  b->frag_lens[slot] = frag_data_len;
  bytes_copy(b->frags[slot], ip + ip_hdr_bytes, (uint64_t)frag_data_len);
  b->frag_count++;
  if (!mf) {
    b->total_len = (uint32_t)(offset + frag_data_len);
  }

  uint32_t max_off = 0;
  int have_last = 0;
  for (uint32_t i = 0; i < b->frag_count; ++i) {
    uint32_t end = (uint32_t)b->frag_offsets[i] + (uint32_t)b->frag_lens[i];
    if (end > max_off) { max_off = end; }
  }
  if (b->total_len > 0 && max_off >= b->total_len) {
    have_last = 1;
  }

  if (have_last && (uint32_t)max_off <= 1500U) {
    uint32_t reasm_len = (uint32_t)max_off;
    if (reasm_len > 1480U) { reasm_len = 1480U; }

    bytes_copy(frame + 14, ip, XAIOS_IPV4_HEADER_SIZE);
    put_be16(frame + 16, (uint16_t)(XAIOS_IPV4_HEADER_SIZE + (uint16_t)reasm_len));
    put_be16(frame + 18, id);
    put_be16(frame + 20, 0);
    uint8_t *reasm_data = frame + 14U + XAIOS_IPV4_HEADER_SIZE;

    for (uint32_t i = 0; i < b->frag_count; ++i) {
      uint32_t off = b->frag_offsets[i];
      bytes_copy(reasm_data + off, b->frags[i], (uint64_t)b->frag_lens[i]);
    }

    *frame_len = (uint64_t)(14U + XAIOS_IPV4_HEADER_SIZE + (uint64_t)reasm_len);
    b->active = 0;
    return XAIOS_OK;
  }

  return XAIOS_ERR_INVALID;
}

void ipv4_frag_self_test(void) {
  uint8_t frame[1520];
  bytes_copy(frame, frame, 0); /* placeholder */
  klog("ipv4: frag self-test stub\n");
}

void ipv4_self_test(void) {
  uint8_t hdr[20];
  ipv4_build_header(hdr, 40, XAIOS_IPV4_PROTO_UDP, 0x0a00020f, 0x0a000202);
  kassert(hdr[0] == 0x45);
  kassert(hdr[8] == 64);
  kassert(hdr[9] == XAIOS_IPV4_PROTO_UDP);
  kassert(ipv4_checksum(hdr, 20) == 0);

  uint8_t payload[4] = {0x00, 0x01, 0x00, 0x02};
  uint16_t cksum = ipv4_pseudo_checksum(0x0a00020f, 0x0a000202,
                                         XAIOS_IPV4_PROTO_UDP, 4, payload, 4);
  kassert(cksum != 0);
  klog("ipv4: self-test passed header_cksum=0x%04x pseudo_cksum=0x%04x\n",
       0, cksum);
}