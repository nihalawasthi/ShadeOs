// Adapted from knusbaum/kernel keyboard driver. Cleaned for ShadeOS compatibility. All original references removed. Debug output uses vga_print only.
#include "stdint.h"
#include "keyboard.h"
#include "kernel.h" // For inb, outb, etc.
#include "vga.h"    // For vga_print, vga_putchar
#include "idt.h"    // For registers_t, register_interrupt_handler

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
typedef struct {
    unsigned long int dummy;
} registers_t;
#endif
#ifndef register_interrupt_handler
void register_interrupt_handler(int n, void (*handler)(registers_t));
#endif
// --- End compatibility shims ---

#ifndef ESC
#define ESC 27
#endif
#ifndef BS
#define BS 8
#endif

/*
 * Scan code   Key                         Scan code   Key                     Scan code   Key                     Scan code   Key
 * 0x01        escape pressed              0x02        1 pressed               0x03        2 pressed
 * 0x04        3 pressed                   0x05        4 pressed               0x06        5 pressed               0x07        6 pressed
 * 0x08        7 pressed                   0x09        8 pressed               0x0A        9 pressed               0x0B        0 (zero) pressed
 * 0x0C        - pressed                   0x0D        = pressed               0x0E        backspace pressed       0x0F        tab pressed
 * 0x10        Q pressed                   0x11        W pressed               0x12        E pressed               0x13        R pressed
 * 0x14        T pressed                   0x15        Y pressed               0x16        U pressed               0x17        I pressed
 * 0x18        O pressed                   0x19        P pressed               0x1A        [ pressed               0x1B        ] pressed
 * 0x1C        enter pressed               0x1D        left control pressed    0x1E        A pressed               0x1F        S pressed
 * 0x20        D pressed                   0x21        F pressed               0x22        G pressed               0x23        H pressed
 * 0x24        J pressed                   0x25        K pressed               0x26        L pressed               0x27        ; pressed
 * 0x28        ' (single quote) pressed    0x29        ` (back tick) pressed   0x2A        left shift pressed      0x2B        \ pressed
 * 0x2C        Z pressed                   0x2D        X pressed               0x2E        C pressed               0x2F        V pressed
 * 0x30        B pressed                   0x31        N pressed               0x32        M pressed               0x33        , pressed
 * 0x34        . pressed                   0x35        / pressed               0x36        right shift pressed     0x37        (keypad) * pressed
 * 0x38        left alt pressed            0x39        space pressed           0x3A        CapsLock pressed        0x3B        F1 pressed
 * 0x3C        F2 pressed                  0x3D        F3 pressed              0x3E        F4 pressed              0x3F        F5 pressed
 * 0x40        F6 pressed                  0x41        F7 pressed              0x42        F8 pressed              0x43        F9 pressed
 * 0x44        F10 pressed                 0x45        NumberLock pressed      0x46        ScrollLock pressed      0x47        (keypad) 7 pressed
 * 0x48        (keypad) 8 pressed          0x49        (keypad) 9 pressed      0x4A        (keypad) - pressed      0x4B        (keypad) 4 pressed
 * 0x4C        (keypad) 5 pressed          0x4D        (keypad) 6 pressed      0x4E        (keypad) + pressed      0x4F        (keypad) 1 pressed
 * 0x50        (keypad) 2 pressed          0x51        (keypad) 3 pressed      0x52        (keypad) 0 pressed      0x53        (keypad) . pressed
 * 0x57        F11 pressed                 0x58        F12 pressed
 */

