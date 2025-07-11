// kernel/kernel.c - Simplified kernel entry point for debugging
#include "kernel.h"
#include "pmm.h"
#include "paging.h"
#include "heap.h"
#include "timer.h"
#include "keyboard.h"

void kernel_main(uint64_t mb2_info_ptr) {
    // First thing - write directly to VGA to test if we even get here
    volatile uint16_t* vga = (uint16_t*)0xB8000;
    
    // Clear screen
    for (int i = 0; i < 80 * 25; i++) {
        vga[i] = 0x0F20; // White space
    }
    
    // Write test message
    const char* msg = "KERNEL STARTED - 64BIT MODE WORKING!";
    for (int i = 0; msg[i]; i++) {
        vga[i] = 0x0A00 | msg[i]; // Light green text
    }
    
    // If we get here, the kernel is working
    // Initialize VGA properly
    vga_init();
    vga_clear();
    
    vga_set_color(0x0A); // Light green
    vga_print("ShadeOS v0.1\n");
    vga_set_color(0x0F); // White
    vga_print("======================================\n\n");

    vga_print("[BOOT] Multiboot2 info pointer: 0x");
    // Print mb2_info_ptr as hex (simple, not robust)
    for (int i = 60; i >= 0; i -= 4) {
        uint8_t digit = (mb2_info_ptr >> i) & 0xF;
        vga_putchar(digit < 10 ? '0' + digit : 'A' + digit - 10);
    }
    vga_print("\n");

    // Parse Multiboot2 memory map
    parse_multiboot2_memory_map(mb2_info_ptr);

    // Initialize physical memory manager
    pmm_init(mb2_info_ptr);
    vga_print("[BOOT] Physical memory manager initialized\n");
    vga_print("[BOOT] Total memory: ");
    uint64_t total = pmm_total_memory();
    for (int i = 60; i >= 0; i -= 4) {
        uint8_t digit = (total >> i) & 0xF;
        vga_putchar(digit < 10 ? '0' + digit : 'A' + digit - 10);
    }
    vga_print(" bytes\n");
    vga_print("[BOOT] Free memory: ");
    uint64_t free = pmm_free_memory();
    for (int i = 60; i >= 0; i -= 4) {
        uint8_t digit = (free >> i) & 0xF;
        vga_putchar(digit < 10 ? '0' + digit : 'A' + digit - 10);
    }
    vga_print(" bytes\n");

    // Initialize paging
    paging_init();
    vga_print("[BOOT] Virtual memory manager (paging) initialized\n");

    // Initialize kernel heap
    heap_init();
    vga_print("[BOOT] Kernel heap allocator initialized\n");

    // Initialize PIT timer
    timer_init(100); // 100 Hz
    vga_print("[BOOT] PIT timer initialized (100 Hz)\n");

    // Initialize keyboard
    keyboard_init();
    vga_print("[BOOT] Keyboard driver initialized\n");

    vga_print("[BOOT] Initializing GDT...\n");
    gdt_init();
    vga_print("[BOOT] GDT initialized.\n");

    vga_print("[BOOT] Initializing IDT...\n");
    idt_init();
    vga_print("[BOOT] IDT initialized.\n");
    vga_print("[BOOT] Kernel loaded successfully!\n");
    vga_print("[BOOT] VGA text mode initialized\n\n");
    
    // Simple demo
    vga_print("=== ShadeOS Demo ===\n");
    vga_print("Proof that kernel is working!\n");
    vga_print("- Multiboot2 loading: OK\n");
    vga_print("- C kernel execution: OK\n");
    vga_print("- VGA text output: OK\n\n");
    
    vga_set_color(0x0E); // Yellow
    vga_print("System ready. Close QEMU to exit.\n");
    
    // Infinite loop
    while (1) {
        __asm__ volatile("hlt");
    }
}
