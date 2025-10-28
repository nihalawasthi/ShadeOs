#include "kernel.h"
#include "pmm.h"
#include "paging.h"
#include "heap.h"
#include "timer.h"
#include "keyboard.h"
#include "http.h"
#include "serial.h"
#include "vfs.h"
#include "net.h"
#include "task.h"
#include "syscall.h"
#include "blockdev.h" // Needed for blockdev_get in Rust FFI
#include <stdbool.h>

typedef unsigned int u32;
typedef unsigned char u8;

// FFI declarations for the kernel's internal socket API
extern int sock_socket(void);
extern int sock_connect(int s, const u8* ip, unsigned short port);
extern int sock_close(int s);

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
// static const unsigned char test_elf_stub[128] = {
//     0x7F, 'E', 'L', 'F', // Magic
//     2, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 64-bit, LSB, version, padding
//     2, 0, // ET_EXEC
//     0x3E, 0x00, // EM_X86_64
//     1, 0, 0, 0, // EV_CURRENT
//     0x78, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, // e_entry (0x400078)
//     0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // e_phoff (64)
//     0, 0, 0, 0, 0, 0, 0, 0, // e_shoff
//     0, 0, 0, 0, // e_flags
//     0x40, 0x00, // e_ehsize (64)
//     0x38, 0x00, // e_phentsize (56)
//     0x01, 0x00, // e_phnum (1)
//     0, 0, // e_shentsize
//     0, 0, // e_shnum
//     0, 0, // e_shstrndx
//     // Program header (PT_LOAD)
//     1, 0, 0, 0, // p_type (PT_LOAD)
//     5, 0, 0, 0, // p_flags (R+X)
//     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // p_offset
//     0x78, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, // p_vaddr (0x400078)
//     0x78, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, // p_paddr
//     0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // p_filesz (8)
//     0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // p_memsz (8)
//     0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // p_align (0x1000)
//     // Code at 0x400078 (just ret)
//     0xC3, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, // ret + padding
//     // Padding to 128 bytes
// };

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
    // Debug: print current PIC masks
    {
        uint8_t master_mask = inb(0x21);
        uint8_t slave_mask = inb(0xA1);
        serial_write("[PIC] Master mask=0x"); serial_write_hex("", master_mask); serial_write("\n");
        serial_write("[PIC] Slave mask=0x"); serial_write_hex("", slave_mask); serial_write("\n");
    }
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
    volatile uint16_t* vga = (uint16_t*)0xB8000;
    for (int i = 0; i < 80 * 25; i++) vga[i] = 0x0F20;
    const char* msg = "KERNEL STARTED - 64BIT MODE WORKING!";
    for (int i = 0; msg[i]; i++) vga[i] = 0x0A00 | msg[i];

    // VGA init/clear
    rust_vga_clear();
    serial_write("[KERNEL] Initializing ShadeOS v0.1\n");

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
    init_heap();
    // Timer
    timer_init(100);
    // Serial
    serial_init();
    // GDT/IDT
    gdt_init();
    idt_init();    
    // PIC
    pic_init();
    // Keyboard
    initialize_keyboard();
    // Block Devices
    blockdev_init();

    // Device framework + Network devices
    extern void device_framework_init(void);
    extern void netdev_init(void);
    extern void arp_init(void);
    extern void icmp_init(void);
    extern void tcp_init(void);
    device_framework_init();
    arp_init();
    icmp_init();
    tcp_init();

    // PCI bus
    extern void pci_init(void);
    pci_init();
    __asm__ volatile("" ::: "memory");
    
    // Network
    netdev_init();
    struct ip_addr ip = (struct ip_addr){{10, 0, 2, 15}};
    net_init(ip);
    /* Periodically poll NIC RX to feed network stack */
    extern void net_poll_rx(void);
    timer_register_periodic(net_poll_rx, 10);
    // Multitasking    
    task_init();
    // VFS
    rust_vfs_init();

    // Security/ACL init
    extern void sec_init(void);
    extern int acl_init(void);
    sec_init();
    acl_init();

    // Set basic ACLs for system paths
    extern int sec_set_acl(const char* path, unsigned int owner, unsigned int group, unsigned short mode);
    sec_set_acl("/\0", 0, 0, 0755);
    sec_set_acl("/etc\0", 0, 0, 0755);
    sec_set_acl("/bin\0", 0, 0, 0755);
    sec_set_acl("/usr\0", 0, 0, 0755);

    // Service manager
    extern int svc_init(void);
    svc_init();

    // Process management
    rust_process_init();
    // System calls  
    rust_syscall_init();

    // Create /bin and other directories
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
    extern int sec_set_acl(const char* path, unsigned int owner, unsigned int group, unsigned short mode);
    sec_set_acl("/bin/bash\0", 0, 0, 0755);
    static const char bash_binary[] = "/bin/bash\0";
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
    serial_write("[VFS] Initial ramdisk mounted, bash binary created.\n");

    // Create some basic files
    rust_vfs_create_file("/etc/passwd\0");
    sec_set_acl("/etc/passwd\0", 0, 0, 0644);
    rust_vfs_write("/etc/passwd\0", "root:x:0:0:root:/root:/bin/bash\n\0", 32);
    rust_vfs_create_file("/etc/hostname\0");
    sec_set_acl("/etc/hostname\0", 0, 0, 0644);
    rust_vfs_write("/etc/hostname\0", "shadeos\n\0", 9);

    // Syscalls
    syscall_init();
    rust_entry_point();

    // Clear keyboard buffer before starting shell
    extern void rust_keyboard_clear_buffer();
    rust_keyboard_clear_buffer();

    // Initialize bash shell
    serial_write("[CORE] Syscalls and Scheduler initialized.\n");
    rust_bash_init();

    // const char* url = "http://www.google.com\0";
    // unsigned char out[8192];  // buffer for response

    // int ret = http_get(url, out, sizeof(out));
    // if (ret > 0) {
    //     serial_write("HTTP GET successful, response:\n");
    //     for (int i = 0; i < ret; i++) {
    //         unsigned char s[2] = { out[i], 0 };
    //         serial_write((const char*)s);
    //     }
    //     serial_write("\n");
    // } else {
    //     serial_write("HTTP GET failed\n");
    // }

    rust_bash_run();
    rust_vga_print("\n");
}