// QWERTY scancode -> ASCII tables
const uint8_t lower_ascii_codes[256] = {
    0x00,  ESC,  '1',  '2',     /* 0x00 */
     '3',  '4',  '5',  '6',     /* 0x04 */
     '7',  '8',  '9',  '0',     /* 0x08 */
     '-',  '=',   BS, '\t',     /* 0x0C */
     'q',  'w',  'e',  'r',     /* 0x10 */
     't',  'y',  'u',  'i',     /* 0x14 */
     'o',  'p',  '[',  ']',     /* 0x18 */
    '\n', 0x00,  'a',  's',     /* 0x1C */
     'd',  'f',  'g',  'h',     /* 0x20 */
     'j',  'k',  'l',  ';',     /* 0x24 */
    '\'',  '`', 0x00, '\\',     /* 0x28 */
     'z',  'x',  'c',  'v',     /* 0x2C */
     'b',  'n',  'm',  ',',     /* 0x30 */
     '.',  '/', 0x00,  '*',     /* 0x34 */
    0x00,  ' ', 0x00, 0x00,     /* 0x38 */
    0x00, 0x00, 0x00, 0x00,     /* 0x3C */
    0x00, 0x00, 0x00, 0x00,     /* 0x40 */
    0x00, 0x00, 0x00,  '7',     /* 0x44 */
     '8',  '9',  '-',  '4',     /* 0x48 */
     '5',  '6',  '+',  '1',     /* 0x4C */
     '2',  '3',  '0',  '.',     /* 0x50 */
    0x00, 0x00, 0x00, 0x00,     /* 0x54 */
    0x00, 0x00, 0x00, 0x00      /* 0x58 */
};
const uint8_t upper_ascii_codes[256] = {
    0x00,  ESC,  '!',  '@',     /* 0x00 */
     '#',  '$',  '%',  '^',     /* 0x04 */
     '&',  '*',  '(',  ')',     /* 0x08 */
     '_',  '+',   BS, '\t',     /* 0x0C */
     'Q',  'W',  'E',  'R',     /* 0x10 */
     'T',  'Y',  'U',  'I',     /* 0x14 */
     'O',  'P',  '{',  '}',     /* 0x18 */
    '\n', 0x00,  'A',  'S',     /* 0x1C */
     'D',  'F',  'G',  'H',     /* 0x20 */
     'J',  'K',  'L',  ':',     /* 0x24 */
     '"',  '~', 0x00,  '|',     /* 0x28 */
     'Z',  'X',  'C',  'V',     /* 0x2C */
     'B',  'N',  'M',  '<',     /* 0x30 */
     '>',  '?', 0x00,  '*',     /* 0x34 */
    0x00,  ' ', 0x00, 0x00,     /* 0x38 */
    0x00, 0x00, 0x00, 0x00,     /* 0x3C */
    0x00, 0x00, 0x00, 0x00,     /* 0x40 */
    0x00, 0x00, 0x00,  '7',     /* 0x44 */
     '8',  '9',  '-',  '4',     /* 0x48 */
     '5',  '6',  '+',  '1',     /* 0x4C */
     '2',  '3',  '0',  '.',     /* 0x50 */
    0x00, 0x00, 0x00, 0x00,     /* 0x54 */
    0x00, 0x00, 0x00, 0x00      /* 0x58 */
};
// Remove Dvorak support and all references to Dvorak tables
// Ensure the driver always uses QWERTY mapping for both shifted and unshifted keys

// shift flags. left shift is bit 0, right shift is bit 1.
uint8_t shift;
// control flags just like shift flags.
uint8_t ctrl;
uint8_t keypresses[256];
static int capslock = 0;

#define BUFFLEN 128
// New characters are added to hd. characters are pulled off of tl.
uint8_t kb_buff[BUFFLEN];
uint8_t kb_buff_hd;
uint8_t kb_buff_tl;

// Map kprint to vga_print and terminal_putchar to vga_putchar for compatibility
#define kprint vga_print
#define terminal_putchar vga_putchar

