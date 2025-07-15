#include "task.h"
#include "kernel.h"
#include "vga.h"
#include "serial.h"
#include "gdt.h"
#include "timer.h"
#include "paging.h"

static task_t tasks[MAX_TASKS];
static int num_tasks = 0;
task_t* current = 0;
static int scheduler_running = 0;
static int current_task_index = 0;

extern void task_switch(uint64_t* old_rsp, uint64_t new_rsp);
extern uint64_t* pml4_table;

void task_init() {
    for (int i = 0; i < MAX_TASKS; i++) tasks[i].state = TASK_TERMINATED;
    num_tasks = 0;
    current = 0;
    scheduler_running = 0;
    current_task_index = 0;
}

int task_create(void (*entry)(void)) {
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_TERMINATED) {
            tasks[i].state = TASK_READY;
            tasks[i].id = i;
            tasks[i].rip = (uint64_t)entry;
            tasks[i].user_mode = 0; // Kernel mode task
            // Set up stack for new task
            uint64_t* stack_top = (uint64_t*)(tasks[i].stack + TASK_STACK_SIZE);
            *(--stack_top) = (uint64_t)entry; // Return address (RIP)
            tasks[i].rsp = (uint64_t)stack_top;
            // Use kernel's PML4
            tasks[i].cr3 = (uint64_t)pml4_table;
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
            // Allocate a new PML4 for this user process
            tasks[i].cr3 = paging_new_pml4();
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

void task_exit() {
    if (current) {
        current->state = TASK_TERMINATED;
        num_tasks--;
        // Remove from linked list
        if (current->next == current) {
            // Only one task left
            current = 0;
        } else {
            task_t* prev = current;
            while (prev->next != current) prev = prev->next;
            prev->next = current->next;
            current = current->next;
        }
    }
}

// Assembly context switch (save/restore rsp)
__attribute__((naked)) void task_switch(uint64_t* old_rsp, uint64_t new_rsp) {
    __asm__ volatile (
        "movq %rsp, (%rdi)\n"
        "movq %rsi, %rsp\n"
        // Load new CR3 if needed
        "movq current, %rax\n"
        "movq (%rax), %rbx\n" // current task pointer
        "movq %cr3, %rcx\n"
        "cmpq 0x38(%rbx), %rcx\n" // offset of cr3 in task_t
        "je 1f\n"
        "movq 0x38(%rbx), %rcx\n"
        "movq %rcx, %cr3\n"
        "1:\n"
        "ret\n"
    );
}

__attribute__((naked)) void enter_user_mode(uint64_t rsp) {
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

void task_schedule() {
    if (scheduler_running) return; // Prevent recursive calls
    scheduler_running = 1;
    
    // Simple cooperative multitasking - run each task for a few iterations
    while (num_tasks > 0) {
        // Find next ready task
        task_t* task = 0;
        for (int i = 0; i < MAX_TASKS; i++) {
            if (tasks[i].state == TASK_READY) {
                task = &tasks[i];
                break;
            }
        }
        
        if (task) {
            task->state = TASK_RUNNING;
            // Execute the task function
            void (*entry)(void) = (void (*)(void))task->rip;
            entry();
            // Task should call task_exit() when done
        } else {
            // No more ready tasks
            break;
        }
    }
    
    // If we get here, all tasks have terminated
    scheduler_running = 0;
    vga_print("[SCHEDULER] All tasks terminated\n");
}

// Timer interrupt handler for preemptive scheduling
void timer_task_handler() {
    // For now, just use cooperative multitasking
    // Preemptive scheduling can be added later
    rust_scheduler_tick();
} 