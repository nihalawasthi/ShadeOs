#ifndef PMM_H
#define PMM_H

#include "kernel.h"

#define PAGE_SIZE 4096

void pmm_init(uint64_t mb2_info_ptr);
void* alloc_page();
void free_page(void* addr);
uint64_t pmm_total_memory();
uint64_t pmm_free_memory();

#endif
