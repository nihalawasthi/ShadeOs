#include "task.h"
#include "kernel.h"
#include "vga.h"
#include "serial.h"
#include "gdt.h"
#include "timer.h"
#include "paging.h"
#include "syscall.h" // For sys_pipe, sys_read, sys_write, sys_close
#include <string.h>  // For strlen

// FFI declarations for Rust tasking
extern void rust_task_init();
extern int rust_task_create(void (*entry)(void));
extern int rust_task_create_user(void (*entry)(void), void* user_stack, int stack_size, void* arg);
extern void rust_task_yield();
extern void rust_task_exit();
extern void rust_task_schedule();

void task_init() { rust_task_init(); }
int task_create(void (*entry)(void)) { return rust_task_create(entry); }
int task_create_user(void (*entry)(void), void* user_stack, int stack_size, void* arg) { return rust_task_create_user(entry, user_stack, stack_size, arg); }
void task_yield() { rust_task_yield(); }
void task_exit() { rust_task_exit(); }
void task_schedule() { rust_task_schedule(); }

// Assembly context switch (save/restore rsp)
__attribute__((naked)) void task_switch(uint64_t* /*old_rsp*/, uint64_t /*new_rsp*/) {
    __asm__ volatile (
        "movq %rsp, (%rdi)\n"
        "movq %rsi, %rsp\n"
        // Load new CR3 if needed
        "movq current, %rax\n"
        "movq (%rax), %rbx\n" // current task pointer
        // Load new CR3 if needed. rbx holds the address of the next task's struct.
        "movq current, %rax\n"    // rax = &current
        "movq (%rax), %rbx\n"     // rbx = current (the task_t* of the next task)
        "movq %cr3, %rcx\n"
        "cmpq 0x20(%rbx), %rcx\n" // Compare current cr3 with next task's cr3 (offset 32)
        "je 1f\n"
        "movq 0x20(%rbx), %rcx\n" // Load new cr3
        "movq %rcx, %cr3\n"
        "1:\n"
        "ret\n"
    );
}

__attribute__((naked)) void enter_user_mode(uint64_t /*rsp*/) {
    __asm__ volatile (
        "movq %rdi, %rsp\n"
        "pop %rax\n" // SS
        "pop %rbx\n" // RSP
        "pop %rcx\n" // RFLAGS
        "pop %rdx\n" // CS
        "pop %rsi\n" // RIP
        "push %rax\n"
        "push %rbx\n"
        "push %rcx\n"
        "push %rdx\n"
        "push %rsi\n"
        "iretq\n"
    );
}

// Timer interrupt handler for preemptive scheduling
void timer_task_handler() {
    // For now, just use cooperative multitasking
    // Preemptive scheduling can be added later
    rust_scheduler_tick();
}

// Dummy function to force linker to include Rust FFI statics and functions
void rust_task_link_force() {
    extern void rust_task_init();
    extern int rust_task_create(void (*entry)(void));
    extern int rust_task_create_user(void (*entry)(void), void* user_stack, int stack_size, void* arg);
    extern void rust_task_yield();
    extern void rust_task_exit();
    extern void rust_task_schedule();
    extern task_t* current;
    extern task_t* TASKS;
    extern int* NUM_TASKS;
    extern size_t* RUST_MAX_TASKS;
    volatile void* p = current;
    p = TASKS;
    p = NUM_TASKS;
    p = RUST_MAX_TASKS;
    rust_task_init();
    rust_task_create((void*)0);
    rust_task_create_user((void*)0, 0, 0, 0);
    rust_task_yield();
    rust_task_exit();
    rust_task_schedule();
    (void)p;
}

void scheduler_sleep(void* wait_channel) {
    // In a real implementation, you'd add the current task to a wait queue
    // associated with wait_channel. For now, just yield.
    (void)wait_channel;
    task_yield();
}

void scheduler_wakeup(void* wait_channel) {
    (void)wait_channel;
    task_schedule();
}

// static int pipe_fds[2];
// void ipc_writer_task() {
//     const char* message = "IPC pipe test from kernel OK";
//     serial_write("[IPC Test - Writer] Task started.\n");
//     syscall1(SYS_CLOSE, pipe_fds[0]);
//     serial_write("[IPC Test - Writer] Writing message...\n");
//     int bytes_written = syscall3(SYS_WRITE, pipe_fds[1], (uint64_t)message, strlen(message) + 1);
//     if (bytes_written > 0) {
//         serial_write("[IPC Test - Writer] Write successful.\n");
//     } else {
//         serial_write("[IPC Test - Writer] Write FAILED.\n");
//     }
//     syscall1(SYS_CLOSE, pipe_fds[1]);
//     serial_write("[IPC Test - Writer] Task finished.\n");
// }

// void ipc_reader_task() {
//     char buffer[100] = {0};
//     const char* expected_message = "IPC pipe test from kernel OK";
//     serial_write("[IPC Test - Reader] Task started.\n");
//     syscall1(SYS_CLOSE, pipe_fds[1]);
//     serial_write("[IPC Test - Reader] Waiting to read from pipe (this should block)...\n");
//     int bytes_read = syscall3(SYS_READ, pipe_fds[0], (uint64_t)buffer, sizeof(buffer) - 1);
//     if (bytes_read > 0) {
//         serial_write("[IPC Test - Reader] Read successful. Verifying message...\n");
//         if (strcmp(buffer, expected_message) == 0) {
//             serial_write("--- Kernel IPC Test: PASSED ---\n");
//         } else {
//             serial_write("--- Kernel IPC Test: FAILED (Message mismatch) ---\n");
//         }
//     } else {
//         serial_write("--- Kernel IPC Test: FAILED (Read error) ---\n");
//     }
//     syscall1(SYS_CLOSE, pipe_fds[0]);
//     serial_write("[IPC Test - Reader] Task finished.\n");
// }

// void ipc_test() {
//     serial_write("\n--- Starting Kernel IPC Pipe Test ---\n");

//     if (syscall1(SYS_PIPE, (uint64_t)pipe_fds) == -1) {
//         serial_write("[IPC Test] FAILED: Could not create pipe.\n");
//         return;
//     }
//     rust_task_create(ipc_writer_task);
//     rust_task_create(ipc_reader_task);
//     serial_write("[IPC Test] Writer and Reader tasks created. Yielding CPU...\n");

//     // Force a context switch to allow the new tasks to run.
//     syscall0(SYS_SCHED_YIELD);
// }