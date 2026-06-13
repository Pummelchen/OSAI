#include <osai/assert.h>
#include <osai/klog.h>
#include <osai/virtio_mmio.h>

#define VIRTIO_MMIO_BASE UINT64_C(0x0a000000)
#define VIRTIO_MMIO_STRIDE UINT64_C(0x200)
#define VIRTIO_MMIO_SLOTS 32U
#define VIRTQ_SIZE 8U
#define VIRTIO_SPIN_LIMIT UINT64_C(10000000)

#define VIRTIO_MMIO_MAGIC 0x000U
#define VIRTIO_MMIO_VERSION 0x004U
#define VIRTIO_MMIO_DEVICE_ID 0x008U
#define VIRTIO_MMIO_VENDOR_ID 0x00cU
#define VIRTIO_MMIO_DEVICE_FEATURES_SEL 0x014U
#define VIRTIO_MMIO_DRIVER_FEATURES 0x020U
#define VIRTIO_MMIO_DRIVER_FEATURES_SEL 0x024U
#define VIRTIO_MMIO_QUEUE_SEL 0x030U
#define VIRTIO_MMIO_QUEUE_NUM_MAX 0x034U
#define VIRTIO_MMIO_QUEUE_NUM 0x038U
#define VIRTIO_MMIO_QUEUE_READY 0x044U
#define VIRTIO_MMIO_QUEUE_NOTIFY 0x050U
#define VIRTIO_MMIO_INTERRUPT_STATUS 0x060U
#define VIRTIO_MMIO_INTERRUPT_ACK 0x064U
#define VIRTIO_MMIO_STATUS 0x070U
#define VIRTIO_MMIO_QUEUE_DESC_LOW 0x080U
#define VIRTIO_MMIO_QUEUE_DESC_HIGH 0x084U
#define VIRTIO_MMIO_QUEUE_DRIVER_LOW 0x090U
#define VIRTIO_MMIO_QUEUE_DRIVER_HIGH 0x094U
#define VIRTIO_MMIO_QUEUE_DEVICE_LOW 0x0a0U
#define VIRTIO_MMIO_QUEUE_DEVICE_HIGH 0x0a4U

#define VIRTIO_MAGIC UINT32_C(0x74726976)
#define VIRTIO_DEVICE_BLOCK UINT32_C(2)
#define VIRTIO_DEVICE_NET UINT32_C(1)

#define VIRTIO_STATUS_ACKNOWLEDGE UINT32_C(1)
#define VIRTIO_STATUS_DRIVER UINT32_C(2)
#define VIRTIO_STATUS_DRIVER_OK UINT32_C(4)
#define VIRTIO_STATUS_FEATURES_OK UINT32_C(8)
#define VIRTIO_STATUS_FAILED UINT32_C(128)

#define VRING_DESC_F_NEXT UINT16_C(1)
#define VRING_DESC_F_WRITE UINT16_C(2)

#define VIRTIO_BLK_T_IN UINT32_C(0)

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

typedef struct virtio_blk_req {
  uint32_t type;
  uint32_t reserved;
  uint64_t sector;
} virtio_blk_req_t;

static virtq_desc_t g_blk_desc[VIRTQ_SIZE] __attribute__((aligned(16)));
static virtq_avail_t g_blk_avail __attribute__((aligned(2)));
static virtq_used_t g_blk_used __attribute__((aligned(4)));
static virtio_blk_req_t g_blk_req __attribute__((aligned(16)));
static uint8_t g_blk_sector[512] __attribute__((aligned(16)));
static uint8_t g_blk_status __attribute__((aligned(16)));

static virtq_desc_t g_net_rx_desc[VIRTQ_SIZE] __attribute__((aligned(16)));
static virtq_avail_t g_net_rx_avail __attribute__((aligned(2)));
static virtq_used_t g_net_rx_used __attribute__((aligned(4)));
static virtq_desc_t g_net_tx_desc[VIRTQ_SIZE] __attribute__((aligned(16)));
static virtq_avail_t g_net_tx_avail __attribute__((aligned(2)));
static virtq_used_t g_net_tx_used __attribute__((aligned(4)));
static uint8_t g_net_rx_packet[2048] __attribute__((aligned(16)));
static uint8_t g_net_tx_packet[128] __attribute__((aligned(16)));

static uint32_t mmio_read32(uint64_t base, uint32_t offset) {
  volatile uint32_t *reg = (volatile uint32_t *)(uintptr_t)(base + offset);
  return *reg;
}

static void mmio_write32(uint64_t base, uint32_t offset, uint32_t value) {
  volatile uint32_t *reg = (volatile uint32_t *)(uintptr_t)(base + offset);
  *reg = value;
}

static void mmio_barrier(void) {
  __asm__ volatile("dsb sy" ::: "memory");
}

