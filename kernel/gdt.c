// kernel/gdt.c - Global Descriptor Table
#include "kernel.h"
#include "serial.h"

struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct gdt_entry gdt[5];
static struct gdt_ptr gdt_pointer;

extern void gdt_flush(uint64_t);

static void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[num].base_low = (base & 0xFFFF);
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high = (base >> 24) & 0xFF;
    
    gdt[num].limit_low = (limit & 0xFFFF);
    gdt[num].granularity = (limit >> 16) & 0x0F;
    gdt[num].granularity |= gran & 0xF0;
    gdt[num].access = access;
}

void gdt_init() {
    serial_write("[GDT] Starting GDT initialization\n");
    
    serial_write("[GDT] Setting up GDT pointer\n");
    gdt_pointer.limit = (sizeof(struct gdt_entry) * 5) - 1;
    gdt_pointer.base = (uint64_t)&gdt;
    serial_write("[GDT] GDT pointer setup complete\n");
    
    serial_write("[GDT] Setting up null segment\n");
    gdt_set_gate(0, 0, 0, 0, 0);                // Null segment
    serial_write("[GDT] Null segment set\n");
    
    serial_write("[GDT] Setting up kernel code segment\n");
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xAF); // Code segment (64-bit)
    serial_write("[GDT] Kernel code segment set\n");
    
    serial_write("[GDT] Setting up kernel data segment\n");
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF); // Data segment
    serial_write("[GDT] Kernel data segment set\n");
    
    serial_write("[GDT] Setting up user code segment\n");
    gdt_set_gate(3, 0, 0xFFFFFFFF, 0xFA, 0xAF); // User code segment
    serial_write("[GDT] User code segment set\n");
    
    serial_write("[GDT] Setting up user data segment\n");
    gdt_set_gate(4, 0, 0xFFFFFFFF, 0xF2, 0xCF); // User data segment
    serial_write("[GDT] User data segment set\n");
    
    serial_write("[GDT] About to call gdt_flush\n");
    gdt_flush((uint64_t)&gdt_pointer);
    serial_write("[GDT] gdt_flush completed\n");
    
    serial_write("[GDT] GDT initialization complete\n");
}

// Export selectors for user mode
uint16_t gdt_kernel_code = 0x08;
uint16_t gdt_kernel_data = 0x10;
uint16_t gdt_user_code = 0x18;
uint16_t gdt_user_data = 0x20;
