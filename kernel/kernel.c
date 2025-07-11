// kernel/kernel.c - Simplified kernel entry point for debugging
#include "kernel.h"
#include "pmm.h"
#include "paging.h"
#include "heap.h"
#include "timer.h"
#include "keyboard.h"
#include "serial.h"
#include "vfs.h"
#include "shell.h"
#include "rtl8139.h"
#include "net.h"
#include "task.h"

void demo_task1() {
    while (1) {
        vga_set_color(0x0B);
        vga_print("[Task1] Hello from Task 1\n");
        serial_write("[Task1] Hello from Task 1\n");
        for (volatile int i = 0; i < 10000000; i++);
        task_yield();
    }
}
void demo_task2() {
    while (1) {
        vga_set_color(0x0E);
        vga_print("[Task2] Hello from Task 2\n");
        serial_write("[Task2] Hello from Task 2\n");
        for (volatile int i = 0; i < 10000000; i++);
        task_yield();
    }
}

void kernel_main(uint64_t mb2_info_ptr) {
    // STEP 0: VGA direct print (always enabled)
    volatile uint16_t* vga = (uint16_t*)0xB8000;
    for (int i = 0; i < 80 * 25; i++) vga[i] = 0x0F20;
    const char* msg = "KERNEL STARTED - 64BIT MODE WORKING!";
    for (int i = 0; msg[i]; i++) vga[i] = 0x0A00 | msg[i];

    // VGA init/clear
    vga_init();
    vga_clear();
    vga_set_color(0x0A);
    vga_print("ShadeOS v0.1\n");
    vga_set_color(0x0F);
    vga_print("======================================\n\n");

    // Print Multiboot2 info pointer
    vga_print("[BOOT] Multiboot2 info pointer: 0x");
    for (int i = 60; i >= 0; i -= 4) {
        uint8_t digit = (mb2_info_ptr >> i) & 0xF;
        vga_putchar(digit < 10 ? '0' + digit : 'A' + digit - 10);
    }
    vga_print("\n");

    //Parse Multiboot2 memory map
    parse_multiboot2_memory_map(mb2_info_ptr);

    // Physical memory manager
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

    // STEP 5: Paging
    // paging_init();
    vga_print("[BOOT] Virtual memory manager (paging) initialized\n");

    // STEP 6: Heap
    //heap_init();
    //vga_print("[BOOT] Kernel heap allocator initialized\n");

    // STEP 7: Timer
    //timer_init(100); // 100 Hz
    //vga_print("[BOOT] PIT timer initialized (100 Hz)\n");

    // STEP 8: Keyboard
    keyboard_init();
    vga_print("[BOOT] Keyboard driver initialized\n");

    // STEP 9: Serial
    serial_init();
    vga_print("[BOOT] Serial port (COM1) initialized\n");
    serial_write("[BOOT] ShadeOS serial port initialized\n");

    // STEP 10: VFS
    //vfs_init();
    //vga_print("[BOOT] VFS (in-memory) initialized\n");
    //vfs_node_t* file = vfs_create("demo.txt", VFS_TYPE_MEM, vfs_get_root());
    //if (file) {
    //    vfs_write(file, "Hello, VFS!\n", 12);
    //    file->pos = 0;
    //    char buf[32] = {0};
    //    int n = vfs_read(file, buf, 31);
    //    if (n > 0) buf[n] = '\0';
    //    vga_print("[VFS] Read from demo.txt: ");
    //    vga_print(buf);
    //    vga_print("\n");
    //}

    // STEP 11: GDT/IDT
    //vga_print("[BOOT] Initializing GDT...\n");
    //gdt_init();
    //vga_print("[BOOT] GDT initialized.\n");
    //vga_print("[BOOT] Initializing IDT...\n");
    //idt_init();
    //vga_print("[BOOT] IDT initialized.\n");
    //vga_print("[BOOT] Kernel loaded successfully!\n");
    //vga_print("[BOOT] VGA text mode initialized\n\n");

    // STEP 12: Demo prints
    // vga_print("=== ShadeOS Demo ===\n");
    // vga_print("Proof that kernel is working!\n");
    // vga_print("- Multiboot2 loading: OK\n");
    // vga_print("- C kernel execution: OK\n");
    // vga_print("- VGA text output: OK\n\n");
    // vga_set_color(0x0E);
    // vga_print("System ready. Close QEMU to exit.\n");

    // STEP 13: Network
    rtl8139_init();
    vga_print("[BOOT] RTL8139 network driver initialized\n");
    struct ip_addr ip = { {10,0,2,15} };
    net_init(ip);
    vga_print("[BOOT] Network stack (UDP/IP) initialized\n");

    // STEP 14: Multitasking
    // task_init();
    // task_create(demo_task1);
    // task_create(demo_task2);
    // vga_print("[BOOT] Multitasking demo: running two tasks\n");
    // task_schedule();
}
