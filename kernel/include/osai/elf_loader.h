#ifndef OSAI_ELF_LOADER_H
#define OSAI_ELF_LOADER_H

#include <osai/initramfs.h>
#include <osai/status.h>
#include <osai/types.h>
#include <osai/vmm.h>

#define OSAI_ELF_LOADER_MAX_PAGES 64U
#define OSAI_ELF_LOADER_L3_TABLES 4U

typedef struct osai_process_aspace {
  uint64_t l3_phys[OSAI_ELF_LOADER_L3_TABLES];
  uint32_t l3_count;
  uint64_t page_va[OSAI_ELF_LOADER_MAX_PAGES];
  uint64_t page_pa[OSAI_ELF_LOADER_MAX_PAGES];
  uint32_t page_count;
} osai_process_aspace_t;

osai_status_t elf_loader_load(const osai_initramfs_file_t *file,
                              osai_process_aspace_t *aspace,
                              uint64_t *out_entry);
osai_status_t elf_loader_map_stack(osai_process_aspace_t *aspace,
                                   uint64_t stack_va, uint64_t guard_low,
                                   uint64_t guard_high);
void elf_loader_reclaim(osai_process_aspace_t *aspace, uint64_t mapped_low,
                        uint64_t mapped_high);
void elf_loader_self_test(void);

#endif
