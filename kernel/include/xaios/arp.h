#ifndef XAIOS_ARP_H
#define XAIOS_ARP_H

#include <xaios/status.h>
#include <xaios/types.h>

#define XAIOS_ARP_CACHE_SIZE 4U
#define XAIOS_ARP_ETHERNET 0x0001U
#define XAIOS_ARP_IPV4 0x0800U
#define XAIOS_ARP_OP_REQUEST 1U
#define XAIOS_ARP_OP_REPLY 2U

typedef struct xaios_arp_entry {
  uint32_t ip;
  uint8_t mac[6];
  uint32_t active;
} xaios_arp_entry_t;

void arp_init(void);
xaios_status_t arp_cache_lookup(uint32_t ip, uint8_t mac[6]);
xaios_status_t arp_cache_insert(uint32_t ip, const uint8_t mac[6]);
xaios_status_t arp_build_request(uint8_t *frame, uint64_t *frame_len,
                                const uint8_t src_mac[6], uint32_t src_ip,
                                uint32_t target_ip);
xaios_status_t arp_process_reply(const uint8_t *frame, uint64_t frame_len);
xaios_status_t arp_build_reply(uint8_t *frame, uint64_t *frame_len,
                              const uint8_t src_mac[6], uint32_t src_ip,
                              const uint8_t dst_mac[6], uint32_t dst_ip);
uint64_t arp_cache_count(void);
void arp_self_test(void);

#endif
