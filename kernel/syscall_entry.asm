BITS 64

section .text
global syscall_entry
extern rust_syscall_handler

syscall_entry:
    ; The user-space C code (syscall() in syscall.h) will place arguments in:
    ; RAX (num), RDI (arg1), RSI (arg2), RDX (arg3), R10 (arg4), R8 (arg5), R9 (arg6)

    ; The rust_syscall_handler is `extern "C"`, so it expects arguments
    ; according to the System V AMD64 ABI:
    ; RDI, RSI, RDX, RCX, R8, R9, [stack]
    ; We perform the necessary register shuffling here.
    mov rcx, r10      ; 4th argument (arg4) from R10
    ; R8 and R9 are already in place for arg5 and arg6.
    ; We must move the first three arguments last.
    mov rdx, rsi      ; 3rd argument (arg2)
    mov rsi, rdi      ; 2nd argument (arg1)
    mov rdi, rax      ; 1st argument (syscall_num)
    call rust_syscall_handler
    ; The return value is placed in RAX by the function call, which is correct for syscalls.
    iretq
