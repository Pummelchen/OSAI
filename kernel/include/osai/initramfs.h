#ifndef OSAI_INITRAMFS_H
#define OSAI_INITRAMFS_H

#include <osai/status.h>
#include <osai/types.h>

typedef struct osai_initramfs_file {
  const char *path;
  const void *base;
  uint64_t size;
  uint32_t executable;
} osai_initramfs_file_t;

void initramfs_init(void);
osai_status_t initramfs_lookup(const char *path,
                               const osai_initramfs_file_t **file);
void initramfs_self_test(void);

#endif
