#ifndef SERIAL_H
#define SERIAL_H

#include "kernel.h"

void serial_init();
void serial_putchar(char c);
int serial_getchar(); // Returns char or -1 if no data
void serial_write(const char* str);

#endif 