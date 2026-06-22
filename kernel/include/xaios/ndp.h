#ifndef XAIOS_NDP_H
#define XAIOS_NDP_H

#include <xaios/ip_addr.h>
#include <xaios/status.h>
#include <xaios/types.h>

#define XAIOS_NDP_CACHE_SIZE 8U

typedef struct xaios_ndp_entry {
  xaios_ip_addr_t ip;
  uint8_t mac[6];
  uint32_t active;
} xaios_ndp_entry_t;

void ndp_init(void);
xaios_status_t ndp_cache_lookup(const xaios_ip_addr_t *ip, uint8_t mac[6]);
xaios_status_t ndp_cache_insert(const xaios_ip_addr_t *ip, const uint8_t mac[6]);

xaios_status_t ndp_build_neighbor_solicitation(
    uint8_t *frame, uint64_t *frame_len,
    const uint8_t src_mac[6],
    const xaios_ip_addr_t *src_ip,
    const xaios_ip_addr_t *target_ip);

xaios_status_t ndp_process_neighbor_advertisement(
    const uint8_t *frame, uint64_t frame_len);

uint64_t ndp_cache_count(void);
void ndp_self_test(void);

#endif
