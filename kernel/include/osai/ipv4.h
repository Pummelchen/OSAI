#ifndef OSAI_IPV4_H
#define OSAI_IPV4_H

#include <osai/status.h>
#include <osai/types.h>

#define OSAI_IPV4_PROTO_ICMP 1U
#define OSAI_IPV4_PROTO_TCP 6U
#define OSAI_IPV4_PROTO_UDP 17U
#define OSAI_IPV4_HEADER_SIZE 20U
#define OSAI_IPV4_VERSION_IHL 0x45U

/* Static IP configuration for QEMU SLIRP */
#define OSAI_IPV4_GUEST_IP  0x0a00020fU /* 10.0.2.15 */
#define OSAI_IPV4_GATEWAY   0x0a000202U /* 10.0.2.2 */
#define OSAI_IPV4_NETMASK   0xffffff00U /* 255.255.255.0 */

void ipv4_build_header(uint8_t *hdr, uint16_t total_length, uint8_t protocol,
                       uint32_t src_ip, uint32_t dst_ip);
uint16_t ipv4_checksum(const uint8_t *data, uint32_t length);
uint16_t ipv4_pseudo_checksum(uint32_t src_ip, uint32_t dst_ip,
                               uint8_t protocol, uint16_t payload_length,
                               const uint8_t *payload, uint32_t payload_len);
void ipv4_self_test(void);

#endif
