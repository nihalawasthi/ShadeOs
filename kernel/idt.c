#include "kernel.h"
#include "idt.h"
#include "serial.h"
#include "syscall.h"

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
    serial_write("[IDT] IDT pointer setup complete\n");
    
    memset(&idt, 0, sizeof(struct idt_entry) * 256);
    serial_write("[IDT] IDT array cleared\n");
    
    for (int i = 0; i < 256; i++) {
        if (i == 0) {
            serial_write("[IDT] Setting gate 0\n");
        }
        if (i == 32) {
            serial_write("[IDT] Setting gate 32 (timer)\n");
        }
        if (i == 33) {
            serial_write("[IDT] Setting gate 33 (keyboard)\n");
        }
        idt_set_gate(i, (uint64_t)isr_stub_table[i], 0x08, 0x8E);
        if (i == 0) {
            serial_write("[IDT] Gate 0 set\n");
        }
        if (i == 32) {
            serial_write("[IDT] Gate 32 set\n");
        }
        if (i == 33) {
            serial_write("[IDT] Gate 33 set\n");
        }
    }
    serial_write("[IDT] All gates set\n");
    // Set syscall gate (int 0x80) to syscall_entry
    extern void syscall_entry();
    idt_set_gate(0x80, (uint64_t)syscall_entry, 0x08, 0xEE);
    
    idt_flush((uint64_t)&idt_pointer);
    
    serial_write("[IDT] IDT initialization complete\n");
}

// Central interrupt handler
void isr_handler(uint64_t int_no, uint64_t err_code) {
    char int_str[9];
    for (int i = 0; i < 8; i++) {
        int nibble = (int_no >> ((7 - i) * 4)) & 0xF;
        int_str[i] = (nibble < 10) ? ('0' + nibble) : ('A' + nibble - 10);
    }
    int_str[8] = 0;
    char err_str[9];
    for (int i = 0; i < 8; i++) {
        int nibble = (err_code >> ((7 - i) * 4)) & 0xF;
        err_str[i] = (nibble < 10) ? ('0' + nibble) : ('A' + nibble - 10);
    }
    err_str[8] = 0;

    if (int_no == 6) {
        serial_write("[INTERRUPT] Invalid Opcode detected!\n");
        vga_set_color(0x0C);
        vga_print("[INTERRUPT] Invalid Opcode Exception!\n");
        vga_set_color(0x0F);
    }
    if (int_no == 14) {
        // Page Fault
        uint64_t cr2;
        __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
        serial_write("[INTERRUPT] Page Fault detected!\n");
        serial_write("[INTERRUPT] Faulting address: 0x");
        char addr_hex[17];
        for (int i = 0; i < 16; i++) {
            int nibble = (cr2 >> ((15 - i) * 4)) & 0xF;
            addr_hex[i] = (nibble < 10) ? ('0' + nibble) : ('A' + nibble - 10);
        }
        addr_hex[16] = 0;
        serial_write(addr_hex);
        serial_write("\n");
        serial_write("[INTERRUPT] Error code: ");
        char err_hex[17];
        for (int i = 0; i < 16; i++) {
            int nibble = (err_code >> ((15 - i) * 4)) & 0xF;
            err_hex[i] = (nibble < 10) ? ('0' + nibble) : ('A' + nibble - 10);
        }
        err_hex[16] = 0;
        serial_write(err_hex);
        serial_write("\n");
        vga_set_color(0x0C);
        vga_print("[INTERRUPT] Page Fault Exception!\n");
        vga_set_color(0x0F);
        // Check if fault from user mode (CS & 3 == 3)
        uint64_t cs;
        __asm__ volatile ("mov %%cs, %0" : "=r"(cs));
        if ((cs & 3) == 3) {
            vga_print("[PAGE FAULT] User process caused page fault. Killing process.\n");
            serial_write("[PAGE FAULT] User process killed.\n");
            extern void task_exit();
            task_exit();
            return;
        }
        // Kernel mode: panic
        while(1) { __asm__ volatile("hlt"); }
    }
    if (int_no == 32) {
        timer_interrupt_handler();
        outb(0x20, 0x20); // EOI to master PIC
        return;
    } else if (int_no == 33) {
        keyboard_interrupt_handler();
        outb(0x20, 0x20); // EOI to master PIC
        return;
    } else {
        vga_set_color(0x0C);
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
        vga_set_color(0x0F);
        // Panic: halt the system
        serial_write("[PANIC] Unhandled interrupt! Halting.\n");
        while(1) { __asm__ volatile("cli; hlt"); }
    }
}
