#include "paging.h"
#include "pmm.h"
#include "kernel.h"
#include "serial.h"
#include "memory.h"

#define PML4_ENTRIES 512
#define PDPTE_ENTRIES 512
#define PDE_ENTRIES 512
#define PTE_ENTRIES 512

uint64_t* pml4_table = 0;

// Prototype for debug print function
void print_hex64(unsigned long val);

static inline uint64_t* get_table(uint64_t phys_addr) {
    return (uint64_t*)(phys_addr);
}

static uint64_t get_pml4_index(uint64_t addr) { return (addr >> 39) & 0x1FF; }
static uint64_t get_pdpt_index(uint64_t addr) { return (addr >> 30) & 0x1FF; }
static uint64_t get_pd_index(uint64_t addr)   { return (addr >> 21) & 0x1FF; }
static uint64_t get_pt_index(uint64_t addr)   { return (addr >> 12) & 0x1FF; }

void paging_init() {
    pml4_table = (uint64_t*)alloc_page();
    memset(pml4_table, 0, PAGE_SIZE);

    // --- TEST: Copy kernel PML4 upper half before enabling paging ---
    uint64_t* test_user_pml4 = (uint64_t*)alloc_page();
    if (!test_user_pml4) {
        serial_write("[PAGING]: alloc_page for test_user_pml4 failed!\n");
    } else {
        memset(test_user_pml4, 0, PAGE_SIZE);
        for (int i = 256; i < 512; i++) {
            test_user_pml4[i] = pml4_table[i];
        }
    }
    for (uint64_t addr = 0; addr < 0x1000000; addr += PAGE_SIZE) { // 16MB
        map_page(addr, addr, PAGE_PRESENT | PAGE_RW);
    }

    // Load new PML4
    __asm__ volatile("mov %0, %%cr3" : : "r"(pml4_table));
}

void map_page(uint64_t virt_addr, uint64_t phys_addr, uint64_t flags) {
    uint64_t* pml4 = pml4_table;
    if (!pml4) return;
    
    // PML4
    if (!(pml4[get_pml4_index(virt_addr)] & PAGE_PRESENT)) {
        uint64_t pdpt = (uint64_t)alloc_page();
        memset((void*)pdpt, 0, PAGE_SIZE);
        pml4[get_pml4_index(virt_addr)] = pdpt | PAGE_PRESENT | PAGE_RW;
    }
    uint64_t* pdpt = get_table(pml4[get_pml4_index(virt_addr)] & ~0xFFFULL);
    
    // PDPT
    if (!(pdpt[get_pdpt_index(virt_addr)] & PAGE_PRESENT)) {
        uint64_t pd = (uint64_t)alloc_page();
        memset((void*)pd, 0, PAGE_SIZE);
        pdpt[get_pdpt_index(virt_addr)] = pd | PAGE_PRESENT | PAGE_RW;
    }
    uint64_t* pd = get_table(pdpt[get_pdpt_index(virt_addr)] & ~0xFFFULL);
    
    // PD
    if (!(pd[get_pd_index(virt_addr)] & PAGE_PRESENT)) {
        uint64_t pt = (uint64_t)alloc_page();
        memset((void*)pt, 0, PAGE_SIZE);
        pd[get_pd_index(virt_addr)] = pt | PAGE_PRESENT | PAGE_RW;
    }
    uint64_t* pt = get_table(pd[get_pd_index(virt_addr)] & ~0xFFFULL);
    
    // PT
    pt[get_pt_index(virt_addr)] = (phys_addr & ~0xFFFULL) | (flags & 0xFFF) | PAGE_PRESENT;
}

void unmap_page(uint64_t virt_addr) {
    uint64_t* pml4 = pml4_table;
    if (!pml4) return;
    uint64_t* pdpt = get_table(pml4[get_pml4_index(virt_addr)] & ~0xFFFULL);
    if (!pdpt) return;
    uint64_t* pd = get_table(pdpt[get_pdpt_index(virt_addr)] & ~0xFFFULL);
    if (!pd) return;
    uint64_t* pt = get_table(pd[get_pd_index(virt_addr)] & ~0xFFFULL);
    if (!pt) return;
    pt[get_pt_index(virt_addr)] = 0;
    // (No TLB flush here; add if needed)
}

