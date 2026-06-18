#ifndef XAIOS_INITRAMFS_H
#define XAIOS_INITRAMFS_H

#include <xaios/status.h>
#include <xaios/types.h>

typedef struct xaios_initramfs_file {
  const char *path;
  void *base;
  uint64_t size;
  uint32_t executable;
  uint32_t manifest;
  uint64_t content_hash;
} xaios_initramfs_file_t;

typedef struct xaios_initramfs_config {
  const char *service_path;
  const char *service_manager_path;
  const char *service_descriptor_path;
  const char *mode;
  const char *child_service_path;
  const char *child_service_parent;
  const char *child_service_restart;
  uint32_t valid;
} xaios_initramfs_config_t;

xaios_status_t initramfs_init(void);
xaios_status_t initramfs_lookup(const char *path,
                               const xaios_initramfs_file_t **file);
const xaios_initramfs_config_t *initramfs_config(void);
void initramfs_self_test(void);

#endif
