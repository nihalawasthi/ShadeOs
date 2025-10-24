#ifndef IDT_H
#define IDT_H

#include "kernel.h"

// Minimal interrupt register structure used by handlers. Handlers may ignore
// the contents; it's a compatibility stub.
typedef struct {
	uint64_t dummy;
} registers_t;

void idt_init();
void isr_handler(uint64_t int_no, uint64_t err_code);
extern void* isr_stub_table[256];

// Register a C-level interrupt handler for a given interrupt vector.
// Use vector numbers (e.g., IRQn + 32 for PIC IRQs).
void register_interrupt_handler(int n, void (*handler)(registers_t));

#endif
