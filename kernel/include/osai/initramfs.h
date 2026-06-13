#ifndef OSAI_INITRAMFS_H
#define OSAI_INITRAMFS_H

#include <osai/status.h>
#include <osai/types.h>

typedef struct osai_initramfs_file {
  const char *path;
  void *base;
  uint64_t size;
  uint32_t executable;
  uint32_t manifest;
  uint64_t content_hash;
} osai_initramfs_file_t;

typedef struct osai_initramfs_config {
  const char *service_path;
  const char *mode;
  uint32_t valid;
} osai_initramfs_config_t;

osai_status_t initramfs_init(void);
osai_status_t initramfs_lookup(const char *path,
                               const osai_initramfs_file_t **file);
const osai_initramfs_config_t *initramfs_config(void);
void initramfs_self_test(void);

#endif
