#include <osai/assert.h>
#include <osai/initramfs.h>
#include <osai/kheap.h>
#include <osai/klog.h>
#include <osai/virtio_blk.h>

#define INITFS_SECTOR UINT64_C(1)
#define INITFS_MAGIC "OSAIINITFS1"
#define INITFS_MAGIC_LEN 11U
#define INITFS_MAX_FILES 4U
#define INITFS_PATH_MAX 56U
#define SECTOR_SIZE UINT64_C(512)

typedef struct initfs_disk_entry {
  char path[INITFS_PATH_MAX];
  uint64_t offset;
  uint64_t size;
  uint32_t flags;
  uint32_t reserved;
} initfs_disk_entry_t;

typedef struct initfs_disk_header {
  char magic[16];
  uint32_t version;
  uint32_t entry_count;
  initfs_disk_entry_t entries[INITFS_MAX_FILES];
} initfs_disk_header_t;

static osai_initramfs_file_t g_files[INITFS_MAX_FILES];
static char g_file_paths[INITFS_MAX_FILES][INITFS_PATH_MAX];
static uint32_t g_file_count;

static int str_eq(const char *a, const char *b) {
  while (*a != '\0' && *b != '\0') {
    if (*a != *b) {
      return 0;
    }
    ++a;
    ++b;
  }
  return *a == '\0' && *b == '\0';
}

static int magic_ok(const char *magic) {
  for (uint32_t i = 0; i < INITFS_MAGIC_LEN; ++i) {
    if (magic[i] != INITFS_MAGIC[i]) {
      return 0;
    }
  }
  return 1;
}

static void bytes_copy(void *dst, const void *src, uint64_t size) {
  uint8_t *out = (uint8_t *)dst;
  const uint8_t *in = (const uint8_t *)src;
  for (uint64_t i = 0; i < size; ++i) {
    out[i] = in[i];
  }
}

static void copy_path(char *dst, const char *src) {
  for (uint32_t i = 0; i < INITFS_PATH_MAX; ++i) {
    dst[i] = src[i];
    if (src[i] == '\0') {
      return;
    }
  }
  dst[INITFS_PATH_MAX - 1U] = '\0';
}

static osai_status_t read_file_bytes(uint64_t offset, uint64_t size,
                                     void *buffer) {
  uint8_t sector[SECTOR_SIZE];
  uint8_t *out = (uint8_t *)buffer;
  uint64_t copied = 0;

  while (copied < size) {
    uint64_t absolute = offset + copied;
    uint64_t sector_index = absolute / SECTOR_SIZE;
    uint64_t sector_offset = absolute % SECTOR_SIZE;
    uint64_t chunk = SECTOR_SIZE - sector_offset;
    if (chunk > size - copied) {
      chunk = size - copied;
    }
    if (virtio_block_read_sector(sector_index, sector, sizeof(sector)) !=
        OSAI_OK) {
      return OSAI_ERR_IO;
    }
    bytes_copy(out + copied, sector + sector_offset, chunk);
    copied += chunk;
  }

  return OSAI_OK;
}

osai_status_t initramfs_init(void) {
  uint8_t sector[SECTOR_SIZE];
  if (virtio_block_read_sector(INITFS_SECTOR, sector, sizeof(sector)) !=
      OSAI_OK) {
    klog("initramfs: failed to read header sector=%lu\n", INITFS_SECTOR);
    return OSAI_ERR_IO;
  }

  const initfs_disk_header_t *header =
      (const initfs_disk_header_t *)(const void *)sector;
  if (!magic_ok(header->magic) || header->version != 1 ||
      header->entry_count == 0 || header->entry_count > INITFS_MAX_FILES) {
    klog("initramfs: bad header magic='%c%c%c%c' version=%u entries=%u\n",
         header->magic[0], header->magic[1], header->magic[2],
         header->magic[3], header->version, header->entry_count);
    return OSAI_ERR_INVALID;
  }

  g_file_count = 0;
  for (uint32_t i = 0; i < header->entry_count; ++i) {
    const initfs_disk_entry_t *entry = &header->entries[i];
    if (entry->path[0] == '\0' || entry->size == 0 ||
        entry->offset < (INITFS_SECTOR + 1U) * SECTOR_SIZE) {
      klog("initramfs: bad entry index=%u path0=0x%x offset=%lu size=%lu\n",
           i, (unsigned)entry->path[0], entry->offset, entry->size);
      return OSAI_ERR_INVALID;
    }

    void *content = kheap_alloc(entry->size, 16);
    if (content == 0) {
      klog("initramfs: allocation failed path=%s size=%lu\n",
           entry->path, entry->size);
      return OSAI_ERR_NO_MEMORY;
    }
    if (read_file_bytes(entry->offset, entry->size, content) != OSAI_OK) {
      klog("initramfs: failed to read path=%s offset=%lu size=%lu\n",
           entry->path, entry->offset, entry->size);
      return OSAI_ERR_IO;
    }

    copy_path(g_file_paths[g_file_count], entry->path);
    g_files[g_file_count].path = g_file_paths[g_file_count];
    g_files[g_file_count].base = content;
    g_files[g_file_count].size = entry->size;
    g_files[g_file_count].executable = (entry->flags & 1U) != 0;
    ++g_file_count;
  }

  klog("initramfs: mounted files=%u source=virtio-blk\n", g_file_count);
  return OSAI_OK;
}

osai_status_t initramfs_lookup(const char *path,
                               const osai_initramfs_file_t **file) {
  if (path == 0 || file == 0) {
    return OSAI_ERR_INVALID;
  }
  for (uint32_t i = 0; i < g_file_count; ++i) {
    if (str_eq(path, g_files[i].path)) {
      *file = &g_files[i];
      return OSAI_OK;
    }
  }
  return OSAI_ERR_NOT_FOUND;
}

void initramfs_self_test(void) {
  kassert(initramfs_init() == OSAI_OK);
  const osai_initramfs_file_t *init = 0;
  const osai_initramfs_file_t *config = 0;
  kassert(initramfs_lookup("/init", &init) == OSAI_OK);
  kassert(init != 0);
  kassert(init->base != 0);
  kassert(init->size != 0);
  kassert(init->executable != 0);
  kassert(initramfs_lookup("/etc/osai-init.conf", &config) == OSAI_OK);
  kassert(config != 0);
  kassert(config->base != 0);
  kassert(config->size != 0);
  kassert(initramfs_lookup("/missing", &init) == OSAI_ERR_NOT_FOUND);
  klog("initramfs: virtio lookup self-test passed\n");
}
