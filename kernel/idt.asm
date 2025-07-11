; kernel/idt.asm - 256 ISR stubs for all interrupts
BITS 64

section .text

extern isr_handler

global isr_stub_table
isr_stub_table:
%assign i 0
%rep 256
    dq isr_stub_ %+ i
%assign i i+1
%endrep

global idt_flush
idt_flush:
    mov rax, rdi
    lidt [rax]
    ret

%macro ISR_STUB 1
    global isr_stub_%1
isr_stub_%1:
    push 0                ; Push dummy error code for consistency
    push %1               ; Push interrupt number
    mov rdi, [rsp]        ; rdi = int_no
    mov rsi, [rsp+8]      ; rsi = err_code
    call isr_handler
    add rsp, 16           ; Clean up stack
    iretq
%endmacro

%assign i 0
%rep 256
    ISR_STUB i
%assign i i+1
%endrep
