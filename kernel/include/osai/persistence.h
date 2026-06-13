#ifndef OSAI_PERSISTENCE_H
#define OSAI_PERSISTENCE_H

#include <osai/status.h>
#include <osai/types.h>

typedef enum osai_snapshot_kind {
  OSAI_SNAPSHOT_BOOT_CONFIG = 1,
  OSAI_SNAPSHOT_SERVICE = 2,
  OSAI_SNAPSHOT_WORKSPACE = 3,
  OSAI_SNAPSHOT_SANDBOX = 4,
  OSAI_SNAPSHOT_UPDATE = 5,
} osai_snapshot_kind_t;

void persistence_runtime_init(void);
osai_status_t persistence_snapshot_create(osai_snapshot_kind_t kind,
                                          uint32_t owner_id,
                                          const char *label);
osai_status_t persistence_rollback(osai_snapshot_kind_t kind,
                                   uint32_t owner_id);
uint64_t persistence_snapshot_count(void);
uint64_t persistence_rollback_count(void);
uint64_t persistence_reject_count(void);
uint64_t persistence_disk_write_count(void);
uint64_t persistence_disk_load_count(void);
uint64_t persistence_disk_boot_load_count(void);
uint64_t persistence_checksum_error_count(void);
void persistence_self_test(void);

#endif
