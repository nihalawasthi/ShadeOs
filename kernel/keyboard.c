#include "keyboard.h"
#include "kernel.h"

#define KEYBOARD_DATA_PORT 0x60
#define KEYBOARD_STATUS_PORT 0x64
#define KEYBOARD_BUFFER_SIZE 128

static char key_buffer[KEYBOARD_BUFFER_SIZE];
static int buf_head = 0, buf_tail = 0;

// US QWERTY scancode to ASCII (set 1, only basic keys)
static const char scancode_table[128] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=', 8, 9,
    'q','w','e','r','t','y','u','i','o','p','[',']','\\', 0, 0,
    'a','s','d','f','g','h','j','k','l',';','\'','`', 0,'\\',
    'z','x','c','v','b','n','m',',','.','/', 0, '*', 0, ' ', 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

void keyboard_init() {
    // No special init needed for basic PS/2
    buf_head = buf_tail = 0;
}

void keyboard_interrupt_handler() {
    uint8_t status = inb(KEYBOARD_STATUS_PORT);
    if (!(status & 1)) return; // No data
    uint8_t scancode = inb(KEYBOARD_DATA_PORT);
    if (scancode & 0x80) return; // Key release
    char c = scancode_table[scancode & 0x7F];
    if (c && ((buf_head + 1) % KEYBOARD_BUFFER_SIZE != buf_tail)) {
        key_buffer[buf_head] = c;
        buf_head = (buf_head + 1) % KEYBOARD_BUFFER_SIZE;
    }
    // Send EOI to PIC
    outb(0x20, 0x20);
}

int keyboard_getchar() {
    if (buf_head == buf_tail) return -1;
    char c = key_buffer[buf_tail];
    buf_tail = (buf_tail + 1) % KEYBOARD_BUFFER_SIZE;
    return c;
} 