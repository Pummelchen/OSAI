#ifndef XAIOS_VIRTIO_NET_H
#define XAIOS_VIRTIO_NET_H

#include <xaios/status.h>
#include <xaios/types.h>

void virtio_net_self_test(void);
xaios_status_t virtio_net_init_persistent(void);
xaios_status_t virtio_net_tx(const uint8_t *data, uint64_t len);
uint32_t virtio_net_rx_poll(uint8_t *buffer, uint64_t buffer_size);
xaios_status_t virtio_net_get_mac(uint8_t mac[6]);

#endif
