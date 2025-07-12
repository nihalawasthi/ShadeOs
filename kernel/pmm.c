#include "pmm.h"
#include "kernel.h"
#include <stdint.h>
#include "serial.h"

#define MULTIBOOT2_TAG_TYPE_MMAP 6
#define MULTIBOOT2_TAG_ALIGN 8

typedef struct {
    uint32_t type;
    uint32_t size;
} mb2_tag_t;

typedef struct {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
} mb2_tag_mmap_t;

typedef struct {
    uint64_t addr;
    uint64_t len;
    uint32_t type;
    uint32_t reserved;
} mb2_mmap_entry_t;

#define MAX_PHYS_MEM (512ULL * 1024 * 1024) // 512 MiB max for simplicity
#define MAX_PAGES (MAX_PHYS_MEM / PAGE_SIZE)

static uint8_t page_bitmap[MAX_PAGES / 8];
static uint64_t total_pages = 0;
static uint64_t free_pages = 0;
static uint64_t base_addr = 0;

static void set_page_used(uint64_t page_idx) {
    page_bitmap[page_idx / 8] |= (1 << (page_idx % 8));
}
static void set_page_free(uint64_t page_idx) {
    page_bitmap[page_idx / 8] &= ~(1 << (page_idx % 8));
}
static int is_page_free(uint64_t page_idx) {
    return !(page_bitmap[page_idx / 8] & (1 << (page_idx % 8)));
}

void pmm_init(uint64_t mb2_info_ptr) {
    // Set base address for physical memory management
    base_addr = 0x100000; // 1 MiB
    // Mark all pages as used initially
    memset(page_bitmap, 0xFF, sizeof(page_bitmap));
    total_pages = 0;
    free_pages = 0;

    // Parse Multiboot2 memory map
    uint8_t* mb2 = (uint8_t*)mb2_info_ptr;
    uint32_t total_size = *(uint32_t*)mb2;
    mb2_tag_t* tag = (mb2_tag_t*)(mb2 + 8);
    int mmap_found = 0;
    while ((uint8_t*)tag < mb2 + total_size) {
        if (tag->type == MULTIBOOT2_TAG_TYPE_MMAP) {
            mmap_found = 1;
            mb2_tag_mmap_t* mmap_tag = (mb2_tag_mmap_t*)tag;
            uint8_t* mmap_end = (uint8_t*)mmap_tag + mmap_tag->size;
            for (uint8_t* entry_ptr = (uint8_t*)mmap_tag + sizeof(mb2_tag_mmap_t);
                 entry_ptr < mmap_end;
                 entry_ptr += mmap_tag->entry_size) {
                mb2_mmap_entry_t* entry = (mb2_mmap_entry_t*)entry_ptr;
                if (entry->type == 1 && entry->addr >= base_addr) {
                    uint64_t start = entry->addr;
                    uint64_t end = entry->addr + entry->len;
                    for (uint64_t addr = start; addr + PAGE_SIZE <= end; addr += PAGE_SIZE) {
                        uint64_t page_idx = (addr - base_addr) / PAGE_SIZE;
                        if (page_idx < MAX_PAGES) {
                            set_page_free(page_idx);
                            total_pages++;
                            free_pages++;
                        }
                    }
                }
            }
        }
        tag = (mb2_tag_t*)(((uintptr_t)((uint8_t*)tag + tag->size + MULTIBOOT2_TAG_ALIGN - 1)) & ~(uintptr_t)(MULTIBOOT2_TAG_ALIGN - 1));
    }
    if (!mmap_found) {
        vga_print("[PMM] ERROR: No MMAP tag found!\n");
        serial_write("[PMM] ERROR: No MMAP tag found!\n");
    }
    // Mark kernel and bitmap as used
    extern uint8_t _kernel_start, _kernel_end;
    uint64_t kernel_start = (uint64_t)&_kernel_start;
    uint64_t kernel_end = (uint64_t)&_kernel_end;
    uint64_t bitmap_start = (uint64_t)page_bitmap;
    uint64_t bitmap_end = bitmap_start + sizeof(page_bitmap);
    for (uint64_t addr = kernel_start; addr < kernel_end; addr += PAGE_SIZE) {
        uint64_t page_idx = (addr - base_addr) / PAGE_SIZE;
        if (page_idx < MAX_PAGES && is_page_free(page_idx)) {
            set_page_used(page_idx);
            free_pages--;
        }
    }
    for (uint64_t addr = bitmap_start; addr < bitmap_end; addr += PAGE_SIZE) {
        uint64_t page_idx = (addr - base_addr) / PAGE_SIZE;
        if (page_idx < MAX_PAGES && is_page_free(page_idx)) {
            set_page_used(page_idx);
            free_pages--;
        }
    }
}

void* alloc_page() {
    for (uint64_t i = 0; i < MAX_PAGES; i++) {
        if (is_page_free(i)) {
            set_page_used(i);
            free_pages--;
            return (void*)(base_addr + i * PAGE_SIZE);
        }
    }
    return NULL; // Out of memory
}

void free_page(void* addr) {
    uint64_t a = (uint64_t)addr;
    if (a < base_addr) return;
    uint64_t page_idx = (a - base_addr) / PAGE_SIZE;
    if (page_idx < MAX_PAGES && !is_page_free(page_idx)) {
        set_page_free(page_idx);
        free_pages++;
    }
}

uint64_t pmm_total_memory() {
    return total_pages * PAGE_SIZE;
}

uint64_t pmm_free_memory() {
    return free_pages * PAGE_SIZE;
} 