#ifndef XAIOS_ICMPV6_H
#define XAIOS_ICMPV6_H

#include <xaios/ip_addr.h>
#include <xaios/status.h>
#include <xaios/types.h>

#define XAIOS_ICMPV6_ECHO_REQUEST     128U
#define XAIOS_ICMPV6_ECHO_REPLY       129U
#define XAIOS_ICMPV6_NEIGHBOR_SOLICIT 135U
#define XAIOS_ICMPV6_NEIGHBOR_ADVERT  136U

/* Minimum frame: Ethernet(14) + IPv6(40) + ICMPv6(8) = 62 */
#define XAIOS_ICMPV6_MIN_FRAME        62U
/* Ethernet(14) + IPv6(40) + ICMPv6 header */
#define XAIOS_ICMPV6_OFFSET           54U

xaios_status_t icmpv6_process_echo_request(
    const uint8_t *frame, uint64_t frame_len,
    uint16_t *identifier, uint16_t *sequence,
    xaios_ip_addr_t *src, xaios_ip_addr_t *dst);

xaios_status_t icmpv6_build_echo_reply(
    uint8_t *frame, uint64_t *frame_len,
    const uint8_t src_mac[6], const uint8_t dst_mac[6],
    const xaios_ip_addr_t *src_ip, const xaios_ip_addr_t *dst_ip,
    const uint8_t *request_frame, uint64_t request_len);

xaios_status_t icmpv6_build_neighbor_advertisement(
    uint8_t *frame, uint64_t *frame_len,
    const uint8_t src_mac[6], const uint8_t dst_mac[6],
    const xaios_ip_addr_t *src_ip, const xaios_ip_addr_t *dst_ip,
    const xaios_ip_addr_t *target,
    const uint8_t *ns_frame, uint64_t ns_len);

void icmpv6_self_test(void);

#endif
