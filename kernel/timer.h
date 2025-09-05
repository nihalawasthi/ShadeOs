#ifndef TIMER_H
#define TIMER_H

#include "kernel.h"

void timer_init(uint32_t frequency);
void timer_interrupt_handler();
uint64_t timer_get_ticks();
uint64_t kernel_uptime_ms(void);

#endif 
