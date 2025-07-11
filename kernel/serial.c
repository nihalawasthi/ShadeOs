#include "serial.h"
#include "kernel.h"

#define COM1_PORT 0x3F8

void serial_init() {
    outb(COM1_PORT + 1, 0x00);    // Disable all interrupts
    outb(COM1_PORT + 3, 0x80);    // Enable DLAB (set baud rate divisor)
    outb(COM1_PORT + 0, 0x01);    // Set divisor to 1 (lo byte) 115200 baud
    outb(COM1_PORT + 1, 0x00);    //                  (hi byte)
    outb(COM1_PORT + 3, 0x03);    // 8 bits, no parity, one stop bit
    outb(COM1_PORT + 2, 0xC7);    // Enable FIFO, clear them, 14-byte threshold
    outb(COM1_PORT + 4, 0x0B);    // IRQs enabled, RTS/DSR set
}

int serial_received() {
    return inb(COM1_PORT + 5) & 1;
}

int serial_is_transmit_empty() {
    return inb(COM1_PORT + 5) & 0x20;
}

void serial_putchar(char c) {
    while (!serial_is_transmit_empty());
    outb(COM1_PORT, c);
}

int serial_getchar() {
    if (!serial_received()) return -1;
    return inb(COM1_PORT);
}

void serial_write(const char* str) {
    while (*str) serial_putchar(*str++);
} 