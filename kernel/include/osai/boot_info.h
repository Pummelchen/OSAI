#ifndef OSAI_BOOT_INFO_H
#define OSAI_BOOT_INFO_H

#include <stdint.h>

#define OSAI_BOOT_INFO_MAGIC UINT64_C(0x4f534149424f4f54)
#define OSAI_BOOT_INFO_VERSION UINT32_C(1)

#define OSAI_MEMORY_TYPE_CONVENTIONAL UINT32_C(7)

typedef struct osai_memory_descriptor {
  uint32_t type;
  uint32_t pad;
  uint64_t physical_start;
  uint64_t virtual_start;
  uint64_t number_of_pages;
  uint64_t attribute;
} osai_memory_descriptor_t;

typedef struct osai_boot_info {
  uint64_t magic;
  uint32_t version;
  uint32_t reserved;
  uint64_t memory_map;
  uint64_t memory_map_size;
  uint64_t memory_descriptor_size;
  uint64_t memory_descriptor_version;
  uint64_t kernel_phys_base;
  uint64_t kernel_phys_end;
  uint64_t uart_base;
} osai_boot_info_t;

#endif
