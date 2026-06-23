#ifndef XAIOS_DNS_H
#define XAIOS_DNS_H

#include <xaios/status.h>
#include <xaios/types.h>

#define XAIOS_DNS_PORT 53U
#define XAIOS_DNS_MAX_NAME 256U
#define XAIOS_DNS_CACHE_SIZE 16U

/* DNS record types */
#define XAIOS_DNS_TYPE_A 1U
#define XAIOS_DNS_TYPE_AAAA 28U

/* DNS class IN */
#define XAIOS_DNS_CLASS_IN 1U

/* Configure DNS server IP (default 8.8.8.8) */
void dns_configure(uint32_t server_ip);

/* Resolve hostname to IPv4 address. Returns XAIOS_OK on cache hit or
 * successful query. out_ip is in host byte order. */
xaios_status_t dns_resolve(const char *hostname, uint32_t *out_ip);

/* DNS background tick: send pending queries, process responses.
 * Call from network_poll_tick(). */
void dns_tick(uint64_t now_ns);

/* Encode a DNS name (e.g., "www.google.com" -> 3www6google3com0) */
uint32_t dns_encode_name(uint8_t *buf, uint32_t buf_size, const char *name);

/* Decode a DNS name from a response (may use pointer compression) */
int dns_decode_name(const uint8_t *msg, uint32_t msg_len,
                    uint32_t offset, char *out, uint32_t out_size);

void dns_self_test(void);

#endif