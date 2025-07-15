#ifndef PAGING_H
#define PAGING_H

#include "kernel.h"

#define PAGE_PRESENT   0x1
#define PAGE_RW        0x2
#define PAGE_USER      0x4
#define PAGE_PWT       0x8
#define PAGE_PCD       0x10
#define PAGE_ACCESSED  0x20
#define PAGE_DIRTY     0x40
#define PAGE_HUGE      0x80
#define PAGE_GLOBAL    0x100

void paging_init();
void map_page(uint64_t virt_addr, uint64_t phys_addr, uint64_t flags);
void unmap_page(uint64_t virt_addr);
uint64_t get_phys_addr(uint64_t virt_addr);
void map_user_page(uint64_t virt_addr, uint64_t phys_addr);
uint64_t paging_new_pml4();
void paging_free_pml4(uint64_t pml4_phys);

#ifdef __cplusplus
extern "C" {
#endif
uint64_t rust_paging_new_pml4();
void rust_paging_free_pml4(uint64_t pml4_phys);
void rust_map_page(uint64_t pml4_phys, uint64_t virt, uint64_t phys, uint64_t flags);
#ifdef __cplusplus
}
#endif

#endif 