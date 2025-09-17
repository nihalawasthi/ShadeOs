#include "kernel.h"
#include "pmm.h"
#include "paging.h"
#include "heap.h"
#include "timer.h"
#include "keyboard.h"
#include "http.h"
#include "serial.h"
#include "vfs.h" // Keep this for shell, but its implementation will change
#include "rtl8139.h"
#include "net.h"
#include "task.h"
#include "syscall.h"
#include "blockdev.h" // Needed for blockdev_get in Rust FFI
#include <stdbool.h>
typedef unsigned int u32;

// Declare the Rust entry point function
extern void rust_entry_point();

// Declare Rust bash functions
extern void rust_bash_init();
extern void rust_bash_run();

// Declare new Rust functions
extern void rust_process_init();
extern void rust_syscall_init();
extern u32 rust_process_create(u32 parent_pid, bool is_kernel);
extern void rust_process_list();

// Add FFI declaration for Rust init_heap
extern void init_heap();

// Minimal valid ELF64 binary (hello world stub, 128 bytes)
static const unsigned char test_elf_stub[128] = {
    0x7F, 'E', 'L', 'F', // Magic
    2, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 64-bit, LSB, version, padding
    2, 0, // ET_EXEC
    0x3E, 0x00, // EM_X86_64
    1, 0, 0, 0, // EV_CURRENT
    0x78, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, // e_entry (0x400078)
    0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // e_phoff (64)
    0, 0, 0, 0, 0, 0, 0, 0, // e_shoff
    0, 0, 0, 0, // e_flags
    0x40, 0x00, // e_ehsize (64)
    0x38, 0x00, // e_phentsize (56)
    0x01, 0x00, // e_phnum (1)
    0, 0, // e_shentsize
    0, 0, // e_shnum
    0, 0, // e_shstrndx
    // Program header (PT_LOAD)
    1, 0, 0, 0, // p_type (PT_LOAD)
    5, 0, 0, 0, // p_flags (R+X)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // p_offset
    0x78, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, // p_vaddr (0x400078)
    0x78, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, // p_paddr
    0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // p_filesz (8)
    0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // p_memsz (8)
    0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // p_align (0x1000)
    // Code at 0x400078 (just ret)
    0xC3, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, // ret + padding
    // Padding to 128 bytes
};

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
        rust_vga_set_color(0x0B);
        rust_vga_print("[Task1] Hello from Task 1 - Count: ");
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
        rust_vga_print(count_str);
        rust_vga_print("\n");
        for (volatile int i = 0; i < 10000000; i++);
        count++;
    }
    rust_vga_print("[Task1] Task 1 completed!\n");
    task_exit();
}

void demo_task2() {
    int count = 0;
    while (count < 10) {
        rust_vga_set_color(0x0E);
        rust_vga_print("[Task2] Hello from Task 2 - Count: ");
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
        rust_vga_print(count_str);
        rust_vga_print("\n");
        for (volatile int i = 0; i < 10000000; i++);
        count++;
    }
    rust_vga_print("[Task2] Task 2 completed!\n");
    task_exit();
}

// Minimal hex print for 64-bit values
void print_hex64(unsigned long val) {
    char buf[17];
    for (int i = 0; i < 16; i++) {
        int shift = (15 - i) * 4;
        int digit = (val >> shift) & 0xF;
        buf[i] = (digit < 10) ? ('0' + digit) : ('A' + digit - 10);
    }
    buf[16] = 0;
    serial_write(buf);
}
// Minimal decimal print for 64-bit values
void print_dec64(unsigned long val) {
    char buf[21];
    int i = 20;
    buf[i--] = 0;
    if (val == 0) buf[i--] = '0';
    while (val > 0 && i >= 0) {
        buf[i--] = '0' + (val % 10);
        val /= 10;
    }
    serial_write(&buf[i+1]);
}

