#include <xaios/dns.h>

xaios_status_t dns_resolve(const char *hostname, uint32_t *out_ip) {
  (void)hostname;
  (void)out_ip;
  return XAIOS_ERR_INVALID; /* DNS not implemented */
}
