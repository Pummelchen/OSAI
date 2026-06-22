#ifndef XAIOS_IPV6_H
#define XAIOS_IPV6_H

#include <xaios/ip_addr.h>
#include <xaios/status.h>
#include <xaios/types.h>

#define XAIOS_IPV6_HEADER_SIZE       40U
#define XAIOS_IPV6_VERSION_TC_FLOW   0x60000000U /* version=6, TC=0, flow=0 */
#define XAIOS_IPV6_ETHERTYPE         0x86DDU
#define XAIOS_IPV6_NEXT_ICMPV6       58U
#define XAIOS_IPV6_NEXT_TCP          6U
#define XAIOS_IPV6_NEXT_UDP         17U
#define XAIOS_IPV6_DEFAULT_HOP_LIMIT 64U

/* Link-local prefix fe80::/10 */
#define XAIOS_IPV6_LINK_LOCAL_BYTE0  0xFEU
#define XAIOS_IPV6_LINK_LOCAL_BYTE1  0x80U

void ipv6_build_header(uint8_t *hdr, uint16_t payload_length,
                       uint8_t next_header,
                       const xaios_ip_addr_t *src,
                       const xaios_ip_addr_t *dst);

int ipv6_parse_header(const uint8_t *hdr, uint64_t hdr_len,
                      uint16_t *payload_length, uint8_t *next_header,
                      xaios_ip_addr_t *src, xaios_ip_addr_t *dst);

uint16_t ipv6_pseudo_checksum(const xaios_ip_addr_t *src,
                               const xaios_ip_addr_t *dst,
                               uint8_t next_header,
                               uint32_t upper_layer_length,
                               const uint8_t *payload,
                               uint32_t payload_len);

/* Derive link-local address from MAC (EUI-64) */
void ipv6_link_local_from_mac(xaios_ip_addr_t *addr, const uint8_t mac[6]);

void ipv6_self_test(void);

#endif
