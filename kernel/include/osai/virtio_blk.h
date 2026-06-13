#ifndef OSAI_VIRTIO_BLK_H
#define OSAI_VIRTIO_BLK_H

#include <osai/status.h>
#include <osai/types.h>

osai_status_t virtio_block_init(void);
osai_status_t virtio_block_read_sector(uint64_t sector, void *buffer,
                                       uint64_t buffer_size);
uint64_t virtio_block_capacity_sectors(void);
void virtio_block_self_test(void);

#endif
