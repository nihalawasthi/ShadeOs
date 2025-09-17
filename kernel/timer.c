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
static uint32_t pit_freq_hz = 100;

#define MAX_PERIODIC_TIMERS 16

typedef struct {
    void (*callback)(void);
    uint32_t interval_ms;
    uint64_t next_trigger;
    int active;
} periodic_timer_t;

static periodic_timer_t periodic_timers[MAX_PERIODIC_TIMERS];
static int num_periodic_timers = 0;

void timer_init(uint32_t frequency) {
    uint16_t divisor = (uint16_t)(PIT_FREQUENCY / frequency);
    outb(PIT_COMMAND, 0x36); // Channel 0, low/high byte, mode 3, binary
    outb(PIT_CHANNEL0, divisor & 0xFF); // Low byte
    outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF); // High byte
    timer_ticks = 0;
    pit_freq_hz = (frequency == 0) ? 100 : frequency;
    
    for (int i = 0; i < MAX_PERIODIC_TIMERS; i++) {
        periodic_timers[i].active = 0;
    }
    num_periodic_timers = 0;
}

// scale by 1024 to avoid floating point
static long load1 = 1024, load5 = 1024, load15 = 1024;  // ≈ 1.00 load

#define EXP_1   1884  // 0.9200 * 2048
#define EXP_5   2014  // 0.9835 * 2048
#define EXP_15  2035  // 0.9945 * 2048
#define FIXED_1 (1<<11)

void update_load_average() {
    int runnable_tasks = 1; // later: count real tasks

    load1  = (load1  * EXP_1  + runnable_tasks * (FIXED_1 - EXP_1))  >> 11;
    load5  = (load5  * EXP_5  + runnable_tasks * (FIXED_1 - EXP_5))  >> 11;
    load15 = (load15 * EXP_15 + runnable_tasks * (FIXED_1 - EXP_15)) >> 11;
}

long get_load1()  { return load1; }
long get_load5()  { return load5; }
long get_load15() { return load15; }

void timer_interrupt_handler() {
    timer_ticks++;

    
    uint64_t current_ms = kernel_uptime_ms();
    for (int i = 0; i < num_periodic_timers; i++) {
        if (periodic_timers[i].active && current_ms >= periodic_timers[i].next_trigger) {
            periodic_timers[i].callback();
            periodic_timers[i].next_trigger = current_ms + periodic_timers[i].interval_ms;
        }
    }
    if (timer_ticks % pit_freq_hz == 0) {
        update_load_average();
    }
    // Call task scheduler for preemptive multitasking
    timer_task_handler();
    }

// Wrapper function for the interrupt handler
void timer_interrupt_wrapper(registers_t regs) {
    (void)regs; // Silence unused parameter warning
    timer_interrupt_handler();
}

uint64_t timer_get_ticks() { return timer_ticks; }
// Returns seconds since boot
uint64_t timer_get_seconds() {
    return timer_ticks / pit_freq_hz;
}

// Naive date (assumes boot = 1 Jan 1970 UTC)
void timer_get_date(int *year, int *month, int *day, int *hour, int *minute, int *second) {
    static const int days_in_month[] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
    uint64_t secs = timer_get_seconds();

    *second = secs % 60;
    secs /= 60;
    *minute = secs % 60;
    secs /= 60;
    *hour   = (secs % 24);
    secs /= 24;

    int days = (int)secs;
    *year = 1970;
    *month = 1;
    *day = 1;

    while (days >= 365) {
        days -= 365;
        (*year)++;
    }

    for (int m=0; m<12; m++) {
        if (days < days_in_month[m]) break;
        days -= days_in_month[m];
        (*month)++;
    }
    *day += days;
}

uint64_t kernel_uptime_ms(void) {
    /* Convert ticks to milliseconds using current PIT frequency. */
    if (pit_freq_hz == 0) return 0;
    uint64_t ticks = timer_ticks;
    return (ticks * 1000ULL) / (uint64_t)pit_freq_hz;
}

void timer_register_periodic(void (*callback)(void), uint32_t interval_ms) {
    if (num_periodic_timers >= MAX_PERIODIC_TIMERS || callback == NULL) {
        return; // No space or invalid callback
    }
    
    periodic_timers[num_periodic_timers].callback = callback;
    periodic_timers[num_periodic_timers].interval_ms = interval_ms;
    periodic_timers[num_periodic_timers].next_trigger = kernel_uptime_ms() + interval_ms;
    periodic_timers[num_periodic_timers].active = 1;
    num_periodic_timers++;
}
