#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "kernel.h"

void initialize_keyboard();
char get_ascii_char();
void poll_keyboard_input(); // This will now call into Rust

// New FFI declarations for Rust keyboard functions
extern void rust_keyboard_put_scancode(uint8_t scancode);
extern int rust_keyboard_get_char(); // Returns char or -1 if no data

#endif
