#include <osai/assert.h>
#include <osai/kheap.h>
#include <osai/klog.h>
#include <osai/virtio_net.h>
#include <osai/virtio_transport.h>
#include <osai/vmm.h>

#define VRING_DESC_F_WRITE UINT16_C(2)

typedef struct virtio_net_driver {
  virtio_mmio_device_t device;
  virtq_desc_t *rx_desc;
  virtq_avail_t *rx_avail;
  virtq_used_t *rx_used;
  virtq_desc_t *tx_desc;
  virtq_avail_t *tx_avail;
  virtq_used_t *tx_used;
  uint8_t *rx_packet;
  uint8_t *tx_packet;
} virtio_net_driver_t;

static virtio_net_driver_t *g_net;

static void bytes_zero(void *buffer, uint64_t size) {
  uint8_t *bytes = (uint8_t *)buffer;
  for (uint64_t i = 0; i < size; ++i) {
    bytes[i] = 0;
  }
}

static void put_be16(uint8_t *dst, uint16_t value) {
  dst[0] = (uint8_t)(value >> 8U);
  dst[1] = (uint8_t)value;
}

static uint64_t dma_address(const void *ptr) {
  uint64_t physical = 0;
  uint32_t flags = 0;
  kassert(vmm_translate((uint64_t)(uintptr_t)ptr, &physical, &flags) == OSAI_OK);
  kassert((flags & OSAI_VMM_PRESENT) != 0);
  return physical;
}

static uint16_t get_be16(const uint8_t *src) {
  return (uint16_t)(((uint16_t)src[0] << 8U) | src[1]);
}

static osai_status_t allocate_driver(void) {
  if (g_net != 0) {
    return OSAI_OK;
  }
  g_net = (virtio_net_driver_t *)kheap_calloc(sizeof(*g_net), 16);
  if (g_net == 0) {
    return OSAI_ERR_NO_MEMORY;
  }
  g_net->rx_desc = (virtq_desc_t *)kheap_calloc(sizeof(virtq_desc_t) * VIRTQ_SIZE, 16);
  g_net->rx_avail = (virtq_avail_t *)kheap_calloc(sizeof(virtq_avail_t), 2);
  g_net->rx_used = (virtq_used_t *)kheap_calloc(sizeof(virtq_used_t), 4);
  g_net->tx_desc = (virtq_desc_t *)kheap_calloc(sizeof(virtq_desc_t) * VIRTQ_SIZE, 16);
  g_net->tx_avail = (virtq_avail_t *)kheap_calloc(sizeof(virtq_avail_t), 2);
  g_net->tx_used = (virtq_used_t *)kheap_calloc(sizeof(virtq_used_t), 4);
  g_net->rx_packet = (uint8_t *)kheap_calloc(2048, 16);
  g_net->tx_packet = (uint8_t *)kheap_calloc(128, 16);
  if (g_net->rx_desc == 0 || g_net->rx_avail == 0 || g_net->rx_used == 0 ||
      g_net->tx_desc == 0 || g_net->tx_avail == 0 || g_net->tx_used == 0 ||
      g_net->rx_packet == 0 || g_net->tx_packet == 0) {
    return OSAI_ERR_NO_MEMORY;
  }
  return OSAI_OK;
}

static void build_arp_request(uint8_t *packet, uint64_t *packet_len) {
  static const uint8_t src_mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x15};
  static const uint8_t src_ip[4] = {10, 0, 2, 15};
  static const uint8_t target_ip[4] = {10, 0, 2, 2};
  uint8_t *frame = packet + 10;

  bytes_zero(packet, 10 + 42);
  for (uint32_t i = 0; i < 6; ++i) {
    frame[i] = 0xff;
    frame[6 + i] = src_mac[i];
  }
  put_be16(frame + 12, 0x0806);
  put_be16(frame + 14, 1);
  put_be16(frame + 16, 0x0800);
  frame[18] = 6;
  frame[19] = 4;
  put_be16(frame + 20, 1);
  for (uint32_t i = 0; i < 6; ++i) {
    frame[22 + i] = src_mac[i];
  }
  for (uint32_t i = 0; i < 4; ++i) {
    frame[28 + i] = src_ip[i];
    frame[38 + i] = target_ip[i];
  }
  *packet_len = 10 + 42;
}