static void poll_keyboard_input() {
    // See if there's room in the key buffer, else bug out.
    uint8_t next_hd = (kb_buff_hd + 1) % BUFFLEN;
    if(next_hd == kb_buff_tl) {
        return;
    }

    uint8_t byte = inb(0x60);
    if(byte == 0) {
        return;
    }

    if(byte & 0x80) {
        // Key release
        uint8_t pressedbyte = byte & 0x7F;
        // Check if we're releasing a shift key.
        if(pressedbyte == 0x2A) {
            // left
            shift = shift & 0x02;
        }
        else if(pressedbyte == 0x36) {
            // right
            shift = shift & 0x01;
        }
        else if(pressedbyte == 0x1D) {
            ctrl = 0;
        }

        keypresses[pressedbyte] = 0;
        return;
    }

    if(keypresses[byte] < 10 && keypresses[byte] > 0) {
        // Key is already pressed. Ignore it.
        keypresses[byte]++; // Increment anyway, so we can roll over and repeat.
        return;
    }
    keypresses[byte]++;

    if(byte == 0x2A) {
        shift = shift | 0x01;
        return;
    }
    else if(byte == 0x36) {
        shift = shift | 0x02;
        return;
    }
    else if(byte == 0x1D) {
        ctrl = 1;
        return;
    }
    else if (byte == 0x3A) { // Caps Lock pressed
        capslock = !capslock;
        return;
    }

    const uint8_t *codes;
    if(ctrl) {
        if(lower_ascii_codes[byte] == 'd') {
            // Ctrl+d
            kb_buff[kb_buff_hd] = EOT;
            kb_buff_hd = next_hd;
            return;
        }
    }
    else if(shift) {
        codes = upper_ascii_codes;
    }
    else {
        codes = lower_ascii_codes;
    }

    if (byte < 256) {
        uint8_t ascii;
        if (shift) {
            ascii = upper_ascii_codes[byte];
        } else if (capslock && lower_ascii_codes[byte] >= 'a' && lower_ascii_codes[byte] <= 'z') {
            ascii = lower_ascii_codes[byte] - 32; // Convert to uppercase
        } else if (capslock && lower_ascii_codes[byte] >= 'A' && lower_ascii_codes[byte] <= 'Z') {
            ascii = lower_ascii_codes[byte] + 32; // Convert to lowercase
        } else {
            ascii = lower_ascii_codes[byte];
        }
        if(ascii != 0) {
            kb_buff[kb_buff_hd] = ascii;
            kb_buff_hd = next_hd;
            return;
        }
    }
}

void keyboard_handler(registers_t regs) {
    (void)regs; // Silence unused parameter warning
    poll_keyboard_input();
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

    // Remove register_interrupt_handler(IRQ1, keyboard_handler);
    // Ensure keyboard_interrupt_handler is defined and calls keyboard_handler
    // void keyboard_handler(registers_t regs); // Forward declaration // This line is removed as per the edit hint
    // void keyboard_interrupt_handler(registers_t regs) { // This line is removed as per the edit hint
    //     keyboard_handler(regs); // This line is removed as per the edit hint
    // } // This line is removed as per the edit hint

    kprint("Keyboard ready to go!\n\n");
}

extern void pause();
extern void sys_cli();
extern void sys_sti();

// This can race with the keyboard_handler, so we have to stop interrupts while we check stuff.
char get_ascii_char() {

    while(1) {
        sys_cli();
        if(kb_buff_hd != kb_buff_tl) {
            char c = kb_buff[kb_buff_tl];
            kb_buff_tl = (kb_buff_tl + 1) % BUFFLEN;
            poll_keyboard_input();
            sys_sti();
    return c;
} 
        sys_sti();
        pause();
    }

}

// --- Begin compatibility for missing kernel symbols ---

// 1. Alias for keyboard_interrupt_handler
// void keyboard_handler(registers_t regs); // Forward declaration // This line is removed as per the edit hint
// void keyboard_interrupt_handler(registers_t regs) { // This line is removed as per the edit hint
//     keyboard_handler(regs); // This line is removed as per the edit hint
// } // This line is removed as per the edit hint

// 2. Wrapper for register_interrupt_handler
// Remove reference to idt_set_handler
// Implement sys_sti, sys_cli, and pause as real functions
void sys_sti() { __asm__ volatile ("sti"); }
void sys_cli() { __asm__ volatile ("cli"); }
void pause() { __asm__ volatile ("hlt"); }

// --- End compatibility for missing kernel symbols ---
