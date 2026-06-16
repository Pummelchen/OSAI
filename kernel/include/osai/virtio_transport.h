#ifndef OSAI_VIRTIO_TRANSPORT_H
#define OSAI_VIRTIO_TRANSPORT_H

#include <osai/status.h>
#include <osai/types.h>

#define VIRTQ_SIZE 8U

#define VIRTIO_DEVICE_NET UINT32_C(1)
#define VIRTIO_DEVICE_BLOCK UINT32_C(2)

typedef struct virtq_desc {
  uint64_t addr;
  uint32_t len;
  uint16_t flags;
  uint16_t next;
} virtq_desc_t;

typedef struct virtq_avail {
  uint16_t flags;
  uint16_t idx;
  uint16_t ring[VIRTQ_SIZE];
} virtq_avail_t;

typedef struct virtq_used_elem {
  uint32_t id;
  uint32_t len;
} virtq_used_elem_t;

typedef struct virtq_used {
  uint16_t flags;
  uint16_t idx;
  virtq_used_elem_t ring[VIRTQ_SIZE];
} virtq_used_t;

typedef struct virtio_mmio_device {
  uint64_t base;
  uint32_t device_id;
  const char *name;
} virtio_mmio_device_t;

uint32_t virtio_mmio_read32(uint64_t base, uint32_t offset);
void virtio_mmio_write32(uint64_t base, uint32_t offset, uint32_t value);
void virtio_mmio_barrier(void);
osai_status_t virtio_transport_find(uint32_t device_id, const char *name,
                                    virtio_mmio_device_t *device);
osai_status_t virtio_transport_find_from(uint32_t device_id, const char *name,
                                         uint32_t start_slot,
                                         virtio_mmio_device_t *device);
void virtio_transport_reset(const virtio_mmio_device_t *device);
osai_status_t virtio_transport_negotiate_no_features(
    const virtio_mmio_device_t *device);
osai_status_t virtio_transport_setup_queue(const virtio_mmio_device_t *device,
                                           uint32_t queue_index,
                                           uint32_t queue_size,
                                           virtq_desc_t *desc,
                                           virtq_avail_t *avail,
                                           virtq_used_t *used);
void virtio_transport_set_driver_ok(const virtio_mmio_device_t *device);
void virtio_transport_notify(const virtio_mmio_device_t *device,
                             uint32_t queue_index);
osai_status_t virtio_transport_wait_used(volatile uint16_t *used_idx,
                                         uint16_t expected);
void virtio_transport_ack_interrupts(const virtio_mmio_device_t *device);

#endif
