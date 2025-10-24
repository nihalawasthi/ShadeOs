#include "stdint.h"
#include "keyboard.h"
#include "kernel.h" // For inb, outb, etc.
#include "vga.h"    // For vga_print, vga_putchar
#include "idt.h"    // For registers_t, register_interrupt_handler
#include "serial.h"
#include "helpers.h" // For sys_cli, sys_sti, pause

// --- Compatibility shims and missing definitions ---
#ifndef EOT
#define EOT 4
#endif
#ifndef IRQ1
#define IRQ1 33 // Standard PIC remap: IRQ1 = 0x21 = 33
#endif
#ifndef vga_print
void vga_print(const char*);
#endif
#ifndef vga_putchar
void vga_putchar(char);
#endif
// Remove static inline inb and outb definitions, since kernel.h provides them
#ifndef registers_t
// Minimal stub for registers_t if not defined
#endif
#ifndef register_interrupt_handler
#endif
// `registers_t` and `register_interrupt_handler` are provided by `idt.h`.
// --- End compatibility shims ---

// Remove C-side keyboard buffer and related state
// #define BUFFLEN 128
// uint8_t kb_buff[BUFFLEN];
// uint8_t kb_buff_hd = 0;
// uint8_t kb_buff_tl = 0;
// uint8_t shift = 0;
// uint8_t ctrl = 0;
// uint8_t keypresses[256] = {0};
// static int capslock = 0;

// Map kprint to vga_print and terminal_putchar to vga_putchar for compatibility
#define kprint vga_print
#define terminal_putchar vga_putchar

void poll_keyboard_input() {
    // This function is called by the interrupt handler.
    // It should read the scancode and pass it to the Rust keyboard module.
    uint8_t byte = inb(0x60);
    if (byte != 0) {
        rust_keyboard_put_scancode(byte);
    }
}

void keyboard_handler(registers_t regs) {
    (void)regs; // Silence unused parameter warning
    poll_keyboard_input();
    serial_write("IRQ1 fired\n");
}

// Ensure keyboard_interrupt_handler is defined at file scope
void keyboard_handler(registers_t regs); // Forward declaration
void keyboard_interrupt_handler(registers_t regs) {
    keyboard_handler(regs);
}

void initialize_keyboard() {
    kprint("Initializing keyboard.\n");

    outb(0x64, 0xFF);
    uint8_t status = inb(0x64);
    kprint("Got status after reset.\n");
    
    status = inb(0x64);
    if(status & (1 << 0)) {
        kprint("Output buffer full.\n");
    }
    else {
        kprint("Output buffer empty.\n");
    }

    if(status & (1 << 1)) {
        kprint("Input buffer full.\n");
    }
    else {
        kprint("Input buffer empty.\n");
    }

    if(status & (1 << 2)) {
        kprint("System flag set.\n");
    }
    else {
        kprint("System flag unset.\n");
    }

    if(status & (1 << 3)) {
        kprint("Command/Data -> PS/2 device.\n");
    }
    else {
        kprint("Command/Data -> PS/2 controller.\n");
    }

    if(status & (1 << 6)) {
        kprint("Timeout error.\n");
    }
    else {
        kprint("No timeout error.\n");
    }

    if(status & (1 << 7)) {
        kprint("Parity error.\n");
    }
    else {
        kprint("No parity error.\n");
    }

    // Test the controller.
    outb(0x64, 0xAA);
    uint8_t result = inb(0x60);
    if(result == 0x55) {
        kprint("PS/2 controller test passed.\n");
    }
    else if(result == 0xFC) {
        kprint("PS/2 controller test failed.\n");
//        return;
    }
    else {
        kprint("PS/2 controller responded to test with unknown code.\n");
        kprint("Trying to continue.\n");
//        return;
    }

    // Check the PS/2 controller configuration byte.
    outb(0x64, 0x20);
    result = inb(0x60);
    kprint("PS/2 config byte.\n");

    kprint("Keyboard ready to go!\n\n");
}

// This function now calls into the Rust keyboard module
char get_ascii_char() {
    // The Rust function handles the loop, CLI/STI, and HLT
    return (char)rust_keyboard_get_char();
}
