#ifndef KERNEL_H
#define KERNEL_H

// Standard integer types
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long uint64_t;
typedef unsigned long size_t;

// NULL definition
#ifndef NULL
#define NULL ((void*)0)
#endif

// VGA functions
void vga_init(void);
void vga_clear(void);
void vga_print(const char* str);
void vga_putchar(char c);
void vga_set_color(uint8_t color);

// GDT functions
void gdt_init(void);

// IDT functions  
void idt_init(void);

// Memory management
void memory_init(void);
void* kmalloc(size_t size);
void kfree(void* ptr);

// Utility functions
void* memset(void* dest, int val, size_t len);
void* memcpy(void* dest, const void* src, size_t len);
size_t strlen(const char* str);
int strcmp(const char* a, const char* b);
int memcmp(const void* s1, const void* s2, size_t n);
int snprintf(char* str, size_t size, const char* format, ...);
int sscanf(const char* str, const char* format, ...);


// Port I/O
uint8_t inb(uint16_t port);
void outb(uint16_t port, uint8_t data);
uint16_t inw(uint16_t port);
void outw(uint16_t port, uint16_t data);
uint32_t inl(uint16_t port);
void outl(uint16_t port, uint32_t data);

// Multiboot2 memory map parsing
void parse_multiboot2_memory_map(uint64_t mb2_info_ptr);

void kernel_main(uint64_t mb2_info_ptr);

// --- Rust VFS FFI Declarations ---
// These functions will be implemented in Rust and called from C
extern int rust_vfs_init();
extern int rust_vfs_ls(const char* path);
extern int rust_vfs_read(const char* path, void* buf, int max_len);
extern int rust_vfs_write(const char* path, const void* buf, int len);
extern int rust_vfs_mkdir(const char* path);
extern int rust_vfs_create_file(const char* path);
extern int rust_vfs_unlink(const char* path);
extern int rust_vfs_stat(const char* path, void* stat_out);

// --- Rust Keyboard FFI Declarations ---
extern void rust_keyboard_put_scancode(uint8_t scancode);
extern int rust_keyboard_get_char(); // Returns char or -1 if no data

#endif
