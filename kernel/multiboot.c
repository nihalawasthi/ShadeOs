#include "kernel.h"

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

void parse_multiboot2_memory_map(uint64_t mb2_info_ptr) {
    vga_print("[BOOT] Parsing Multiboot2 memory map...\n");
    uint8_t* mb2 = (uint8_t*)mb2_info_ptr;
    uint32_t total_size = *(uint32_t*)mb2;
    uint32_t reserved = *(uint32_t*)(mb2 + 4);
    (void)reserved;
    mb2_tag_t* tag = (mb2_tag_t*)(mb2 + 8);
    while ((uint8_t*)tag < mb2 + total_size) {
        if (tag->type == MULTIBOOT2_TAG_TYPE_MMAP) {
            mb2_tag_mmap_t* mmap_tag = (mb2_tag_mmap_t*)tag;
            vga_print("[BOOT] Found memory map tag\n");
            uint8_t* mmap_end = (uint8_t*)mmap_tag + mmap_tag->size;
            for (uint8_t* entry_ptr = (uint8_t*)mmap_tag + sizeof(mb2_tag_mmap_t);
                 entry_ptr < mmap_end;
                 entry_ptr += mmap_tag->entry_size) {
                mb2_mmap_entry_t* entry = (mb2_mmap_entry_t*)entry_ptr;
                vga_print("  region: 0x");
                for (int i = 60; i >= 0; i -= 4) {
                    uint8_t digit = (entry->addr >> i) & 0xF;
                    vga_putchar(digit < 10 ? '0' + digit : 'A' + digit - 10);
                }
                vga_print(" - 0x");
                for (int i = 60; i >= 0; i -= 4) {
                    uint8_t digit = ((entry->addr + entry->len) >> i) & 0xF;
                    vga_putchar(digit < 10 ? '0' + digit : 'A' + digit - 10);
                }
                vga_print("  type: ");
                if (entry->type == 1) {
                    vga_print("available\n");
                } else {
                    vga_print("reserved\n");
                }
            }
        }
        // Move to next tag (aligned)
        tag = (mb2_tag_t*)(((uint8_t*)tag + tag->size + MULTIBOOT2_TAG_ALIGN - 1) & ~(MULTIBOOT2_TAG_ALIGN - 1));
    }
} 