void kernel_main(uint64_t mb2_info_ptr) {
    serial_write("[DEBUG] Entering kernel_main\n");
    // VGA direct print (always enabled)
    volatile uint16_t* vga = (uint16_t*)0xB8000;
    for (int i = 0; i < 80 * 25; i++) vga[i] = 0x0F20;
    const char* msg = "KERNEL STARTED - 64BIT MODE WORKING!";
    for (int i = 0; msg[i]; i++) vga[i] = 0x0A00 | msg[i];

    // VGA init/clear
    rust_vga_clear();
    rust_vga_set_color(0x0A);
    rust_vga_print("ShadeOS v0.1\n");
    rust_vga_set_color(0x0F);
    rust_vga_print("======================================\n\n");

    // Print Multiboot2 info pointer
    for (int i = 60; i >= 0; i -= 4) {
        uint8_t digit = (mb2_info_ptr >> i) & 0xF;
        vga_putchar(digit < 10 ? '0' + digit : 'A' + digit - 10);
    }

    //Parse Multiboot2 memory map
    parse_multiboot2_memory_map(mb2_info_ptr);

    // Physical memory manager
    pmm_init(mb2_info_ptr);
    rust_vga_print("[BOOT] Total memory: ");
    uint64_t total = pmm_total_memory();
    for (int i = 60; i >= 0; i -= 4) {
        uint8_t digit = (total >> i) & 0xF;
        vga_putchar(digit < 10 ? '0' + digit : 'A' + digit - 10);
    }
    rust_vga_print(" bytes\n");
    rust_vga_print("[BOOT] Free memory: ");
    uint64_t free = pmm_free_memory();
    for (int i = 60; i >= 0; i -= 4) {
        uint8_t digit = (free >> i) & 0xF;
        vga_putchar(digit < 10 ? '0' + digit : 'A' + digit - 10);
    }
    rust_vga_print(" bytes\n");

    // Paging
    paging_init();

    // Heap
    // heap_init(); // Remove C heap init
    init_heap();    // Call Rust heap init

    // Timer
    timer_init(100); // 100 Hz

    // Serial
    serial_init();
    rust_vga_print("[BOOT] paging, heap allocator, PIT timer, serial port (COM1) initialized (100 Hz)\n");
    serial_write("[BOOT] paging, heap allocator, PIT timer, serial port (COM1) initialized (100 Hz)\n");

    // GDT/IDT
    gdt_init();
    idt_init();
    
    // PIC
    pic_init();

    // Keyboard
    initialize_keyboard();
    rust_vga_print("[BOOT] GDT & IDT, PIC, Keyboard driver initialized\n");
    
    rust_vga_print("[BOOT] Kernel loaded successfully!\n");

    // Block Device (for Rust VFS)
    blockdev_init(); // Initialize the C ramdisk block device
    rust_vga_print("[BOOT] Block Devices initialized.\n");

    // Device framework + Network devices
    extern void device_framework_init(void);
    extern void netdev_init(void);
    extern void arp_init(void);
    extern void icmp_init(void);
    extern void tcp_init(void);
    device_framework_init();
    netdev_init();
    arp_init();
    icmp_init();
    tcp_init();

    // PCI bus
    extern void pci_init(void);
    pci_init();
    serial_write("[DEBUG] PCI init completed successfully\n");
    rust_vga_print("[DEBUG] PCI init completed successfully\n");
    
    // Add memory barrier to prevent compiler optimization issues
    __asm__ volatile("" ::: "memory");
    
    serial_write("[DEBUG] About to call rtl8139_init\n");

    // Network
    rtl8139_init();
    serial_write("[BOOT] RTL8139 network driver initialized\n");
    struct ip_addr ip = { {10, 0, 2, 15} };
    net_init(ip);
    rust_vga_print("[BOOT] Network stack initialized\n");

    // TCP Test: Attempt to fetch a page from the QEMU host
    serial_write("[TEST] Starting TCP HTTP GET test...\n");
    // uint8_t qemu_host_ip[4] = {10, 0, 2, 2};
    // http_get(qemu_host_ip, "10.0.2.2", "/");
    serial_write("[TEST] TCP test finished.\n");

    // Multitasking    
    task_init();
    rust_vga_print("[BOOT] Task system initialized\n");
    serial_write("[BOOT] Task system initialized\n");

    // VFS
    rust_vfs_init();
    rust_vga_print("[BOOT] VFS initialized\n");
    serial_write("[BOOT] VFS initialized\n");

    // Process management
    rust_process_init();
    rust_vga_print("[BOOT] Process management initialized\n");
    serial_write("[BOOT] Process management initialized\n");

    // System calls  
    rust_syscall_init();
    rust_vga_print("[BOOT] System call interface initialized\n");
    serial_write("[BOOT] System call interface initialized\n");

    // Create /bin and other directories
    rust_vga_print("[BOOT] Setting up filesystem structure...\n");
    serial_write("[BOOT] Setting up filesystem structure...\n");
    rust_vfs_mkdir("/bin\0");
    rust_vfs_mkdir("/usr\0");
    rust_vfs_mkdir("/usr/bin\0");
    rust_vfs_mkdir("/sbin\0");
    rust_vfs_mkdir("/usr/sbin\0");
    rust_vfs_mkdir("/etc\0");
    rust_vfs_mkdir("/home\0");
    rust_vfs_mkdir("/root\0");
    rust_vfs_mkdir("/tmp\0");
    rust_vfs_mkdir("/var\0");
    rust_vfs_mkdir("/dev\0");
    rust_vfs_mkdir("/proc\0");
    rust_vfs_mkdir("/sys\0");

    // Create bash binary
    rust_vfs_create_file("/bin/bash\0");
    static const char bash_binary[] = "/bin/bash\0";
    serial_write("[BOOT] Creating bash binary...\n");
    static const unsigned char test_elf_stub[128] = {
        0x7F, 'E', 'L', 'F', 2, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        2, 0, 0x3E, 0x00, 1, 0, 0, 0, 0x78, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0x40, 0x00, 0x38, 0x00, 0x01, 0x00, 0, 0, 0, 0, 0, 0,
        1, 0, 0, 0, 5, 0, 0, 0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x78, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x78, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC3, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90
    };
    uint64_t vfs_ret = rust_vfs_write(bash_binary, test_elf_stub, sizeof(test_elf_stub));
    serial_write("[BOOT] Bash binary created\n");

    // Create some basic files
    rust_vfs_create_file("/etc/passwd\0");
    rust_vfs_write("/etc/passwd\0", "root:x:0:0:root:/root:/bin/bash\n\0", 32);
    rust_vfs_create_file("/etc/hostname\0");
    rust_vfs_write("/etc/hostname\0", "shadeos\n\0", 9);

    // Syscalls
    syscall_init();
    rust_vga_print("[BOOT] Syscalls initialized\n");
    serial_write("[BOOT] Syscalls initialized\n");

    // Initialize Rust components
    rust_entry_point();

    // Clear keyboard buffer before starting shell
    extern void rust_keyboard_clear_buffer();
    rust_keyboard_clear_buffer();

    // Initialize bash shell
    rust_vga_print("[BOOT] Initializing Bash shell...\n");
    serial_write("[BOOT] Initializing Bash shell...\n");
    rust_bash_init();

    // Start the bash shell directly
    // rust_vga_print("[BOOT] Starting Bash shell...\n");
    //serial_write("[BOOT] Starting Bash shell.\n");
    rust_bash_run();
    rust_vga_set_color(0x0E);
    rust_vga_print("Welcome to ShadeOS - Linux-Compatible Kernel\n");
    rust_vga_print("Bash-compatible shell ready!\n");
    rust_vga_set_color(0x0F);
    rust_vga_print("\n");
}
