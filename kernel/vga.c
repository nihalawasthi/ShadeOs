#include "kernel.h"
#include "serial.h"

extern void rust_vga_print(const char* s);
extern void rust_vga_set_color(unsigned char color);
extern void rust_vga_clear();

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
