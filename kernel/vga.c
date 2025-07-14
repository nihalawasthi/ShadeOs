#include "kernel.h"

static uint16_t* vga_buffer = (uint16_t*)0xB8000;
static uint8_t vga_color = 0x0F; // White on black
static uint8_t vga_x = 0;
static uint8_t vga_y = 0;
static const uint8_t VGA_WIDTH = 80;
static const uint8_t VGA_HEIGHT = 25;

void vga_init() {
    vga_color = 0x0F;
    vga_x = 0;
    vga_y = 0;
}

void vga_clear() {
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga_buffer[i] = (vga_color << 8) | ' ';
    }
    vga_x = 0;
    vga_y = 0;
}

void vga_set_color(uint8_t color) {
    vga_color = color;
}

void vga_putchar(char c) {
    if (c == '\b') {
        if (vga_x > 0) {
            vga_x--;
            const int VGA_WIDTH = 80;
            volatile uint16_t* vga = (uint16_t*)0xB8000;
            vga[vga_y * VGA_WIDTH + vga_x] = (0x0F << 8) | ' ';
        }
        return;
    }
    if (c == '\n') {
        vga_x = 0;
        vga_y++;
    } else if (c == '\t') {
        vga_x = (vga_x + 4) & ~3;
    } else {
        vga_buffer[vga_y * VGA_WIDTH + vga_x] = (vga_color << 8) | c;
        vga_x++;
    }
    
    if (vga_x >= VGA_WIDTH) {
        vga_x = 0;
        vga_y++;
    }
    
    if (vga_y >= VGA_HEIGHT) {
        // Scroll up
        for (int i = 0; i < (VGA_HEIGHT - 1) * VGA_WIDTH; i++) {
            vga_buffer[i] = vga_buffer[i + VGA_WIDTH];
        }
        for (int i = (VGA_HEIGHT - 1) * VGA_WIDTH; i < VGA_HEIGHT * VGA_WIDTH; i++) {
            vga_buffer[i] = (vga_color << 8) | ' ';
        }
        vga_y = VGA_HEIGHT - 1;
    }
}

void vga_print(const char* str) {
    while (*str) {
        vga_putchar(*str++);
    }
}
