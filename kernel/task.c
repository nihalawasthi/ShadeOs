#include "task.h"
#include "kernel.h"
#include "vga.h"
#include "serial.h"
#include "gdt.h"

static task_t tasks[MAX_TASKS];
static int num_tasks = 0;
static task_t* current = 0;

extern void task_switch(uint64_t* old_rsp, uint64_t new_rsp);

void task_init() {
    for (int i = 0; i < MAX_TASKS; i++) tasks[i].state = TASK_TERMINATED;
    num_tasks = 0;
    current = 0;
}

int task_create(void (*entry)(void)) {
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_TERMINATED) {
            tasks[i].state = TASK_READY;
            tasks[i].id = i;
            tasks[i].rip = (uint64_t)entry;
            // Set up stack for new task
            uint64_t* stack_top = (uint64_t*)(tasks[i].stack + TASK_STACK_SIZE);
            *(--stack_top) = (uint64_t)entry; // Return address (RIP)
            tasks[i].rsp = (uint64_t)stack_top;
            // Add to round-robin list
            if (!current) {
                current = &tasks[i];
                tasks[i].next = &tasks[i];
            } else {
                task_t* last = current;
                while (last->next != current) last = last->next;
                last->next = &tasks[i];
                tasks[i].next = current;
            }
            num_tasks++;
            return i;
        }
    }
    return -1;
}

static void setup_user_stack_frame(task_t* t, void (*entry)(void), void* user_stack, int stack_size) {
    // Set up stack for iretq: SS, RSP, RFLAGS, CS, RIP
    uint64_t* stack_top = (uint64_t*)((uint8_t*)user_stack + stack_size);
    *(--stack_top) = gdt_user_data | 0x3; // SS
    *(--stack_top) = (uint64_t)user_stack + stack_size - 0x100; // RSP (user stack top - some space)
    *(--stack_top) = 0x202; // RFLAGS (IF=1)
    *(--stack_top) = gdt_user_code | 0x3; // CS
    *(--stack_top) = (uint64_t)entry; // RIP
    t->rsp = (uint64_t)stack_top;
}

int task_create_user(void (*entry)(void), void* user_stack, int stack_size, void* arg) {
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_TERMINATED) {
            tasks[i].state = TASK_READY;
            tasks[i].id = i;
            tasks[i].rip = (uint64_t)entry;
            tasks[i].user_mode = 1;
            setup_user_stack_frame(&tasks[i], entry, user_stack, stack_size);
            // Add to round-robin list
            if (!current) {
                current = &tasks[i];
                tasks[i].next = &tasks[i];
            } else {
                task_t* last = current;
                while (last->next != current) last = last->next;
                last->next = &tasks[i];
                tasks[i].next = current;
            }
            num_tasks++;
            return i;
        }
    }
    return -1;
}

void task_yield() {
    if (!current || !current->next) return;
    task_t* prev = current;
    do {
        current = current->next;
    } while (current->state != TASK_READY && current != prev);
    if (current != prev) {
        task_switch(&prev->rsp, current->rsp);
    }
}

// Assembly context switch (save/restore rsp)
__attribute__((naked)) void task_switch(uint64_t* old_rsp, uint64_t new_rsp) {
    __asm__ volatile (
        "mov [rdi], rsp\n"
        "mov rsp, rsi\n"
        "ret\n"
    );
}

__attribute__((naked)) void enter_user_mode(uint64_t rsp) {
    __asm__ volatile (
        "mov rsp, rdi\n"
        "pop rax\n" // SS
        "pop rbx\n" // RSP
        "pop rcx\n" // RFLAGS
        "pop rdx\n" // CS
        "pop rsi\n" // RIP
        "push rax\n"
        "push rbx\n"
        "push rcx\n"
        "push rdx\n"
        "push rsi\n"
        "iretq\n"
    );
}

void task_schedule() {
    int first = 1;
    while (1) {
        task_yield();
        if (current->user_mode && first) {
            first = 0;
            enter_user_mode(current->rsp);
        }
    }
} 