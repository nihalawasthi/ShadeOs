// kernel/idt.c - Enhanced Interrupt Descriptor Table
#include "kernel.h"
#include "idt.h"

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

// Forward declarations for device handlers
void timer_interrupt_handler();
void keyboard_interrupt_handler();

static void idt_set_gate(uint8_t num, uint64_t base, uint16_t selector, uint8_t flags) {
    idt[num].base_low = base & 0xFFFF;
    idt[num].base_middle = (base >> 16) & 0xFFFF;
    idt[num].base_high = (base >> 32) & 0xFFFFFFFF;
    idt[num].selector = selector;
    idt[num].ist = 0;
    idt[num].flags = flags;
    idt[num].reserved = 0;
}

// Externally defined ISR stubs
extern void* isr_stub_table[256];

void idt_init() {
    idt_pointer.limit = sizeof(struct idt_entry) * 256 - 1;
    idt_pointer.base = (uint64_t)&idt;
    memset(&idt, 0, sizeof(struct idt_entry) * 256);
    for (int i = 0; i < 256; i++) {
        idt_set_gate(i, (uint64_t)isr_stub_table[i], 0x08, 0x8E);
    }
    idt_flush((uint64_t)&idt_pointer);
}

// Central interrupt handler
void isr_handler(uint64_t int_no, uint64_t err_code) {
    if (int_no == 32) {
        timer_interrupt_handler();
    } else if (int_no == 33) {
        keyboard_interrupt_handler();
    } else {
        vga_set_color(0x0C); // Red
        vga_print("[INTERRUPT] Unhandled interrupt: ");
        char num_str[4] = {0};
        num_str[0] = '0' + ((int_no / 100) % 10);
        num_str[1] = '0' + ((int_no / 10) % 10);
        num_str[2] = '0' + (int_no % 10);
        vga_print(num_str);
        vga_print(" (err: ");
        num_str[0] = '0' + ((err_code / 100) % 10);
        num_str[1] = '0' + ((err_code / 10) % 10);
        num_str[2] = '0' + (err_code % 10);
        vga_print(num_str);
        vga_print(")\n");
        vga_set_color(0x0F); // White
    }
}
