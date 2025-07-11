#include "syscall.h"
#include "idt.h"
#include "vga.h"
#include "serial.h"
#include "task.h"
#include "string.h"

extern void syscall_entry();

void syscall_init() {
    extern void* isr_stub_table[256];
    isr_stub_table[0x80] = syscall_entry;
}

void syscall_handler(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3) {
    if (num == SYS_WRITE) {
        const char* str = (const char*)arg1;
        vga_print(str);
        serial_write(str);
    } else if (num == SYS_YIELD) {
        task_yield();
    } else if (num == SYS_EXIT) {
        // Mark current task as terminated
        extern task_t* current;
        current->state = TASK_TERMINATED;
        task_yield();
    }
} 