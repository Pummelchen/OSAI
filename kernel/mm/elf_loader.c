#include <osai/assert.h>
#include <osai/elf_loader.h>
#include <osai/klog.h>
#include <osai/pmm.h>
#include <osai/vmm.h>

#define PAGE_SIZE UINT64_C(4096)
#define ELF_MAGIC UINT32_C(0x464c457f)
#define PT_LOAD UINT32_C(1)
#define PF_X UINT32_C(1)
#define PF_W UINT32_C(2)
#define PF_R UINT32_C(4)
#define ET_EXEC UINT16_C(2)
#define EM_AARCH64 UINT16_C(183)

typedef struct elf64_ehdr {
  uint8_t ident[16];
  uint16_t type;
  uint16_t machine;
  uint32_t version;
  uint64_t entry;
  uint64_t phoff;
  uint64_t shoff;
  uint32_t flags;
  uint16_t ehsize;
  uint16_t phentsize;
  uint16_t phnum;
  uint16_t shentsize;
  uint16_t shnum;
  uint16_t shstrndx;
} elf64_ehdr_t;

typedef struct elf64_phdr {
  uint32_t type;
  uint32_t flags;
  uint64_t offset;
  uint64_t vaddr;
  uint64_t paddr;
  uint64_t filesz;
  uint64_t memsz;
  uint64_t align;
} elf64_phdr_t;

static uint64_t align_down(uint64_t value, uint64_t alignment) {
  return value & ~(alignment - 1);
}

static uint64_t align_up(uint64_t value, uint64_t alignment) {
  return (value + alignment - 1) & ~(alignment - 1);
}

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

static uint32_t elf_magic_value(const uint8_t *ident) {
  return ((uint32_t)ident[0]) | ((uint32_t)ident[1] << 8U) |
         ((uint32_t)ident[2] << 16U) | ((uint32_t)ident[3] << 24U);
}

static osai_status_t validate_elf(const osai_initramfs_file_t *file,
                                  const elf64_ehdr_t **out) {
  if (file == 0 || file->base == 0 || file->size < sizeof(elf64_ehdr_t) ||
      file->executable == 0) {
    return OSAI_ERR_INVALID;
  }

  const elf64_ehdr_t *ehdr = (const elf64_ehdr_t *)file->base;
  if (elf_magic_value(ehdr->ident) != ELF_MAGIC || ehdr->ident[4] != 2 ||
      ehdr->ident[5] != 1 || ehdr->type != ET_EXEC ||
      ehdr->machine != EM_AARCH64 ||
      ehdr->phentsize != sizeof(elf64_phdr_t) || ehdr->phnum == 0 ||
      ehdr->phoff + ((uint64_t)ehdr->phnum * ehdr->phentsize) > file->size) {
    return OSAI_ERR_INVALID;
  }
  if (ehdr->entry < OSAI_USER_BASE || ehdr->entry >= OSAI_USER_LIMIT) {
    return OSAI_ERR_INVALID;
  }

  *out = ehdr;
  return OSAI_OK;
}

static uint32_t vmm_flags_from_phdr(const elf64_phdr_t *phdr) {
  uint32_t flags = OSAI_VMM_PRESENT | OSAI_VMM_USER | OSAI_VMM_NG;
  if ((phdr->flags & PF_W) != 0) {
    flags |= OSAI_VMM_WRITABLE;
  }
  if ((phdr->flags & PF_X) != 0) {
    flags |= OSAI_VMM_EXECUTABLE;
  }
  (void)PF_R;
  return flags;
}

static osai_status_t track_page(osai_process_aspace_t *aspace, uint64_t va,
                                uint64_t pa) {
  if (aspace->page_count >= OSAI_ELF_LOADER_MAX_PAGES) {
    return OSAI_ERR_NO_MEMORY;
  }
  aspace->page_va[aspace->page_count] = va;
  aspace->page_pa[aspace->page_count] = pa;
  ++aspace->page_count;
  return OSAI_OK;
}

