#include <osai/assert.h>
#include <osai/klog.h>
#include <osai/mutable_fs.h>
#include <osai/virtio_blk.h>

#define MFS_MAGIC "OSAIMFS1"
#define MFS_MAGIC_LEN 8U
#define MFS_VERSION 1U
#define MFS_SECTOR_SIZE UINT64_C(512)
#define MFS_START_SECTOR UINT64_C(1600)
#define MFS_METADATA_SECTORS UINT64_C(4)
#define MFS_DATA_START_SECTOR (MFS_START_SECTOR + MFS_METADATA_SECTORS)
#define MFS_MAX_FILES 8U
#define MFS_PATH_MAX 64U
#define MFS_FILE_BYTES 512U
#define MFS_MOUNT_READ_WRITE 1U
#define FNV1A64_OFFSET UINT64_C(14695981039346656037)
#define FNV1A64_PRIME UINT64_C(1099511628211)

typedef struct osai_mfs_entry {
  uint32_t active;
  uint32_t snapshot_active;
  uint32_t deleted;
  uint32_t reserved;
  uint64_t data_sector;
  uint64_t snapshot_sector;
  uint64_t size;
  uint64_t content_hash;
  uint64_t generation;
  uint64_t snapshot_size;
  uint64_t snapshot_hash;
  uint64_t snapshot_generation;
  char path[MFS_PATH_MAX];
} osai_mfs_entry_t;

typedef struct osai_mfs_disk {
  char magic[8];
  uint32_t version;
  uint32_t sector_size;
  uint32_t metadata_sectors;
  uint32_t max_files;
  uint64_t start_sector;
  uint64_t data_start_sector;
  uint64_t data_sectors;
  uint64_t generation;
  uint64_t committed_generation;
  uint64_t checksum;
  osai_mfs_entry_t entries[MFS_MAX_FILES];
  uint8_t padding[824];
} osai_mfs_disk_t;

static osai_mfs_disk_t g_mfs;
static uint32_t g_mounted;
static uint32_t g_mount_flags;
static uint64_t g_mount_count;
static uint64_t g_format_count;
static uint64_t g_boot_load_count;
static uint64_t g_write_count;
static uint64_t g_read_count;
static uint64_t g_delete_count;
static uint64_t g_commit_count;
static uint64_t g_rollback_count;
static uint64_t g_reject_count;
static uint64_t g_checksum_error_count;

static const char k_service_v1[] =
    "service=/svc/source-index\nstate=running\n";
static const char k_service_v2[] =
    "service=/svc/source-index\nstate=restarting\n";
static const char k_config_v1[] = "mode=qemu-full-os\nmutable=true\n";
static const char k_boot_log[] = "boot=ok\n";

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

static int bytes_eq(const void *a, const void *b, uint64_t size) {
  const uint8_t *left = (const uint8_t *)a;
  const uint8_t *right = (const uint8_t *)b;
  for (uint64_t i = 0; i < size; ++i) {
    if (left[i] != right[i]) {
      return 0;
    }
  }
  return 1;
}

static uint64_t fnv1a64(const void *buffer, uint64_t size) {
  const uint8_t *bytes = (const uint8_t *)buffer;
  uint64_t hash = FNV1A64_OFFSET;
  for (uint64_t i = 0; i < size; ++i) {
    hash ^= bytes[i];
    hash *= FNV1A64_PRIME;
  }
  return hash;
}

static uint64_t disk_checksum(osai_mfs_disk_t *disk) {
  uint64_t saved = disk->checksum;
  disk->checksum = 0;
  uint64_t checksum = fnv1a64(disk, sizeof(*disk));
  disk->checksum = saved;
  return checksum;
}

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

static int path_prefix(const char *path, const char *prefix) {
  while (*prefix != '\0') {
    if (*path != *prefix) {
      return 0;
    }
    ++path;
    ++prefix;
  }
  return 1;
}

static osai_status_t validate_path(const char *path) {
  if (path == 0 || path[0] != '/') {
    return OSAI_ERR_INVALID;
  }
  if (!path_prefix(path, "/state/") && !path_prefix(path, "/config/") &&
      !path_prefix(path, "/logs/")) {
    return OSAI_ERR_INVALID;
  }
  for (uint32_t i = 0; i < MFS_PATH_MAX; ++i) {
    char c = path[i];
    if (c == '\0') {
      return i > 1 ? OSAI_OK : OSAI_ERR_INVALID;
    }
    if (c < '!' || c > '~') {
      return OSAI_ERR_INVALID;
    }
  }
  return OSAI_ERR_INVALID;
}

