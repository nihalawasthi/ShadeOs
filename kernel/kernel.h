// kernel/kernel.h - Main kernel header
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

// Port I/O
uint8_t inb(uint16_t port);
void outb(uint16_t port, uint8_t data);
uint16_t inw(uint16_t port);
void outw(uint16_t port, uint16_t data);

#endif
