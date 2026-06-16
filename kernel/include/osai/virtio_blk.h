#ifndef OSAI_VIRTIO_BLK_H
#define OSAI_VIRTIO_BLK_H

#include <osai/status.h>
#include <osai/types.h>

typedef struct virtio_block_driver virtio_block_handle_t;

osai_status_t virtio_block_init(void);
osai_status_t virtio_block_read_sector(uint64_t sector, void *buffer,
                                       uint64_t buffer_size);
osai_status_t virtio_block_write_sector(uint64_t sector, const void *buffer,
                                        uint64_t buffer_size);
uint64_t virtio_block_capacity_sectors(void);
void virtio_block_self_test(void);

osai_status_t virtio_block_open_slot(uint32_t start_slot,
                                     virtio_block_handle_t **out_handle);
osai_status_t virtio_block_read_sector_h(virtio_block_handle_t *handle,
                                         uint64_t sector, void *buffer,
                                         uint64_t buffer_size);
osai_status_t virtio_block_write_sector_h(virtio_block_handle_t *handle,
                                          uint64_t sector, const void *buffer,
                                          uint64_t buffer_size);
uint64_t virtio_block_capacity_sectors_h(virtio_block_handle_t *handle);
void virtio_block_close(virtio_block_handle_t *handle);

#endif
