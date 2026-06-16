#include <osai/arp.h>
#include <osai/assert.h>
#include <osai/klog.h>

static osai_arp_entry_t g_arp_cache[OSAI_ARP_CACHE_SIZE];

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

static void put_be32(uint8_t *dst, uint32_t value) {
  dst[0] = (uint8_t)(value >> 24U);
  dst[1] = (uint8_t)(value >> 16U);
  dst[2] = (uint8_t)(value >> 8U);
  dst[3] = (uint8_t)value;
}

static uint32_t get_be32(const uint8_t *src) {
  return ((uint32_t)src[0] << 24U) | ((uint32_t)src[1] << 16U) |
         ((uint32_t)src[2] << 8U) | (uint32_t)src[3];
}

void arp_init(void) {
  for (uint32_t i = 0; i < OSAI_ARP_CACHE_SIZE; ++i) {
    g_arp_cache[i].active = 0;
    g_arp_cache[i].ip = 0;
    bytes_zero(g_arp_cache[i].mac, 6);
  }
  klog("arp: cache initialized slots=%u\n", OSAI_ARP_CACHE_SIZE);
}

osai_status_t arp_cache_lookup(uint32_t ip, uint8_t mac[6]) {
  if (mac == 0) {
    return OSAI_ERR_INVALID;
  }
  for (uint32_t i = 0; i < OSAI_ARP_CACHE_SIZE; ++i) {
    if (g_arp_cache[i].active != 0 && g_arp_cache[i].ip == ip) {
      for (uint32_t j = 0; j < 6; ++j) {
        mac[j] = g_arp_cache[i].mac[j];
      }
      return OSAI_OK;
    }
  }
  return OSAI_ERR_NOT_FOUND;
}

