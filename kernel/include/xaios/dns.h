#ifndef XAIOS_DNS_H
#define XAIOS_DNS_H

#include <xaios/status.h>
#include <xaios/types.h>

/* DNS resolver stub. Currently returns XAIOS_ERR_INVALID for all queries.
 * Real implementation (UDP port 53 query/response) is deferred. */
xaios_status_t dns_resolve(const char *hostname, uint32_t *out_ip);

#endif