static void copy_path(char dst[MFS_PATH_MAX], const char *src) {
  uint32_t i = 0;
  while (i + 1U < MFS_PATH_MAX && src[i] != '\0') {
    dst[i] = src[i];
    ++i;
  }
  dst[i] = '\0';
}

static uint64_t file_count(void) {
  uint64_t count = 0;
  for (uint32_t i = 0; i < MFS_MAX_FILES; ++i) {
    if (g_mfs.entries[i].active != 0) {
      ++count;
    }
  }
  return count;
}

static osai_mfs_entry_t *find_entry(const char *path, uint32_t include_deleted) {
  for (uint32_t i = 0; i < MFS_MAX_FILES; ++i) {
    if ((g_mfs.entries[i].active != 0 ||
         (include_deleted != 0 && g_mfs.entries[i].snapshot_active != 0)) &&
        str_eq(g_mfs.entries[i].path, path)) {
      return &g_mfs.entries[i];
    }
  }
  return 0;
}

static osai_mfs_entry_t *find_free_entry(void) {
  for (uint32_t i = 0; i < MFS_MAX_FILES; ++i) {
    if (g_mfs.entries[i].active == 0 &&
        g_mfs.entries[i].snapshot_active == 0) {
      return &g_mfs.entries[i];
    }
  }
  for (uint32_t i = 0; i < MFS_MAX_FILES; ++i) {
    if (g_mfs.entries[i].active == 0) {
      return &g_mfs.entries[i];
    }
  }
  return 0;
}

static osai_status_t read_metadata(osai_mfs_disk_t *disk) {
  uint8_t *bytes = (uint8_t *)disk;
  for (uint64_t i = 0; i < MFS_METADATA_SECTORS; ++i) {
    if (virtio_block_read_sector(MFS_START_SECTOR + i,
                                 bytes + i * MFS_SECTOR_SIZE,
                                 MFS_SECTOR_SIZE) != OSAI_OK) {
      return OSAI_ERR_IO;
    }
  }
  return OSAI_OK;
}

static osai_status_t write_metadata(void) {
  g_mfs.checksum = disk_checksum(&g_mfs);
  const uint8_t *bytes = (const uint8_t *)&g_mfs;
  for (uint64_t i = 0; i < MFS_METADATA_SECTORS; ++i) {
    if (virtio_block_write_sector(MFS_START_SECTOR + i,
                                  bytes + i * MFS_SECTOR_SIZE,
                                  MFS_SECTOR_SIZE) != OSAI_OK) {
      ++g_reject_count;
      return OSAI_ERR_IO;
    }
  }
  return OSAI_OK;
}

static osai_status_t validate_disk(osai_mfs_disk_t *disk) {
  if (!bytes_eq(disk->magic, MFS_MAGIC, MFS_MAGIC_LEN) ||
      disk->version != MFS_VERSION ||
      disk->sector_size != MFS_SECTOR_SIZE ||
      disk->metadata_sectors != MFS_METADATA_SECTORS ||
      disk->max_files != MFS_MAX_FILES ||
      disk->start_sector != MFS_START_SECTOR ||
      disk->data_start_sector != MFS_DATA_START_SECTOR ||
      disk->data_sectors != MFS_MAX_FILES * 2U) {
    return OSAI_ERR_INVALID;
  }
  uint64_t expected = disk->checksum;
  uint64_t actual = disk_checksum(disk);
  if (actual != expected) {
    ++g_checksum_error_count;
    return OSAI_ERR_INVALID;
  }
  for (uint32_t i = 0; i < MFS_MAX_FILES; ++i) {
    osai_mfs_entry_t *entry = &disk->entries[i];
    uint64_t expected_data = MFS_DATA_START_SECTOR + i;
    uint64_t expected_snapshot = MFS_DATA_START_SECTOR + MFS_MAX_FILES + i;
    if (entry->data_sector != 0 && entry->data_sector != expected_data) {
      return OSAI_ERR_INVALID;
    }
    if (entry->snapshot_sector != 0 &&
        entry->snapshot_sector != expected_snapshot) {
      return OSAI_ERR_INVALID;
    }
    if ((entry->active != 0 || entry->snapshot_active != 0) &&
        validate_path(entry->path) != OSAI_OK) {
      return OSAI_ERR_INVALID;
    }
    if (entry->size > MFS_FILE_BYTES ||
        entry->snapshot_size > MFS_FILE_BYTES) {
      return OSAI_ERR_INVALID;
    }
  }
  return OSAI_OK;
}