uint64_t get_phys_addr(uint64_t virt_addr) {
    uint64_t* pml4 = pml4_table;
    if (!pml4) return 0;
    uint64_t* pdpt = get_table(pml4[get_pml4_index(virt_addr)] & ~0xFFFULL);
    if (!pdpt) return 0;
    uint64_t* pd = get_table(pdpt[get_pdpt_index(virt_addr)] & ~0xFFFULL);
    if (!pd) return 0;
    uint64_t* pt = get_table(pd[get_pd_index(virt_addr)] & ~0xFFFULL);
    if (!pt) return 0;
    return pt[get_pt_index(virt_addr)] & ~0xFFFULL;
} 

void map_user_page(uint64_t virt_addr, uint64_t phys_addr) {
    map_page(virt_addr, phys_addr, PAGE_PRESENT | PAGE_RW | PAGE_USER);
} 

// Create a new PML4 for a user process, mapping kernel memory
uint64_t paging_new_pml4() {
    serial_write("[PAGING] paging_new_pml4: called\n");
    uint64_t* new_pml4 = (uint64_t*)alloc_page();
    if (!new_pml4) {
        serial_write("[PAGING] paging_new_pml4: alloc_page returned NULL!\n");
        return 0;
    }
    serial_write("[PAGING] paging_new_pml4: alloc_page OK\n");
    memset(new_pml4, 0, PAGE_SIZE);
    extern uint64_t* pml4_table;
    if (!pml4_table) {
        serial_write("[PAGING] paging_new_pml4: pml4_table is NULL!\n");
        return 0;
    }
    serial_write("[PAGING] paging_new_pml4: pml4_table OK\n");
    char addr_msg[128];
    snprintf(addr_msg, sizeof(addr_msg), "[PAGING] pml4_table=0x%lx &pml4_table[256]=0x%lx pml4_table[256]=0x%lx\n", (unsigned long)pml4_table, (unsigned long)&pml4_table[256], (unsigned long)pml4_table[256]);
    serial_write(addr_msg);
    for (int i = 256; i < 512; i++) {
        new_pml4[i] = pml4_table[i];
    }
    serial_write("[PAGING] paging_new_pml4: done copying kernel entries\n");
    // Optionally: map user stack/code here
    return (uint64_t)new_pml4;
}

void paging_free_pml4(uint64_t pml4_phys) {
    if (!pml4_phys) return;
    
    uint64_t* pml4 = (uint64_t*)pml4_phys;
    
    // Free all page tables recursively
    for (int pml4_idx = 0; pml4_idx < 256; pml4_idx++) { // Only user space (lower half)
        if (!(pml4[pml4_idx] & PAGE_PRESENT)) continue;
        
        uint64_t* pdpt = get_table(pml4[pml4_idx] & ~0xFFFULL);
        if (!pdpt) continue;
        
        for (int pdpt_idx = 0; pdpt_idx < PDE_ENTRIES; pdpt_idx++) {
            if (!(pdpt[pdpt_idx] & PAGE_PRESENT)) continue;
            
            uint64_t* pd = get_table(pdpt[pdpt_idx] & ~0xFFFULL);
            if (!pd) continue;
            
            for (int pd_idx = 0; pd_idx < PDE_ENTRIES; pd_idx++) {
                if (!(pd[pd_idx] & PAGE_PRESENT)) continue;
                
                uint64_t* pt = get_table(pd[pd_idx] & ~0xFFFULL);
                if (pt) {
                    free_page(pt); // Free page table
                }
            }
            
            free_page(pd); // Free page directory
        }
        
        free_page(pdpt); // Free page directory pointer table
    }
    
    free_page((void*)pml4_phys); // Free PML4 itself
}

// FFI wrappers for Rust
uint64_t rust_paging_new_pml4() {
    return paging_new_pml4();
}
void rust_paging_free_pml4(uint64_t pml4_phys) {
    paging_free_pml4(pml4_phys);
}
void rust_map_page(uint64_t pml4_phys, uint64_t virt, uint64_t phys, uint64_t flags) {
    // Temporarily switch pml4_table to the target, map, then restore
    uint64_t* old_pml4 = pml4_table;
    pml4_table = (uint64_t*)pml4_phys;
    map_page(virt, phys, flags);
    pml4_table = old_pml4;
}
