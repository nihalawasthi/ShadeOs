BITS 64

section .text
global syscall_entry
extern syscall_handler

syscall_entry:
    mov rcx, rdx      ; RCX = arg3
    mov rdx, rsi      ; RDX = arg2
    mov rsi, rdi      ; RSI = arg1
    mov rdi, rax      ; RDI = syscall number
    call syscall_handler
    iretq
