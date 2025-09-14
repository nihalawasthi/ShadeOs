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

void parse_multiboot2_memory_map(uint64_t mb2_info_ptr) {
    serial_write("[BOOT] Parsing Multiboot2 memory map...\n");
    serial_write_hex("[MB2] mb2_info_ptr: ", mb2_info_ptr);

    serial_write("[DEBUG] parse_multiboot2_memory_map: start\n");
    // Check if pointer is valid and aligned
    if (mb2_info_ptr == 0 || (mb2_info_ptr & 0x7) != 0) {
        serial_write("[BOOT] ERROR: Invalid or unaligned Multiboot2 info pointer!\n");
        return;
    }

    uint8_t* mb2 = (uint8_t*)mb2_info_ptr;
    uint32_t total_size = *(uint32_t*)mb2;
    uint32_t reserved = *(uint32_t*)(mb2 + 4);

    // Debug: Print first 32 bytes at mb2_info_ptr
    vga_print("[MB2] Raw bytes: ");
    for (int i = 0; i < 32; i++) {
        uint8_t byte = mb2[i];
        vga_putchar(' ');
        if (byte < 0x10) vga_putchar('0');
        uint8_t digit = (byte >> 4) & 0xF;
        vga_putchar(digit < 10 ? '0' + digit : 'A' + digit - 10);
        digit = byte & 0xF;
        vga_putchar(digit < 10 ? '0' + digit : 'A' + digit - 10);
    }
    vga_print("\n");

    // Validate total_size
    if (total_size < 8 || total_size > 0x1000000) {
        serial_write("[BOOT] ERROR: Invalid total_size!\n");
        return;
    }

    (void)reserved;
    mb2_tag_t* tag = (mb2_tag_t*)(mb2 + 8);
    int tag_count = 0;
    while ((uint8_t*)tag < mb2 + total_size && tag_count < 20) {
        tag_count++;
        if (tag->type == MULTIBOOT2_TAG_TYPE_MMAP) {
            mb2_tag_mmap_t* mmap_tag = (mb2_tag_mmap_t*)tag;
            serial_write("[BOOT] Found memory map tag\n");
            uint8_t* mmap_end = (uint8_t*)mmap_tag + mmap_tag->size;
            for (uint8_t* entry_ptr = (uint8_t*)mmap_tag + sizeof(mb2_tag_mmap_t);
                 entry_ptr < mmap_end;
                 entry_ptr += mmap_tag->entry_size) {
                if (mmap_tag->entry_size == 0) {
                    serial_write("[DEBUG] parse_multiboot2_memory_map: entry_size==0, breaking\n");
                    break;
                }
                if (entry_ptr + mmap_tag->entry_size > mmap_end) {
                    serial_write("[DEBUG] parse_multiboot2_memory_map: entry_ptr out of bounds, breaking\n");
                    break;
                }
                mb2_mmap_entry_t* entry = (mb2_mmap_entry_t*)entry_ptr;
            }
            serial_write("[DEBUG] parse_multiboot2_memory_map: finished mmap entry loop\n");
        }
        // Move to next tag (aligned)
        tag = (mb2_tag_t*)(((uintptr_t)((uint8_t*)tag + tag->size + MULTIBOOT2_TAG_ALIGN - 1)) & ~(uintptr_t)(MULTIBOOT2_TAG_ALIGN - 1));
    }
    serial_write("[DEBUG] parse_multiboot2_memory_map: done\n");
}
