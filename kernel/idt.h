#ifndef IDT_H
#define IDT_H

#include "kernel.h"

void idt_init();
void isr_handler(uint64_t int_no, uint64_t err_code);
extern void* isr_stub_table[256];

#endif
