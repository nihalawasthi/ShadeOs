#include "kernel.h"
#include "pmm.h"
#include "paging.h"
#include "heap.h"
#include "timer.h"
#include "keyboard.h"
#include "serial.h"
#include "vfs.h" // Keep this for shell, but its implementation will change
#include "shell.h"
#include "rtl8139.h"
#include "net.h"
#include "task.h"
#include "syscall.h"
#include "blockdev.h" // Needed for blockdev_get in Rust FFI

// Declare the Rust entry point function
extern void rust_entry_point();

// PIC initialization function
void pic_init() {
    // Remap PIC
    outb(0x20, 0x11);
    outb(0xA0, 0x11);
    outb(0x21, 0x20); // Master offset 0x20 (32)
    outb(0xA1, 0x28); // Slave offset 0x28 (40)
    outb(0x21, 0x04); // Tell Master PIC there is a slave at IRQ2
    outb(0xA1, 0x02); // Tell Slave PIC its cascade identity
    outb(0x21, 0x01);
    outb(0xA1, 0x01);
    // Unmask only IRQ0 (timer) and IRQ1 (keyboard)
    outb(0x21, 0xFC);
    outb(0xA1, 0xFF);
}

void demo_task1() {
    int count = 0;
    while (count < 10) {
        vga_set_color(0x0B);
        vga_print("[Task1] Hello from Task 1 - Count: ");
        // Print count
        char count_str[16];
        int temp = count;
        int i = 0;
        do {
            count_str[i++] = '0' + (temp % 10);
            temp /= 10;
        } while (temp > 0);
        count_str[i] = '\0';
        // Reverse the string
        for (int j = 0; j < i/2; j++) {
            char t = count_str[j];
            count_str[j] = count_str[i-1-j];
            count_str[i-1-j] = t;
        }
        vga_print(count_str);
        vga_print("\n");
        for (volatile int i = 0; i < 10000000; i++);
        count++;
    }
    vga_print("[Task1] Task 1 completed!\n");
    task_exit();
}

void demo_task2() {
    int count = 0;
    while (count < 10) {
        vga_set_color(0x0E);
        vga_print("[Task2] Hello from Task 2 - Count: ");
        // Print count
        char count_str[16];
        int temp = count;
        int i = 0;
        do {
            count_str[i++] = '0' + (temp % 10);
            temp /= 10;
        } while (temp > 0);
        count_str[i] = '\0';
        // Reverse the string
        for (int j = 0; j < i/2; j++) {
            char t = count_str[j];
            count_str[j] = count_str[i-1-j];
            count_str[i-1-j] = t;
        }
        vga_print(count_str);
        vga_print("\n");
        for (volatile int i = 0; i < 10000000; i++);
        count++;
    }
    vga_print("[Task2] Task 2 completed!\n");
    task_exit();
}

void kernel_main(uint64_t mb2_info_ptr) {
    // VGA direct print (always enabled)
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

    // Paging
    paging_init();
    vga_print("[BOOT] Virtual memory manager (paging) initialized\n");

    // Heap
    heap_init();
    vga_print("[BOOT] Kernel heap allocator initialized\n");

    // Timer
    timer_init(100); // 100 Hz
    vga_print("[BOOT] PIT timer initialized (100 Hz)\n");

    // Serial
    serial_init();
    vga_print("[BOOT] Serial port (COM1) initialized\n");
    serial_write("[BOOT] ShadeOS serial port initialized\n");

    // GDT/IDT
    vga_print("[BOOT] Initializing GDT...\n");
    gdt_init();
    vga_print("[BOOT] GDT initialized.\n");
    vga_print("[BOOT] Initializing IDT...\n");
    idt_init();
    vga_print("[BOOT] IDT initialized.\n");
    
    // PIC initialization
    vga_print("[BOOT] Initializing PIC...\n");
    pic_init();
    vga_print("[BOOT] PIC initialized.\n");

    // Keyboard
    initialize_keyboard();
    vga_print("[BOOT] Keyboard driver initialized\n");
    
    vga_print("[BOOT] Kernel loaded successfully!\n");
    vga_print("[BOOT] VGA text mode initialized\n\n");

    // Block Device (for Rust VFS)
    vga_print("[BOOT] Initializing Block Devices...\n");
    blockdev_init(); // Initialize the C ramdisk block device
    vga_print("[BOOT] Block Devices initialized.\n");
    vga_print("[DEBUG] Block device init complete\n");
    serial_write("[DEBUG] Block device init complete\n");

    // Add more debug before VFS or Rust
    vga_print("[DEBUG] About to init VFS or call Rust\n");
    serial_write("[DEBUG] About to init VFS or call Rust\n");

    // Network
    rtl8139_init();
    vga_print("[BOOT] RTL8139 network driver initialized\n");
    serial_write("[BOOT] RTL8139 network driver initialized\n");
    struct ip_addr ip = { {10,0,2,15} };
    net_init(ip);
    vga_print("[BOOT] Network stack (UDP/IP) initialized\n");

    // Multitasking
    vga_print("[BOOT] About to initialize multitasking...\n");
    serial_write("[BOOT] About to initialize multitasking...\n");
    
    task_init();
    vga_print("[BOOT] Task system initialized\n");
    serial_write("[BOOT] Task system initialized\n");

    // Minimal user process that triggers syscalls
    void user_process() {
        sys_write("[USER] Hello from user mode!\n");
        sys_exit();
        while (1) {} // Should not reach here
    }
    // Allocate user stack (page-aligned, 16KB)
    static uint8_t user_stack[16384] __attribute__((aligned(4096)));
    task_create_user(user_process, user_stack, sizeof(user_stack), 0);
    vga_print("[BOOT] User process created\n");
    serial_write("[BOOT] User process created\n");

    // Initialize and run the shell after user process creation
    vga_print("[BOOT] Initializing shell...\n");
    serial_write("[BOOT] Initializing shell...\n");
    shell_init();
    vga_print("[BOOT] Shell initialized\n");
    serial_write("[BOOT] Shell initialized\n");
    // VFS
    // vga_print("[BOOT] Initializing VFS...\n");
    // serial_write("[BOOT] Initializing VFS...\n");
    // vfs_init();
    vga_print("[BOOT] Calling Rust VFS init...\n");
    serial_write("[BOOT] Calling Rust VFS init...\n");
    rust_vfs_init();
    vga_print("[BOOT] Returned from Rust VFS init.\n");
    serial_write("[BOOT] Returned from Rust VFS init.\n");

    // Shell
    // vga_print("[BOOT] Initializing shell...\n");
    // serial_write("[BOOT] Initializing shell...\n");
    // shell_init();
    // vga_print("[BOOT] Shell initialized\n");
    // serial_write("[BOOT] Shell initialized\n");
    
    // SSyscalls
    vga_print("[BOOT] Initializing syscalls...\n");
    serial_write("[BOOT] Initializing syscalls...\n");
    syscall_init();
    vga_print("[BOOT] Syscalls initialized\n");
    serial_write("[BOOT] Syscalls initialized\n");
    
    // Start the shell
    
    vga_print("[BOOT] Starting shell...\n");
    serial_write("[BOOT] Starting shell...\n");
    shell_run();
}