static osai_status_t format_volume(void) {
  bytes_zero(&g_mfs, sizeof(g_mfs));
  bytes_copy(g_mfs.magic, MFS_MAGIC, MFS_MAGIC_LEN);
  g_mfs.version = MFS_VERSION;
  g_mfs.sector_size = (uint32_t)MFS_SECTOR_SIZE;
  g_mfs.metadata_sectors = (uint32_t)MFS_METADATA_SECTORS;
  g_mfs.max_files = MFS_MAX_FILES;
  g_mfs.start_sector = MFS_START_SECTOR;
  g_mfs.data_start_sector = MFS_DATA_START_SECTOR;
  g_mfs.data_sectors = MFS_MAX_FILES * 2U;
  g_mfs.generation = 1;
  g_mfs.committed_generation = 0;
  for (uint32_t i = 0; i < MFS_MAX_FILES; ++i) {
    g_mfs.entries[i].data_sector = MFS_DATA_START_SECTOR + i;
    g_mfs.entries[i].snapshot_sector = MFS_DATA_START_SECTOR + MFS_MAX_FILES + i;
  }
  ++g_format_count;
  return write_metadata();
}

static osai_status_t mount_volume(uint32_t mount_flags) {
  if (virtio_block_capacity_sectors() <
      MFS_DATA_START_SECTOR + (MFS_MAX_FILES * 2U)) {
    ++g_reject_count;
    return OSAI_ERR_IO;
  }

  g_mount_flags = mount_flags;
  osai_mfs_disk_t disk;
  bytes_zero(&disk, sizeof(disk));
  if (read_metadata(&disk) != OSAI_OK) {
    ++g_reject_count;
    return OSAI_ERR_IO;
  }
  if (validate_disk(&disk) == OSAI_OK) {
    bytes_copy(&g_mfs, &disk, sizeof(g_mfs));
    ++g_boot_load_count;
    klog("mutable-fs: existing state loaded files=%lu generation=%lu committed=%lu\n",
         file_count(), g_mfs.generation, g_mfs.committed_generation);
  } else {
    klog("mutable-fs: no valid filesystem at sector=%lu; formatting\n",
         MFS_START_SECTOR);
    if (format_volume() != OSAI_OK) {
      return OSAI_ERR_IO;
    }
  }

  g_mounted = 1;
  ++g_mount_count;
  klog("mutable-fs: mounted start=%lu metadata=%lu data=%lu sectors=%u policy=%s\n",
       MFS_START_SECTOR, MFS_METADATA_SECTORS, MFS_DATA_START_SECTOR,
       MFS_MAX_FILES * 2U,
       (g_mount_flags & MFS_MOUNT_READ_WRITE) != 0 ? "rw" : "ro");
  return OSAI_OK;
}

static osai_status_t write_file(const char *path, const void *data,
                                uint64_t size) {
  if (g_mounted == 0 || (g_mount_flags & MFS_MOUNT_READ_WRITE) == 0 ||
      validate_path(path) != OSAI_OK || data == 0 || size == 0 ||
      size > MFS_FILE_BYTES) {
    ++g_reject_count;
    return OSAI_ERR_INVALID;
  }

  osai_mfs_entry_t *entry = find_entry(path, 1);
  if (entry == 0) {
    entry = find_free_entry();
  }
  if (entry == 0) {
    ++g_reject_count;
    return OSAI_ERR_NO_MEMORY;
  }

  uint8_t sector[MFS_FILE_BYTES];
  bytes_zero(sector, sizeof(sector));
  bytes_copy(sector, data, size);
  if (virtio_block_write_sector(entry->data_sector, sector, sizeof(sector)) !=
      OSAI_OK) {
    ++g_reject_count;
    return OSAI_ERR_IO;
  }

  entry->active = 1;
  entry->deleted = 0;
  copy_path(entry->path, path);
  entry->size = size;
  entry->content_hash = fnv1a64(sector, size);
  entry->generation = g_mfs.generation++;
  ++g_write_count;
  klog("mutable-fs: write path=%s size=%lu generation=%lu\n",
       entry->path, entry->size, entry->generation);
  return write_metadata();
}