static osai_status_t load_segment(const osai_initramfs_file_t *file,
                                  const elf64_phdr_t *phdr,
                                  osai_process_aspace_t *aspace) {
  if (phdr->memsz < phdr->filesz ||
      phdr->offset + phdr->filesz > file->size ||
      phdr->vaddr < OSAI_USER_BASE ||
      phdr->vaddr + phdr->memsz < phdr->vaddr ||
      phdr->vaddr + phdr->memsz > OSAI_USER_LIMIT) {
    return OSAI_ERR_INVALID;
  }

  uint64_t map_start = align_down(phdr->vaddr, PAGE_SIZE);
  uint64_t map_end = align_up(phdr->vaddr + phdr->memsz, PAGE_SIZE);
  uint32_t flags = vmm_flags_from_phdr(phdr);

  for (uint64_t va = map_start; va < map_end; va += PAGE_SIZE) {
    void *page = pmm_alloc_page();
    if (page == 0) {
      return OSAI_ERR_NO_MEMORY;
    }
    bytes_zero(page, PAGE_SIZE);

    /* Copy file data into the page */
    uint64_t seg_file_start_va = phdr->vaddr;
    uint64_t seg_file_end_va = phdr->vaddr + phdr->filesz;
    if (va < seg_file_end_va && va + PAGE_SIZE > seg_file_start_va) {
      uint64_t copy_start = va > seg_file_start_va ? va : seg_file_start_va;
      uint64_t copy_end =
          va + PAGE_SIZE < seg_file_end_va ? va + PAGE_SIZE : seg_file_end_va;
      uint64_t file_offset = phdr->offset + (copy_start - phdr->vaddr);
      bytes_copy((uint8_t *)page + (copy_start - va),
                 (const uint8_t *)file->base + file_offset,
                 copy_end - copy_start);
    }

    /* Map into per-process aspace AND global tables */
    if (vmm_map_user_page(va, (uint64_t)(uintptr_t)page, flags,
                          aspace->l3_phys, aspace->l3_count) != OSAI_OK) {
      pmm_free_page(page);
      return OSAI_ERR_INVALID;
    }
    if (track_page(aspace, va, (uint64_t)(uintptr_t)page) != OSAI_OK) {
      pmm_free_page(page);
      return OSAI_ERR_NO_MEMORY;
    }
  }

  return OSAI_OK;
}

osai_status_t elf_loader_load(const osai_initramfs_file_t *file,
                              osai_process_aspace_t *aspace,
                              uint64_t *out_entry) {
  const elf64_ehdr_t *ehdr = 0;
  if (file == 0 || aspace == 0 || out_entry == 0) {
    return OSAI_ERR_INVALID;
  }
  if (validate_elf(file, &ehdr) != OSAI_OK) {
    return OSAI_ERR_INVALID;
  }

  /* Create per-process address space (allocates L3 tables) */
  vmm_create_user_aspace(aspace->l3_phys, OSAI_ELF_LOADER_L3_TABLES,
                         &aspace->l3_count);
  aspace->page_count = 0;

  /* Load all PT_LOAD segments */
  const uint8_t *base = (const uint8_t *)file->base;
  for (uint16_t i = 0; i < ehdr->phnum; ++i) {
    const elf64_phdr_t *phdr =
        (const elf64_phdr_t *)(const void *)(base + ehdr->phoff +
                                             ((uint64_t)i * ehdr->phentsize));
    if (phdr->type == PT_LOAD) {
      if (load_segment(file, phdr, aspace) != OSAI_OK) {
        return OSAI_ERR_INVALID;
      }
    }
  }

  *out_entry = ehdr->entry;
  klog("elf_loader: loaded entry=0x%lx pages=%u l3_count=%u\n", ehdr->entry,
       aspace->page_count, aspace->l3_count);
  return OSAI_OK;
}