static uint64_t find_device(uint32_t expected_device_id, const char *log_name) {
  for (uint32_t slot = 0; slot < VIRTIO_MMIO_SLOTS; ++slot) {
    uint64_t base = VIRTIO_MMIO_BASE + (slot * VIRTIO_MMIO_STRIDE);
    uint32_t magic = mmio_read32(base, VIRTIO_MMIO_MAGIC);
    uint32_t version = mmio_read32(base, VIRTIO_MMIO_VERSION);
    uint32_t device_id = mmio_read32(base, VIRTIO_MMIO_DEVICE_ID);
    if (magic == VIRTIO_MAGIC && version >= 2 &&
        device_id == expected_device_id) {
      klog("%s: mmio base=0x%lx version=%u vendor=0x%x\n",
           log_name, base, version, mmio_read32(base, VIRTIO_MMIO_VENDOR_ID));
      return base;
    }
  }

  return 0;
}

static void set_status(uint64_t base, uint32_t status) {
  mmio_write32(base, VIRTIO_MMIO_STATUS, status);
  mmio_barrier();
}

static void negotiate_no_features(uint64_t base) {
  set_status(base, 0);
  set_status(base, VIRTIO_STATUS_ACKNOWLEDGE);
  set_status(base, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);

  mmio_write32(base, VIRTIO_MMIO_DEVICE_FEATURES_SEL, 0);
  mmio_write32(base, VIRTIO_MMIO_DRIVER_FEATURES_SEL, 0);
  mmio_write32(base, VIRTIO_MMIO_DRIVER_FEATURES, 0);
  set_status(base, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER |
                       VIRTIO_STATUS_FEATURES_OK);
  uint32_t status = mmio_read32(base, VIRTIO_MMIO_STATUS);
  if ((status & VIRTIO_STATUS_FEATURES_OK) == 0) {
    set_status(base, status | VIRTIO_STATUS_FAILED);
    kassert(0);
  }
}

static void set_driver_ok(uint64_t base) {
  set_status(base, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER |
                       VIRTIO_STATUS_FEATURES_OK | VIRTIO_STATUS_DRIVER_OK);
}

static void write_addr_pair(uint64_t base, uint32_t low_offset, uint32_t high_offset,
                            uint64_t address) {
  mmio_write32(base, low_offset, (uint32_t)(address & UINT64_C(0xffffffff)));
  mmio_write32(base, high_offset, (uint32_t)(address >> 32U));
}

static void zero_bytes(void *buffer, uint64_t size) {
  uint8_t *bytes = (uint8_t *)buffer;
  for (uint64_t i = 0; i < size; ++i) {
    bytes[i] = 0;
  }
}

static void clear_queue(virtq_desc_t *desc, virtq_avail_t *avail,
                        virtq_used_t *used) {
  zero_bytes(desc, sizeof(virtq_desc_t) * VIRTQ_SIZE);
  zero_bytes(avail, sizeof(*avail));
  zero_bytes(used, sizeof(*used));
}

static void setup_queue(uint64_t base, uint32_t queue_index, uint32_t queue_size,
                        virtq_desc_t *desc, virtq_avail_t *avail,
                        virtq_used_t *used) {
  mmio_write32(base, VIRTIO_MMIO_QUEUE_SEL, queue_index);
  uint32_t queue_max = mmio_read32(base, VIRTIO_MMIO_QUEUE_NUM_MAX);
  kassert(queue_max >= queue_size);
  mmio_write32(base, VIRTIO_MMIO_QUEUE_NUM, queue_size);
  write_addr_pair(base, VIRTIO_MMIO_QUEUE_DESC_LOW, VIRTIO_MMIO_QUEUE_DESC_HIGH,
                  (uint64_t)(uintptr_t)desc);
  write_addr_pair(base, VIRTIO_MMIO_QUEUE_DRIVER_LOW, VIRTIO_MMIO_QUEUE_DRIVER_HIGH,
                  (uint64_t)(uintptr_t)avail);
  write_addr_pair(base, VIRTIO_MMIO_QUEUE_DEVICE_LOW, VIRTIO_MMIO_QUEUE_DEVICE_HIGH,
                  (uint64_t)(uintptr_t)used);
  mmio_write32(base, VIRTIO_MMIO_QUEUE_READY, 1);
}

static void notify_queue(uint64_t base, uint32_t queue_index) {
  mmio_barrier();
  mmio_write32(base, VIRTIO_MMIO_QUEUE_NOTIFY, queue_index);
}

static int wait_used_idx(volatile uint16_t *used_idx, uint16_t expected) {
  for (uint64_t spin = 0; spin < VIRTIO_SPIN_LIMIT; ++spin) {
    if (*used_idx >= expected) {
      return 1;
    }
    __asm__ volatile("yield");
  }
  return 0;
}

static void ack_interrupts(uint64_t base) {
  uint32_t interrupt_status = mmio_read32(base, VIRTIO_MMIO_INTERRUPT_STATUS);
  if (interrupt_status != 0) {
    mmio_write32(base, VIRTIO_MMIO_INTERRUPT_ACK, interrupt_status);
  }
}

static void put_be16(uint8_t *dst, uint16_t value) {
  dst[0] = (uint8_t)(value >> 8U);
  dst[1] = (uint8_t)value;
}

static uint16_t get_be16(const uint8_t *src) {
  return (uint16_t)(((uint16_t)src[0] << 8U) | src[1]);
}