static osai_status_t read_file(const char *path, void *buffer,
                               uint64_t buffer_size, uint64_t *out_size) {
  if (g_mounted == 0 || validate_path(path) != OSAI_OK || buffer == 0 ||
      out_size == 0) {
    ++g_reject_count;
    return OSAI_ERR_INVALID;
  }
  osai_mfs_entry_t *entry = find_entry(path, 0);
  if (entry == 0 || entry->active == 0 || entry->size > buffer_size) {
    ++g_reject_count;
    return OSAI_ERR_NOT_FOUND;
  }

  uint8_t sector[MFS_FILE_BYTES];
  if (virtio_block_read_sector(entry->data_sector, sector, sizeof(sector)) !=
      OSAI_OK) {
    ++g_reject_count;
    return OSAI_ERR_IO;
  }
  uint64_t hash = fnv1a64(sector, entry->size);
  if (hash != entry->content_hash) {
    ++g_checksum_error_count;
    ++g_reject_count;
    return OSAI_ERR_INVALID;
  }
  bytes_copy(buffer, sector, entry->size);
  *out_size = entry->size;
  ++g_read_count;
  klog("mutable-fs: read path=%s size=%lu generation=%lu\n",
       entry->path, entry->size, entry->generation);
  return OSAI_OK;
}

static osai_status_t delete_file(const char *path) {
  if (g_mounted == 0 || (g_mount_flags & MFS_MOUNT_READ_WRITE) == 0 ||
      validate_path(path) != OSAI_OK) {
    ++g_reject_count;
    return OSAI_ERR_INVALID;
  }
  osai_mfs_entry_t *entry = find_entry(path, 0);
  if (entry == 0) {
    ++g_reject_count;
    return OSAI_ERR_NOT_FOUND;
  }
  entry->active = 0;
  entry->deleted = 1;
  entry->generation = g_mfs.generation++;
  ++g_delete_count;
  klog("mutable-fs: delete path=%s generation=%lu\n",
       entry->path, entry->generation);
  return write_metadata();
}

static osai_status_t commit_snapshot(const char *label) {
  (void)label;
  if (g_mounted == 0 || (g_mount_flags & MFS_MOUNT_READ_WRITE) == 0) {
    ++g_reject_count;
    return OSAI_ERR_INVALID;
  }
  uint8_t sector[MFS_FILE_BYTES];
  for (uint32_t i = 0; i < MFS_MAX_FILES; ++i) {
    osai_mfs_entry_t *entry = &g_mfs.entries[i];
    if (entry->active == 0) {
      continue;
    }
    if (virtio_block_read_sector(entry->data_sector, sector, sizeof(sector)) !=
        OSAI_OK ||
        virtio_block_write_sector(entry->snapshot_sector, sector,
                                  sizeof(sector)) != OSAI_OK) {
      ++g_reject_count;
      return OSAI_ERR_IO;
    }
    entry->snapshot_active = 1;
    entry->snapshot_size = entry->size;
    entry->snapshot_hash = entry->content_hash;
    entry->snapshot_generation = entry->generation;
  }
  g_mfs.committed_generation = g_mfs.generation;
  ++g_commit_count;
  klog("mutable-fs: snapshot committed generation=%lu files=%lu\n",
       g_mfs.committed_generation, file_count());
  return write_metadata();
}

static osai_status_t rollback_snapshot(void) {
  if (g_mounted == 0 || (g_mount_flags & MFS_MOUNT_READ_WRITE) == 0) {
    ++g_reject_count;
    return OSAI_ERR_INVALID;
  }
  uint8_t sector[MFS_FILE_BYTES];
  for (uint32_t i = 0; i < MFS_MAX_FILES; ++i) {
    osai_mfs_entry_t *entry = &g_mfs.entries[i];
    if (entry->snapshot_active != 0) {
      if (virtio_block_read_sector(entry->snapshot_sector, sector,
                                   sizeof(sector)) != OSAI_OK ||
          virtio_block_write_sector(entry->data_sector, sector,
                                    sizeof(sector)) != OSAI_OK) {
        ++g_reject_count;
        return OSAI_ERR_IO;
      }
      entry->active = 1;
      entry->deleted = 0;
      entry->size = entry->snapshot_size;
      entry->content_hash = entry->snapshot_hash;
      entry->generation = entry->snapshot_generation;
    } else if (entry->active != 0 &&
               entry->generation >= g_mfs.committed_generation) {
      entry->active = 0;
      entry->deleted = 1;
    }
  }
  ++g_mfs.generation;
  ++g_rollback_count;
  klog("mutable-fs: snapshot rollback committed=%lu files=%lu\n",
       g_mfs.committed_generation, file_count());
  return write_metadata();
}

uint64_t mutable_fs_mount_count(void) {
  return g_mount_count;
}

uint64_t mutable_fs_format_count(void) {
  return g_format_count;
}

uint64_t mutable_fs_boot_load_count(void) {
  return g_boot_load_count;
}

uint64_t mutable_fs_file_count(void) {
  return file_count();
}

