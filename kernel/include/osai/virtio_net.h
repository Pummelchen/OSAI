#ifndef OSAI_VIRTIO_NET_H
#define OSAI_VIRTIO_NET_H

#include <osai/status.h>
#include <osai/types.h>

void virtio_net_self_test(void);
osai_status_t virtio_net_init_persistent(void);
osai_status_t virtio_net_tx(const uint8_t *data, uint64_t len);
uint32_t virtio_net_rx_poll(uint8_t *buffer, uint64_t buffer_size);
osai_status_t virtio_net_get_mac(uint8_t mac[6]);

#endif
