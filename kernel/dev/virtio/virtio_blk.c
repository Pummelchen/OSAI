#include <osai/assert.h>
#include <osai/kheap.h>
#include <osai/klog.h>
#include <osai/virtio_blk.h>
#include <osai/virtio_transport.h>
#include <osai/vmm.h>

#define VIRTIO_MMIO_CONFIG 0x100U
#define VRING_DESC_F_NEXT UINT16_C(1)
#define VRING_DESC_F_WRITE UINT16_C(2)
#define VIRTIO_BLK_T_IN UINT32_C(0)
#define SECTOR_SIZE UINT64_C(512)

typedef struct virtio_blk_req {
  uint32_t type;
  uint32_t reserved;
  uint64_t sector;
} virtio_blk_req_t;

typedef struct virtio_block_driver {
  virtio_mmio_device_t device;
  virtq_desc_t *desc;
  virtq_avail_t *avail;
  virtq_used_t *used;
  virtio_blk_req_t *request;
  uint8_t *dma_sector;
  uint8_t *status;
  uint16_t next_avail;
  uint64_t capacity_sectors;
  uint32_t initialized;
} virtio_block_driver_t;

static virtio_block_driver_t *g_blk;

static void bytes_zero(void *buffer, uint64_t size) {
  uint8_t *bytes = (uint8_t *)buffer;
  for (uint64_t i = 0; i < size; ++i) {
    bytes[i] = 0;
  }
}

static void bytes_copy(void *dst, const void *src, uint64_t size) {
  uint8_t *out = (uint8_t *)dst;
  const uint8_t *in = (const uint8_t *)src;
  for (uint64_t i = 0; i < size; ++i) {
    out[i] = in[i];
  }
}

static uint64_t dma_address(const void *ptr) {
  uint64_t physical = 0;
  uint32_t flags = 0;
  kassert(vmm_translate((uint64_t)(uintptr_t)ptr, &physical, &flags) == OSAI_OK);
  kassert((flags & OSAI_VMM_PRESENT) != 0);
  return physical;
}

static uint64_t read_capacity(const virtio_mmio_device_t *device) {
  uint32_t low = virtio_mmio_read32(device->base, VIRTIO_MMIO_CONFIG);
  uint32_t high = virtio_mmio_read32(device->base, VIRTIO_MMIO_CONFIG + 4U);
  return ((uint64_t)high << 32U) | low;
}

static osai_status_t allocate_driver(void) {
  if (g_blk != 0) {
    return OSAI_OK;
  }

  g_blk = (virtio_block_driver_t *)kheap_calloc(sizeof(*g_blk), 16);
  if (g_blk == 0) {
    return OSAI_ERR_NO_MEMORY;
  }
  g_blk->desc = (virtq_desc_t *)kheap_calloc(sizeof(virtq_desc_t) * VIRTQ_SIZE, 16);
  g_blk->avail = (virtq_avail_t *)kheap_calloc(sizeof(virtq_avail_t), 2);
  g_blk->used = (virtq_used_t *)kheap_calloc(sizeof(virtq_used_t), 4);
  g_blk->request = (virtio_blk_req_t *)kheap_calloc(sizeof(virtio_blk_req_t), 16);
  g_blk->dma_sector = (uint8_t *)kheap_calloc(SECTOR_SIZE, 16);
  g_blk->status = (uint8_t *)kheap_calloc(1, 1);
  if (g_blk->desc == 0 || g_blk->avail == 0 || g_blk->used == 0 ||
      g_blk->request == 0 || g_blk->dma_sector == 0 || g_blk->status == 0) {
    return OSAI_ERR_NO_MEMORY;
  }
  return OSAI_OK;
}