uint64_t mutable_fs_write_count(void) {
  return g_write_count;
}

uint64_t mutable_fs_read_count(void) {
  return g_read_count;
}

uint64_t mutable_fs_delete_count(void) {
  return g_delete_count;
}

uint64_t mutable_fs_commit_count(void) {
  return g_commit_count;
}

uint64_t mutable_fs_rollback_count(void) {
  return g_rollback_count;
}

uint64_t mutable_fs_reject_count(void) {
  return g_reject_count;
}

uint64_t mutable_fs_checksum_error_count(void) {
  return g_checksum_error_count;
}

void mutable_fs_self_test(void) {
  kassert(sizeof(osai_mfs_disk_t) == MFS_METADATA_SECTORS * MFS_SECTOR_SIZE);
  g_mounted = 0;
  g_mount_flags = 0;
  g_mount_count = 0;
  g_format_count = 0;
  g_boot_load_count = 0;
  g_write_count = 0;
  g_read_count = 0;
  g_delete_count = 0;
  g_commit_count = 0;
  g_rollback_count = 0;
  g_reject_count = 0;
  g_checksum_error_count = 0;

  kassert(mount_volume(MFS_MOUNT_READ_WRITE) == OSAI_OK);
  kassert(format_volume() == OSAI_OK);

  char buffer[MFS_FILE_BYTES];
  uint64_t size = 0;

  kassert(write_file("/state/service.db", k_service_v1,
                     sizeof(k_service_v1)) == OSAI_OK);
  kassert(write_file("/config/osai.conf", k_config_v1,
                     sizeof(k_config_v1)) == OSAI_OK);
  kassert(read_file("/state/service.db", buffer, sizeof(buffer), &size) ==
          OSAI_OK);
  kassert(size == sizeof(k_service_v1));
  kassert(bytes_eq(buffer, k_service_v1, sizeof(k_service_v1)) != 0);
  kassert(commit_snapshot("mfs-snapshot-v1") == OSAI_OK);

  kassert(write_file("/state/service.db", k_service_v2,
                     sizeof(k_service_v2)) == OSAI_OK);
  kassert(write_file("/logs/boot.log", k_boot_log, sizeof(k_boot_log)) ==
          OSAI_OK);
  kassert(delete_file("/config/osai.conf") == OSAI_OK);
  kassert(read_file("/config/osai.conf", buffer, sizeof(buffer), &size) ==
          OSAI_ERR_NOT_FOUND);
  kassert(write_file("/init", k_service_v1, sizeof(k_service_v1)) ==
          OSAI_ERR_INVALID);
  uint8_t too_large[MFS_FILE_BYTES + 1U];
  kassert(write_file("/state/too-large", too_large, sizeof(too_large)) ==
          OSAI_ERR_INVALID);

  kassert(rollback_snapshot() == OSAI_OK);
  kassert(read_file("/state/service.db", buffer, sizeof(buffer), &size) ==
          OSAI_OK);
  kassert(size == sizeof(k_service_v1));
  kassert(bytes_eq(buffer, k_service_v1, sizeof(k_service_v1)) != 0);
  kassert(read_file("/config/osai.conf", buffer, sizeof(buffer), &size) ==
          OSAI_OK);
  kassert(size == sizeof(k_config_v1));
  kassert(bytes_eq(buffer, k_config_v1, sizeof(k_config_v1)) != 0);
  kassert(read_file("/logs/boot.log", buffer, sizeof(buffer), &size) ==
          OSAI_ERR_NOT_FOUND);

  kassert(mutable_fs_mount_count() == 1);
  kassert(mutable_fs_format_count() >= 1);
  kassert(mutable_fs_format_count() <= 2);
  kassert(mutable_fs_file_count() == 2);
  kassert(mutable_fs_write_count() == 4);
  kassert(mutable_fs_read_count() == 3);
  kassert(mutable_fs_delete_count() == 1);
  kassert(mutable_fs_commit_count() == 1);
  kassert(mutable_fs_rollback_count() == 1);
  kassert(mutable_fs_reject_count() == 4);
  kassert(mutable_fs_checksum_error_count() == 0);
  klog("mutable-fs: self-test passed files=%lu writes=%lu reads=%lu deletes=%lu commits=%lu rollbacks=%lu rejects=%lu checksum_errors=%lu\n",
       mutable_fs_file_count(), mutable_fs_write_count(),
       mutable_fs_read_count(), mutable_fs_delete_count(),
       mutable_fs_commit_count(), mutable_fs_rollback_count(),
       mutable_fs_reject_count(), mutable_fs_checksum_error_count());
}