osai_status_t elf_loader_map_stack(osai_process_aspace_t *aspace,
                                   uint64_t stack_va, uint64_t guard_low,
                                   uint64_t guard_high) {
  if (aspace == 0) {
    return OSAI_ERR_INVALID;
  }

  /* Allocate stack page */
  void *stack_page = pmm_alloc_page();
  if (stack_page == 0) {
    return OSAI_ERR_NO_MEMORY;
  }
  bytes_zero(stack_page, PAGE_SIZE);

  /* Unmap guard pages from global tables */
  kassert(vmm_unmap_page(guard_low) == OSAI_OK);
  kassert(vmm_unmap_page(guard_high) == OSAI_OK);

  /* Map stack page into per-process aspace AND global tables */
  if (vmm_map_user_page(stack_va, (uint64_t)(uintptr_t)stack_page,
                        OSAI_VMM_PRESENT | OSAI_VMM_USER |
                            OSAI_VMM_WRITABLE | OSAI_VMM_NG,
                        aspace->l3_phys, aspace->l3_count) != OSAI_OK) {
    pmm_free_page(stack_page);
    return OSAI_ERR_INVALID;
  }
  if (track_page(aspace, stack_va, (uint64_t)(uintptr_t)stack_page) !=
      OSAI_OK) {
    pmm_free_page(stack_page);
    return OSAI_ERR_NO_MEMORY;
  }

  klog("elf_loader: stack mapped va=0x%lx guard=[0x%lx,0x%lx]\n", stack_va,
       guard_low, guard_high);
  return OSAI_OK;
}

void elf_loader_reclaim(osai_process_aspace_t *aspace, uint64_t mapped_low,
                        uint64_t mapped_high) {
  if (aspace == 0) {
    return;
  }

  /* Free all tracked pages (unmap from both per-process and global tables) */
  for (uint32_t i = 0; i < aspace->page_count; ++i) {
    uint64_t va = aspace->page_va[i];
    uint64_t pa = aspace->page_pa[i];
    if (pa != 0) {
      vmm_unmap_user_page(va, aspace->l3_phys, aspace->l3_count);
      pmm_free_page((void *)(uintptr_t)pa);
    }
  }
  aspace->page_count = 0;

  /* Also walk the mapped range to catch any pages not tracked */
  if (mapped_low != 0 && mapped_high > mapped_low) {
    for (uint64_t va = mapped_low; va < mapped_high; va += PAGE_SIZE) {
      uint64_t physical = 0;
      uint32_t flags = 0;
      if (vmm_translate(va, &physical, &flags) == OSAI_OK &&
          (flags & OSAI_VMM_USER) != 0) {
        vmm_unmap_user_page(va, aspace->l3_phys, aspace->l3_count);
        pmm_free_page((void *)(uintptr_t)physical);
      }
    }
  }

  /* Free per-process L3 tables */
  if (aspace->l3_count > 0) {
    vmm_switch_user_aspace(0, 0);
    vmm_destroy_user_aspace(aspace->l3_phys, aspace->l3_count);
    aspace->l3_count = 0;
  }

  klog("elf_loader: reclaimed pages from aspace\n");
}

void elf_loader_self_test(void) {
  /* Verify ELF validation rejects bad data */
  osai_initramfs_file_t bad_file;
  bytes_zero(&bad_file, sizeof(bad_file));
  osai_process_aspace_t test_aspace;
  bytes_zero(&test_aspace, sizeof(test_aspace));
  uint64_t entry = 0;
  kassert(elf_loader_load(&bad_file, &test_aspace, &entry) == OSAI_ERR_INVALID);
  kassert(elf_loader_load(0, &test_aspace, &entry) == OSAI_ERR_INVALID);
  kassert(elf_loader_load(&bad_file, 0, &entry) == OSAI_ERR_INVALID);
  klog("elf_loader: self-test passed\n");
}