osai_status_t virtio_block_init(void) {
  if (allocate_driver() != OSAI_OK) {
    return OSAI_ERR_NO_MEMORY;
  }
  if (virtio_transport_find(VIRTIO_DEVICE_BLOCK, "virtio-blk",
                            &g_blk->device) != OSAI_OK) {
    return OSAI_ERR_NOT_FOUND;
  }
  if (virtio_transport_negotiate_no_features(&g_blk->device) != OSAI_OK) {
    return OSAI_ERR_IO;
  }

  bytes_zero(g_blk->desc, sizeof(virtq_desc_t) * VIRTQ_SIZE);
  bytes_zero(g_blk->avail, sizeof(*g_blk->avail));
  bytes_zero(g_blk->used, sizeof(*g_blk->used));
  if (virtio_transport_setup_queue(&g_blk->device, 0, VIRTQ_SIZE, g_blk->desc,
                                   g_blk->avail, g_blk->used) != OSAI_OK) {
    return OSAI_ERR_IO;
  }

  g_blk->capacity_sectors = read_capacity(&g_blk->device);
  g_blk->next_avail = 0;
  g_blk->initialized = 1;
  virtio_transport_set_driver_ok(&g_blk->device);
  klog("virtio-blk: capacity_sectors=%lu\n", g_blk->capacity_sectors);
  return OSAI_OK;
}

uint64_t virtio_block_capacity_sectors(void) {
  if (g_blk == 0 || g_blk->initialized == 0) {
    return 0;
  }
  return g_blk->capacity_sectors;
}

osai_status_t virtio_block_read_sector(uint64_t sector, void *buffer,
                                       uint64_t buffer_size) {
  if (g_blk == 0 || g_blk->initialized == 0 || buffer == 0 ||
      buffer_size < SECTOR_SIZE) {
    return OSAI_ERR_INVALID;
  }
  if (sector >= g_blk->capacity_sectors) {
    return OSAI_ERR_IO;
  }

  bytes_zero(g_blk->desc, sizeof(virtq_desc_t) * VIRTQ_SIZE);
  bytes_zero(g_blk->dma_sector, SECTOR_SIZE);
  *g_blk->status = 0xffU;

  g_blk->request->type = VIRTIO_BLK_T_IN;
  g_blk->request->reserved = 0;
  g_blk->request->sector = sector;
  g_blk->desc[0].addr = dma_address(g_blk->request);
  g_blk->desc[0].len = sizeof(*g_blk->request);
  g_blk->desc[0].flags = VRING_DESC_F_NEXT;
  g_blk->desc[0].next = 1;
  g_blk->desc[1].addr = dma_address(g_blk->dma_sector);
  g_blk->desc[1].len = SECTOR_SIZE;
  g_blk->desc[1].flags = VRING_DESC_F_NEXT | VRING_DESC_F_WRITE;
  g_blk->desc[1].next = 2;
  g_blk->desc[2].addr = dma_address(g_blk->status);
  g_blk->desc[2].len = 1;
  g_blk->desc[2].flags = VRING_DESC_F_WRITE;
  g_blk->desc[2].next = 0;

  uint16_t used_target = (uint16_t)(g_blk->used->idx + 1U);
  g_blk->avail->ring[g_blk->next_avail % VIRTQ_SIZE] = 0;
  virtio_mmio_barrier();
  ++g_blk->next_avail;
  g_blk->avail->idx = g_blk->next_avail;
  virtio_transport_notify(&g_blk->device, 0);

  if (virtio_transport_wait_used(&g_blk->used->idx, used_target) != OSAI_OK) {
    return OSAI_ERR_IO;
  }
  virtio_transport_ack_interrupts(&g_blk->device);
  if (*g_blk->status != 0) {
    return OSAI_ERR_IO;
  }

  bytes_copy(buffer, g_blk->dma_sector, SECTOR_SIZE);
  return OSAI_OK;
}

void virtio_block_self_test(void) {
  uint8_t *sector = (uint8_t *)kheap_calloc(SECTOR_SIZE, 16);
  kassert(sector != 0);
  kassert(virtio_block_init() == OSAI_OK);
  kassert(virtio_block_read_sector(0, sector, SECTOR_SIZE) == OSAI_OK);
  kassert(sector[0] == 'O');
  kassert(sector[1] == 'S');
  kassert(sector[2] == 'A');
  kassert(sector[3] == 'I');
  kassert(virtio_block_read_sector(virtio_block_capacity_sectors(), sector,
                                   SECTOR_SIZE) == OSAI_ERR_IO);
  virtio_transport_reset(&g_blk->device);
  kassert(virtio_block_init() == OSAI_OK);
  klog("virtio-blk: sector0 magic='%s'\n", (const char *)sector);
  klog("virtio-blk: read/error/reset self-test passed\n");
}
