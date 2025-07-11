#ifndef SYSCALL_H
#define SYSCALL_H

#include "kernel.h"

// Syscall numbers
#define SYS_WRITE 1
#define SYS_YIELD 2
#define SYS_EXIT  3

void syscall_init();
void syscall_handler(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3);

// User-mode syscall stubs
static inline void sys_yield() {
    __asm__ volatile ("mov $2, %%rax; int $0x80" ::: "rax");
}
static inline void sys_exit() {
    __asm__ volatile ("mov $3, %%rax; int $0x80" ::: "rax");
}
static inline void sys_write(const char* str) {
    __asm__ volatile (
        "mov $1, %%rax; mov %0, %%rdi; int $0x80"
        : : "r"(str) : "rax", "rdi");
}

#endif 