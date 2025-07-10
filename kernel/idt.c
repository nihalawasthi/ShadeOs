// kernel/idt.c - Interrupt Descriptor Table
#include "kernel.h"

struct idt_entry {
    uint16_t base_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t flags;
    uint16_t base_middle;
    uint32_t base_high;
    uint32_t reserved;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct idt_entry idt[256];
static struct idt_ptr idt_pointer;

extern void idt_flush(uint64_t);
extern void isr0();  // Division by zero
extern void isr1();  // Debug
extern void isr2();  // NMI
// ... more ISRs would be defined

static void idt_set_gate(uint8_t num, uint64_t base, uint16_t selector, uint8_t flags) {
    idt[num].base_low = base & 0xFFFF;
    idt[num].base_middle = (base >> 16) & 0xFFFF;
    idt[num].base_high = (base >> 32) & 0xFFFFFFFF;
    idt[num].selector = selector;
    idt[num].ist = 0;
    idt[num].flags = flags;
    idt[num].reserved = 0;
}

void idt_init() {
    idt_pointer.limit = sizeof(struct idt_entry) * 256 - 1;
    idt_pointer.base = (uint64_t)&idt;
    
    memset(&idt, 0, sizeof(struct idt_entry) * 256);
    
    // Set up exception handlers
    idt_set_gate(0, (uint64_t)isr0, 0x08, 0x8E);
    idt_set_gate(1, (uint64_t)isr1, 0x08, 0x8E);
    idt_set_gate(2, (uint64_t)isr2, 0x08, 0x8E);
    // ... more ISRs would be set up
    
    idt_flush((uint64_t)&idt_pointer);
}

void isr_handler(uint64_t interrupt_number) {
    // Convert interrupt number to string and display
    vga_print("Interrupt received: ");
    
    // Simple number to string conversion for debugging
    if (interrupt_number < 10) {
        char num_str[2] = {'0' + interrupt_number, '\0'};
        vga_print(num_str);
    } else {
        vga_print("0x");
        // TODO: Implement proper hex conversion
        vga_print("??");
    }
    vga_print("\n");
}