static void build_arp_request(uint8_t *packet, uint64_t *packet_len) {
  static const uint8_t src_mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x15};
  static const uint8_t src_ip[4] = {10, 0, 2, 15};
  static const uint8_t target_ip[4] = {10, 0, 2, 2};
  uint8_t *frame = packet + 10;

  zero_bytes(packet, 10 + 42);
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

void virtio_block_self_test(void) {
  uint64_t base = find_device(VIRTIO_DEVICE_BLOCK, "virtio-blk");
  kassert(base != 0);

  negotiate_no_features(base);
  clear_queue(g_blk_desc, &g_blk_avail, &g_blk_used);
  zero_bytes(g_blk_sector, sizeof(g_blk_sector));
  g_blk_status = 0xffU;

  setup_queue(base, 0, VIRTQ_SIZE, g_blk_desc, &g_blk_avail, &g_blk_used);

  g_blk_req.type = VIRTIO_BLK_T_IN;
  g_blk_req.reserved = 0;
  g_blk_req.sector = 0;
  g_blk_desc[0].addr = (uint64_t)(uintptr_t)&g_blk_req;
  g_blk_desc[0].len = sizeof(g_blk_req);
  g_blk_desc[0].flags = VRING_DESC_F_NEXT;
  g_blk_desc[0].next = 1;
  g_blk_desc[1].addr = (uint64_t)(uintptr_t)g_blk_sector;
  g_blk_desc[1].len = sizeof(g_blk_sector);
  g_blk_desc[1].flags = VRING_DESC_F_NEXT | VRING_DESC_F_WRITE;
  g_blk_desc[1].next = 2;
  g_blk_desc[2].addr = (uint64_t)(uintptr_t)&g_blk_status;
  g_blk_desc[2].len = sizeof(g_blk_status);
  g_blk_desc[2].flags = VRING_DESC_F_WRITE;
  g_blk_desc[2].next = 0;

  g_blk_avail.ring[0] = 0;
  mmio_barrier();
  g_blk_avail.idx = 1;
  notify_queue(base, 0);

  kassert(wait_used_idx(&g_blk_used.idx, 1));
  ack_interrupts(base);
  kassert(g_blk_used.idx == 1);
  kassert(g_blk_status == 0);
  kassert(g_blk_sector[0] == 'O');
  kassert(g_blk_sector[1] == 'S');
  kassert(g_blk_sector[2] == 'A');
  kassert(g_blk_sector[3] == 'I');
  klog("virtio-blk: sector0 magic='%s' status=%u\n",
       (const char *)g_blk_sector, (unsigned)g_blk_status);
  klog("virtio-blk: read self-test passed\n");

  set_driver_ok(base);
}

void virtio_net_self_test(void) {
  uint64_t base = find_device(VIRTIO_DEVICE_NET, "virtio-net");
  kassert(base != 0);

  negotiate_no_features(base);
  clear_queue(g_net_rx_desc, &g_net_rx_avail, &g_net_rx_used);
  clear_queue(g_net_tx_desc, &g_net_tx_avail, &g_net_tx_used);
  zero_bytes(g_net_rx_packet, sizeof(g_net_rx_packet));
  zero_bytes(g_net_tx_packet, sizeof(g_net_tx_packet));

  setup_queue(base, 0, VIRTQ_SIZE, g_net_rx_desc, &g_net_rx_avail, &g_net_rx_used);
  setup_queue(base, 1, VIRTQ_SIZE, g_net_tx_desc, &g_net_tx_avail, &g_net_tx_used);

  set_driver_ok(base);

  g_net_rx_desc[0].addr = (uint64_t)(uintptr_t)g_net_rx_packet;
  g_net_rx_desc[0].len = sizeof(g_net_rx_packet);
  g_net_rx_desc[0].flags = VRING_DESC_F_WRITE;
  g_net_rx_avail.ring[0] = 0;
  mmio_barrier();
  g_net_rx_avail.idx = 1;
  notify_queue(base, 0);

  uint64_t tx_len = 0;
  build_arp_request(g_net_tx_packet, &tx_len);
  g_net_tx_desc[0].addr = (uint64_t)(uintptr_t)g_net_tx_packet;
  g_net_tx_desc[0].len = (uint32_t)tx_len;
  g_net_tx_desc[0].flags = 0;
  g_net_tx_avail.ring[0] = 0;
  mmio_barrier();
  g_net_tx_avail.idx = 1;
  notify_queue(base, 1);

  kassert(wait_used_idx(&g_net_tx_used.idx, 1));
  kassert(wait_used_idx(&g_net_rx_used.idx, 1));
  ack_interrupts(base);

  kassert(g_net_tx_used.idx == 1);
  kassert(g_net_rx_used.idx >= 1);
  uint32_t rx_len = g_net_rx_used.ring[0].len;
  kassert(is_expected_arp_reply(g_net_rx_packet, rx_len));
  klog("virtio-net: arp reply len=%u from=10.0.2.2\n", rx_len);
  klog("virtio-net: rx/tx self-test passed\n");
}
