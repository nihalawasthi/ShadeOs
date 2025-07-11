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

int task_create_user(void (*entry)(void), void* user_stack, int stack_size, void* arg) {
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_TERMINATED) {
            tasks[i].state = TASK_READY;
            tasks[i].id = i;
            tasks[i].rip = (uint64_t)entry;
            tasks[i].user_mode = 1;
            // Set up user stack (top of provided stack)
            uint64_t* stack_top = (uint64_t*)((uint8_t*)user_stack + stack_size);
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

void task_schedule() {
    while (1) {
        task_yield();
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