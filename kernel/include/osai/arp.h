#ifndef OSAI_ARP_H
#define OSAI_ARP_H

#include <osai/status.h>
#include <osai/types.h>

#define OSAI_ARP_CACHE_SIZE 4U
#define OSAI_ARP_ETHERNET 0x0001U
#define OSAI_ARP_IPV4 0x0800U
#define OSAI_ARP_OP_REQUEST 1U
#define OSAI_ARP_OP_REPLY 2U

typedef struct osai_arp_entry {
  uint32_t ip;
  uint8_t mac[6];
  uint32_t active;
} osai_arp_entry_t;

void arp_init(void);
osai_status_t arp_cache_lookup(uint32_t ip, uint8_t mac[6]);
osai_status_t arp_cache_insert(uint32_t ip, const uint8_t mac[6]);
osai_status_t arp_build_request(uint8_t *frame, uint64_t *frame_len,
                                const uint8_t src_mac[6], uint32_t src_ip,
                                uint32_t target_ip);
osai_status_t arp_process_reply(const uint8_t *frame, uint64_t frame_len);
osai_status_t arp_build_reply(uint8_t *frame, uint64_t *frame_len,
                              const uint8_t src_mac[6], uint32_t src_ip,
                              const uint8_t dst_mac[6], uint32_t dst_ip);
uint64_t arp_cache_count(void);
void arp_self_test(void);

#endif