static int is_expected_arp_reply(const uint8_t *packet, uint32_t len) {
  if (len < 10 + 42) {
    return 0;
  }

  const uint8_t *frame = packet + 10;
  if (get_be16(frame + 12) != 0x0806) {
    return 0;
  }
  if (get_be16(frame + 20) != 2) {
    return 0;
  }
  if (frame[28] != 10 || frame[29] != 0 || frame[30] != 2 || frame[31] != 2) {
    return 0;
  }
  if (frame[38] != 10 || frame[39] != 0 || frame[40] != 2 || frame[41] != 15) {
    return 0;
  }

  return 1;
}

static void malformed_packet_self_test(void) {
  uint8_t packet[52];
  uint64_t len = 0;
  build_arp_request(packet, &len);
  kassert(is_expected_arp_reply(packet, 8) == 0);
  put_be16(packet + 10 + 12, 0x0800);
  kassert(is_expected_arp_reply(packet, (uint32_t)len) == 0);
  klog("virtio-net: malformed packet/drop self-test passed\n");
}

void virtio_net_self_test(void) {
  kassert(allocate_driver() == OSAI_OK);
  kassert(virtio_transport_find(VIRTIO_DEVICE_NET, "virtio-net",
                                &g_net->device) == OSAI_OK);
  kassert(virtio_transport_negotiate_no_features(&g_net->device) == OSAI_OK);

  bytes_zero(g_net->rx_desc, sizeof(virtq_desc_t) * VIRTQ_SIZE);
  bytes_zero(g_net->rx_avail, sizeof(*g_net->rx_avail));
  bytes_zero(g_net->rx_used, sizeof(*g_net->rx_used));
  bytes_zero(g_net->tx_desc, sizeof(virtq_desc_t) * VIRTQ_SIZE);
  bytes_zero(g_net->tx_avail, sizeof(*g_net->tx_avail));
  bytes_zero(g_net->tx_used, sizeof(*g_net->tx_used));
  bytes_zero(g_net->rx_packet, 2048);
  bytes_zero(g_net->tx_packet, 128);

  kassert(virtio_transport_setup_queue(&g_net->device, 0, VIRTQ_SIZE,
                                       g_net->rx_desc, g_net->rx_avail,
                                       g_net->rx_used) == OSAI_OK);
  kassert(virtio_transport_setup_queue(&g_net->device, 1, VIRTQ_SIZE,
                                       g_net->tx_desc, g_net->tx_avail,
                                       g_net->tx_used) == OSAI_OK);
  virtio_transport_set_driver_ok(&g_net->device);

  g_net->rx_desc[0].addr = dma_address(g_net->rx_packet);
  g_net->rx_desc[0].len = 2048;
  g_net->rx_desc[0].flags = VRING_DESC_F_WRITE;
  g_net->rx_avail->ring[0] = 0;
  virtio_mmio_barrier();
  g_net->rx_avail->idx = 1;
  virtio_transport_notify(&g_net->device, 0);

  uint64_t tx_len = 0;
  build_arp_request(g_net->tx_packet, &tx_len);
  g_net->tx_desc[0].addr = dma_address(g_net->tx_packet);
  g_net->tx_desc[0].len = (uint32_t)tx_len;
  g_net->tx_desc[0].flags = 0;
  g_net->tx_avail->ring[0] = 0;
  virtio_mmio_barrier();
  g_net->tx_avail->idx = 1;
  virtio_transport_notify(&g_net->device, 1);

  kassert(virtio_transport_wait_used(&g_net->tx_used->idx, 1) == OSAI_OK);
  kassert(virtio_transport_wait_used(&g_net->rx_used->idx, 1) == OSAI_OK);
  virtio_transport_ack_interrupts(&g_net->device);

  uint32_t rx_len = g_net->rx_used->ring[0].len;
  kassert(is_expected_arp_reply(g_net->rx_packet, rx_len));
  malformed_packet_self_test();
  virtio_transport_reset(&g_net->device);
  klog("virtio-net: arp reply len=%u from=10.0.2.2\n", rx_len);
  klog("virtio-net: rx/tx/reset self-test passed\n");
}
