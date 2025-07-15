#ifndef TASK_H
#define TASK_H

#include "kernel.h"

#define MAX_TASKS 16
#define TASK_STACK_SIZE 16384 // Increased from 4096

typedef enum { TASK_RUNNING, TASK_READY, TASK_BLOCKED, TASK_TERMINATED } task_state_t;

typedef struct task {
  uint64_t rsp; // Stack pointer
  uint64_t rip; // Instruction pointer (for new tasks)
  uint8_t stack[TASK_STACK_SIZE];
  task_state_t state;
  int id;
  int user_mode; // 1=user, 0=kernel
  int priority; // Lower value = higher priority
  uint64_t cr3; // Per-process page table (PML4) physical address
  struct task* next;
} task_t;

extern task_t* current;

void task_init();
int task_create(void (*entry)(void));
int task_create_user(void (*entry)(void), void* user_stack, int stack_size, void* arg);
void task_yield();
void task_exit();
void task_schedule();
void timer_task_handler();

#ifdef __cplusplus
extern "C" {
#endif
void rust_scheduler_tick();
#ifdef __cplusplus
}
#endif

#endif
