#include "paging.h"
#include "pmm.h"
#include "kernel.h"
#include "serial.h"

#define PML4_ENTRIES 512
#define PDPTE_ENTRIES 512
#define PDE_ENTRIES 512
#define PTE_ENTRIES 512

uint64_t* pml4_table = 0;

static inline uint64_t* get_table(uint64_t phys_addr) {
    return (uint64_t*)(phys_addr);
}

static uint64_t get_pml4_index(uint64_t addr) { return (addr >> 39) & 0x1FF; }
static uint64_t get_pdpt_index(uint64_t addr) { return (addr >> 30) & 0x1FF; }
static uint64_t get_pd_index(uint64_t addr)   { return (addr >> 21) & 0x1FF; }
static uint64_t get_pt_index(uint64_t addr)   { return (addr >> 12) & 0x1FF; }

void paging_init() {
    vga_print("[BOOT] Initializing paging...\n");
    serial_write("[PAGING] paging_init: start\n");
    pml4_table = (uint64_t*)alloc_page();
    memset(pml4_table, 0, PAGE_SIZE);

    // Identity map kernel (1 MiB to 16 MiB)
    for (uint64_t addr = 0x100000; addr < 0x1000000; addr += PAGE_SIZE) {
        map_page(addr, addr, PAGE_PRESENT | PAGE_RW);
    }
    serial_write("[PAGING] kernel identity map done\n");

    // Identity map VGA (0xB8000)
    map_page(0xB8000, 0xB8000, PAGE_PRESENT | PAGE_RW);
    serial_write("[PAGING] VGA map done\n");

    // Load new PML4
    __asm__ volatile("mov %0, %%cr3" : : "r"(pml4_table));
    vga_print("[BOOT] Paging enabled\n");
    serial_write("[PAGING] paging_init: done\n");
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
    uint64_t* new_pml4 = (uint64_t*)alloc_page();
    if (!new_pml4) return 0;
    memset(new_pml4, 0, PAGE_SIZE);
    // Map kernel memory (copy kernel PML4 entries for higher half)
    extern uint64_t* pml4_table;
    for (int i = 256; i < 512; i++) {
        new_pml4[i] = pml4_table[i];
    }
    // Optionally: map user stack/code here
    return (uint64_t)new_pml4;
}

void paging_free_pml4(uint64_t pml4_phys) {
    // For now, just free the PML4 page itself
    free_page((void*)pml4_phys);
    // TODO: Free all page tables recursively
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