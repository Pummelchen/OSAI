#ifndef OSAI_BOOT_INFO_H
#define OSAI_BOOT_INFO_H

#include <stdint.h>

#define OSAI_BOOT_INFO_MAGIC UINT64_C(0x4f534149424f4f54)
#define OSAI_BOOT_INFO_VERSION UINT32_C(1)

typedef struct osai_boot_info {
  uint64_t magic;
  uint32_t version;
  uint32_t reserved;
} osai_boot_info_t;

#endif
