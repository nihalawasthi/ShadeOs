#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "kernel.h"

void keyboard_init();
void keyboard_interrupt_handler();
int keyboard_getchar(); // Returns ASCII char or -1 if buffer empty

#endif 