#include "timer.h"
#include "kernel.h"
#include "serial.h"
#include "task.h"
#include "idt.h"

// Define registers_t if not already defined
#ifndef registers_t
typedef struct {
    unsigned long int dummy;
} registers_t;
#endif

// Forward declarations
void register_interrupt_handler(int n, void (*handler)(registers_t));
void timer_interrupt_wrapper(registers_t regs);

#define PIT_CHANNEL0 0x40
#define PIT_COMMAND  0x43
#define PIT_FREQUENCY 1193182

static volatile uint64_t timer_ticks = 0;

void timer_init(uint32_t frequency) {
    uint16_t divisor = (uint16_t)(PIT_FREQUENCY / frequency);
    outb(PIT_COMMAND, 0x36); // Channel 0, low/high byte, mode 3, binary
    outb(PIT_CHANNEL0, divisor & 0xFF); // Low byte
    outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF); // High byte
    timer_ticks = 0;
}

void timer_interrupt_handler() {
    timer_ticks++;
    if (timer_ticks % 100 == 0) {
        // vga_set_color(0x0B); // Cyan
        // vga_print("[TIMER] 100 ticks elapsed\n");
        // vga_set_color(0x0F); // White
    }
    
    // Call task scheduler for preemptive multitasking
    timer_task_handler();
    
    // EOI is sent by the main isr_handler
}

// Wrapper function for the interrupt handler
void timer_interrupt_wrapper(registers_t regs) {
    (void)regs; // Silence unused parameter warning
    timer_interrupt_handler();
}

uint64_t timer_get_ticks() {
    return timer_ticks;
} 