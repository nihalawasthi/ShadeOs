#include "syscall.h"
#include "vga.h"
#include "serial.h"
#include "task.h"

// Forward declarations for syscall implementations
static void syscall_write(const char* str);
static void syscall_exit();
static int syscall_getpid();

void syscall_init() {}

// Syscall handler: dispatch based on syscall number in rax
void syscall_handler(uint64_t syscall_num, uint64_t arg1, uint64_t arg2, uint64_t arg3) {
    switch (syscall_num) {
        case SYS_WRITE:
            syscall_write((const char*)arg1);
            break;
        case SYS_EXIT:
            syscall_exit();
            break;
        case SYS_YIELD:
            task_yield();
            break;
        case 4: // SYS_GETPID
        {
            int pid = syscall_getpid();
            // Return value in RAX: handled by assembly stub
            // (could store to a global or stack if needed)
            (void)pid;
            break;
        }
        default:
            vga_print("[SYSCALL] Unknown syscall\n");
            serial_write("[SYSCALL] Unknown syscall\n");
            break;
    }
}

static void syscall_write(const char* str) {
        vga_print(str);
        serial_write(str);
}

static void syscall_exit() {
    task_exit();
}

static int syscall_getpid() {
    if (current) return current->id;
    return -1;
    }

// Assembly stub for int 0x80 will be set up in IDT
