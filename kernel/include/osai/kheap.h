#ifndef OSAI_KHEAP_H
#define OSAI_KHEAP_H

#include <osai/types.h>

void kheap_init(void);
void *kheap_alloc(uint64_t size, uint64_t align);
void *kheap_calloc(uint64_t size, uint64_t align);
uint64_t kheap_pages_allocated(void);
uint64_t kheap_bytes_allocated(void);
void kheap_self_test(void);

#endif
