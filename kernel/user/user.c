#include <osai/assert.h>
#include <osai/klog.h>
#include <osai/pmm.h>
#include <osai/user.h>
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

static uint64_t align_down(uint64_t value, uint64_t align) {
  return value & ~(align - 1);
}

static uint64_t align_up(uint64_t value, uint64_t align) {
  return (value + align - 1) & ~(align - 1);
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

static uint32_t elf_magic(const uint8_t *ident) {
  return ((uint32_t)ident[0]) | ((uint32_t)ident[1] << 8U) |
         ((uint32_t)ident[2] << 16U) | ((uint32_t)ident[3] << 24U);
}

static osai_status_t validate_ehdr(const osai_initramfs_file_t *file,
                                   const elf64_ehdr_t **out) {
  if (file == 0 || file->base == 0 || file->size < sizeof(elf64_ehdr_t) ||
      file->executable == 0) {
    return OSAI_ERR_INVALID;
  }

  const elf64_ehdr_t *ehdr = (const elf64_ehdr_t *)file->base;
  if (elf_magic(ehdr->ident) != ELF_MAGIC || ehdr->ident[4] != 2 ||
      ehdr->ident[5] != 1 || ehdr->type != ET_EXEC ||
      ehdr->machine != EM_AARCH64 || ehdr->phentsize != sizeof(elf64_phdr_t) ||
      ehdr->phnum == 0 ||
      ehdr->phoff + ((uint64_t)ehdr->phnum * ehdr->phentsize) > file->size) {
    return OSAI_ERR_INVALID;
  }
  if (ehdr->entry < OSAI_USER_BASE || ehdr->entry >= OSAI_USER_LIMIT) {
    return OSAI_ERR_INVALID;
  }

  *out = ehdr;
  return OSAI_OK;
}

static uint32_t flags_from_phdr(const elf64_phdr_t *phdr) {
  uint32_t flags = OSAI_VMM_PRESENT | OSAI_VMM_USER;
  if ((phdr->flags & PF_W) != 0) {
    flags |= OSAI_VMM_WRITABLE;
  }
  if ((phdr->flags & PF_X) != 0) {
    flags |= OSAI_VMM_EXECUTABLE;
  }
  (void)PF_R;
  return flags;
}

static osai_status_t load_segment(const osai_initramfs_file_t *file,
                                  const elf64_phdr_t *phdr) {
  if (phdr->memsz < phdr->filesz ||
      phdr->offset + phdr->filesz > file->size ||
      phdr->vaddr < OSAI_USER_BASE ||
      phdr->vaddr + phdr->memsz < phdr->vaddr ||
      phdr->vaddr + phdr->memsz > OSAI_USER_LIMIT) {
    return OSAI_ERR_INVALID;
  }

  uint64_t map_start = align_down(phdr->vaddr, PAGE_SIZE);
  uint64_t map_end = align_up(phdr->vaddr + phdr->memsz, PAGE_SIZE);
  uint64_t source_start = phdr->offset;
  uint32_t flags = flags_from_phdr(phdr);

  for (uint64_t va = map_start; va < map_end; va += PAGE_SIZE) {
    void *page = pmm_alloc_page();
    if (page == 0) {
      return OSAI_ERR_NO_MEMORY;
    }
    bytes_zero(page, PAGE_SIZE);

    uint64_t page_file_start = 0;
    uint64_t page_file_end = 0;
    uint64_t seg_file_start_va = phdr->vaddr;
    uint64_t seg_file_end_va = phdr->vaddr + phdr->filesz;
    if (va < seg_file_end_va && va + PAGE_SIZE > seg_file_start_va) {
      uint64_t copy_va_start = va > seg_file_start_va ? va : seg_file_start_va;
      uint64_t copy_va_end = va + PAGE_SIZE < seg_file_end_va
                                 ? va + PAGE_SIZE
                                 : seg_file_end_va;
      page_file_start = source_start + (copy_va_start - phdr->vaddr);
      page_file_end = source_start + (copy_va_end - phdr->vaddr);
      bytes_copy((uint8_t *)page + (copy_va_start - va),
                 (const uint8_t *)file->base + page_file_start,
                 page_file_end - page_file_start);
    }

    if (vmm_map_page(va, (uint64_t)(uintptr_t)page, flags) != OSAI_OK) {
      pmm_free_page(page);
      return OSAI_ERR_INVALID;
    }
  }

  return OSAI_OK;
}

static osai_status_t map_user_stack(osai_user_process_t *process) {
  uint64_t guard_low = OSAI_USER_STACK_TOP - (3U * PAGE_SIZE);
  uint64_t stack = OSAI_USER_STACK_TOP - (2U * PAGE_SIZE);
  uint64_t guard_high = OSAI_USER_STACK_TOP - PAGE_SIZE;
  void *stack_page = pmm_alloc_page();
  if (stack_page == 0) {
    return OSAI_ERR_NO_MEMORY;
  }
  bytes_zero(stack_page, PAGE_SIZE);

  kassert(vmm_unmap_page(guard_low) == OSAI_OK);
  kassert(vmm_unmap_page(guard_high) == OSAI_OK);
  if (vmm_map_page(stack, (uint64_t)(uintptr_t)stack_page,
                   OSAI_VMM_PRESENT | OSAI_VMM_USER |
                       OSAI_VMM_WRITABLE) != OSAI_OK) {
    pmm_free_page(stack_page);
    return OSAI_ERR_INVALID;
  }

  uint64_t translated = 0;
  uint32_t flags = 0;
  kassert(vmm_translate(guard_low, &translated, &flags) == OSAI_ERR_INVALID);
  kassert(vmm_translate(guard_high, &translated, &flags) == OSAI_ERR_INVALID);
  process->stack_top = stack + PAGE_SIZE;
  process->stack_guard_low = guard_low;
  process->stack_guard_high = guard_high;
  return OSAI_OK;
}

osai_status_t user_load_init(const osai_initramfs_file_t *file,
                             osai_user_process_t *process) {
  const elf64_ehdr_t *ehdr = 0;
  if (process == 0 || validate_ehdr(file, &ehdr) != OSAI_OK) {
    return OSAI_ERR_INVALID;
  }

  const uint8_t *base = (const uint8_t *)file->base;
  for (uint16_t i = 0; i < ehdr->phnum; ++i) {
    const elf64_phdr_t *phdr =
        (const elf64_phdr_t *)(const void *)(base + ehdr->phoff +
                                             ((uint64_t)i * ehdr->phentsize));
    if (phdr->type == PT_LOAD && load_segment(file, phdr) != OSAI_OK) {
      return OSAI_ERR_INVALID;
    }
  }

  process->entry = ehdr->entry;
  if (map_user_stack(process) != OSAI_OK) {
    return OSAI_ERR_INVALID;
  }

  klog("user: loaded /init ELF entry=0x%lx stack=0x%lx guard=[0x%lx,0x%lx]\n",
       process->entry, process->stack_top, process->stack_guard_low,
       process->stack_guard_high);
  return OSAI_OK;
}

void user_process_run(const osai_user_process_t *process) {
  uint64_t entry = process->entry;
  uint64_t stack = process->stack_top;
  uint64_t spsr = 0;

  klog("user: entering EL0 /init entry=0x%lx stack=0x%lx\n", entry, stack);

  __asm__ volatile(
      "msr sp_el0, %[stack]\n"
      "msr elr_el1, %[entry]\n"
      "msr spsr_el1, %[spsr]\n"
      "eret\n"
      :
      : [stack] "r"(stack), [entry] "r"(entry), [spsr] "r"(spsr)
      : "memory");
}