osai_status_t arp_cache_insert(uint32_t ip, const uint8_t mac[6]) {
  if (mac == 0) {
    return OSAI_ERR_INVALID;
  }
  /* Update existing entry */
  for (uint32_t i = 0; i < OSAI_ARP_CACHE_SIZE; ++i) {
    if (g_arp_cache[i].active != 0 && g_arp_cache[i].ip == ip) {
      for (uint32_t j = 0; j < 6; ++j) {
        g_arp_cache[i].mac[j] = mac[j];
      }
      return OSAI_OK;
    }
  }
  /* Insert into free slot */
  for (uint32_t i = 0; i < OSAI_ARP_CACHE_SIZE; ++i) {
    if (g_arp_cache[i].active == 0) {
      g_arp_cache[i].ip = ip;
      g_arp_cache[i].active = 1;
      for (uint32_t j = 0; j < 6; ++j) {
        g_arp_cache[i].mac[j] = mac[j];
      }
      klog("arp: cached ip=%u.%u.%u.%u mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
           (ip >> 24U) & 0xff, (ip >> 16U) & 0xff, (ip >> 8U) & 0xff,
           ip & 0xff, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
      return OSAI_OK;
    }
  }
  return OSAI_ERR_NO_MEMORY;
}

osai_status_t arp_build_request(uint8_t *frame, uint64_t *frame_len,
                                const uint8_t src_mac[6], uint32_t src_ip,
                                uint32_t target_ip) {
  if (frame == 0 || frame_len == 0 || src_mac == 0) {
    return OSAI_ERR_INVALID;
  }
  bytes_zero(frame, 42);
  /* Ethernet header: dst=ff:ff:ff:ff:ff:ff, src=src_mac, type=ARP */
  for (uint32_t i = 0; i < 6; ++i) {
    frame[i] = 0xff;
    frame[6 + i] = src_mac[i];
  }
  put_be16(frame + 12, 0x0806);
  /* ARP payload */
  put_be16(frame + 14, OSAI_ARP_ETHERNET);
  put_be16(frame + 16, OSAI_ARP_IPV4);
  frame[18] = 6; /* hw addr len */
  frame[19] = 4; /* proto addr len */
  put_be16(frame + 20, OSAI_ARP_OP_REQUEST);
  for (uint32_t i = 0; i < 6; ++i) {
    frame[22 + i] = src_mac[i];
  }
  put_be32(frame + 28, src_ip);
  /* target mac = 00:00:00:00:00:00 */
  put_be32(frame + 38, target_ip);
  *frame_len = 42;
  return OSAI_OK;
}

osai_status_t arp_process_reply(const uint8_t *frame, uint64_t frame_len) {
  if (frame == 0 || frame_len < 42) {
    return OSAI_ERR_INVALID;
  }
  if (get_be16(frame + 12) != 0x0806) {
    return OSAI_ERR_INVALID;
  }
  if (get_be16(frame + 20) != OSAI_ARP_OP_REPLY) {
    return OSAI_ERR_INVALID;
  }
  uint32_t sender_ip = get_be32(frame + 28);
  const uint8_t *sender_mac = frame + 22;
  return arp_cache_insert(sender_ip, sender_mac);
}

osai_status_t arp_build_reply(uint8_t *frame, uint64_t *frame_len,
                              const uint8_t src_mac[6], uint32_t src_ip,
                              const uint8_t dst_mac[6], uint32_t dst_ip) {
  if (frame == 0 || frame_len == 0 || src_mac == 0 || dst_mac == 0) {
    return OSAI_ERR_INVALID;
  }
  bytes_zero(frame, 42);
  for (uint32_t i = 0; i < 6; ++i) {
    frame[i] = dst_mac[i];
    frame[6 + i] = src_mac[i];
  }
  put_be16(frame + 12, 0x0806);
  put_be16(frame + 14, OSAI_ARP_ETHERNET);
  put_be16(frame + 16, OSAI_ARP_IPV4);
  frame[18] = 6;
  frame[19] = 4;
  put_be16(frame + 20, OSAI_ARP_OP_REPLY);
  for (uint32_t i = 0; i < 6; ++i) {
    frame[22 + i] = src_mac[i];
  }
  put_be32(frame + 28, src_ip);
  for (uint32_t i = 0; i < 6; ++i) {
    frame[32 + i] = dst_mac[i];
  }
  put_be32(frame + 38, dst_ip);
  *frame_len = 42;
  return OSAI_OK;
}

uint64_t arp_cache_count(void) {
  uint64_t count = 0;
  for (uint32_t i = 0; i < OSAI_ARP_CACHE_SIZE; ++i) {
    if (g_arp_cache[i].active != 0) {
      ++count;
    }
  }
  return count;
}

void arp_self_test(void) {
  arp_init();
  kassert(arp_cache_count() == 0);

  uint8_t mac1[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
  uint8_t mac_out[6] = {0};

  kassert(arp_cache_lookup(0x0a000202, mac_out) == OSAI_ERR_NOT_FOUND);
  kassert(arp_cache_insert(0x0a000202, mac1) == OSAI_OK);
  kassert(arp_cache_count() == 1);
  kassert(arp_cache_lookup(0x0a000202, mac_out) == OSAI_OK);
  kassert(mac_out[0] == 0x02 && mac_out[5] == 0x01);

  /* Test ARP request build */
  uint8_t frame[64];
  uint64_t frame_len = 0;
  kassert(arp_build_request(frame, &frame_len, mac1, 0x0a00020f,
                            0x0a000202) == OSAI_OK);
  kassert(frame_len == 42);
  kassert(get_be16(frame + 12) == 0x0806);
  kassert(get_be16(frame + 20) == OSAI_ARP_OP_REQUEST);

  /* Test ARP reply processing */
  uint8_t reply[42];
  bytes_zero(reply, 42);
  put_be16(reply + 12, 0x0806);
  put_be16(reply + 14, OSAI_ARP_ETHERNET);
  put_be16(reply + 16, OSAI_ARP_IPV4);
  reply[18] = 6;
  reply[19] = 4;
  put_be16(reply + 20, OSAI_ARP_OP_REPLY);
  uint8_t gw_mac[6] = {0x52, 0x54, 0x00, 0x12, 0x35, 0x02};
  for (uint32_t i = 0; i < 6; ++i) {
    reply[22 + i] = gw_mac[i];
  }
  put_be32(reply + 28, 0x0a000202);
  kassert(arp_process_reply(reply, 42) == OSAI_OK);
  kassert(arp_cache_count() == 1);
  uint8_t gw_out[6] = {0};
  kassert(arp_cache_lookup(0x0a000202, gw_out) == OSAI_OK);
  kassert(gw_out[0] == 0x52 && gw_out[5] == 0x02);

  /* Test ARP reply build */
  uint8_t rep_frame[64];
  uint64_t rep_len = 0;
  kassert(arp_build_reply(rep_frame, &rep_len, mac1, 0x0a00020f, gw_mac,
                          0x0a000202) == OSAI_OK);
  kassert(rep_len == 42);
  kassert(get_be16(rep_frame + 20) == OSAI_ARP_OP_REPLY);

  klog("arp: self-test passed cache_entries=%lu\n", arp_cache_count());
}
