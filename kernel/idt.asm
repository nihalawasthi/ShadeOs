; kernel/idt.asm - IDT assembly functions
BITS 64

global idt_flush
extern isr_handler

idt_flush:
    lidt [rdi]
    ret

; ISR macros
%macro ISR_NOERRCODE 1
global isr%1
isr%1:
    push 0          ; Push dummy error code
    push %1         ; Push interrupt number
    jmp isr_common_stub
%endmacro

%macro ISR_ERRCODE 1
global isr%1
isr%1:
    push %1         ; Push interrupt number
    jmp isr_common_stub
%endmacro

ISR_NOERRCODE 0
ISR_NOERRCODE 1
ISR_NOERRCODE 2

isr_common_stub:
    ; Save all registers
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    
    ; Call C handler
    mov rdi, [rsp + 15*8]  ; Pass interrupt number
    call isr_handler
    
    ; Restore registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    
    add rsp, 16     ; Clean up error code and interrupt number
    iretq
