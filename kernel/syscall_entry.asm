BITS 64

section .text
global syscall_entry
extern syscall_handler

syscall_entry:
    push rdi
    push rsi
    push rdx
    push rcx
    push r8
    push r9
    push r10
    push r11
    mov rdi, rax
    call syscall_handler
    pop r11
    pop r10
    pop r9
    pop r8
    pop rcx
    pop rdx
    pop rsi
    pop rdi
    iretq 