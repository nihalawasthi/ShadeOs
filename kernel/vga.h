#ifndef VGA_H
#define VGA_H

#include "kernel.h"

void vga_init(void);
void vga_clear(void);
void vga_set_color(uint8_t color);
void vga_putchar(char c);
void vga_print(const char* str);

#endif // VGA_H
