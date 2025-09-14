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
    ; Save all general-purpose registers that are not saved by the interrupt itself.
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

    ; Check if this interrupt pushes an error code (8, 10-14, 17)
    %if %1 == 8 || (%1 >= 10 && %1 <= 14) || %1 == 17
        ; Error code is already on stack. We need to pass int_no and err_code.
        ; Stack: [registers..., error_code, RIP, CS, RFLAGS, RSP, SS]
        mov rdi, %1          ; Arg1: interrupt number
        mov rsi, [rsp + 15*8] ; Arg2: error code is at rsp + size_of_pushed_registers
    %else
        ; No error code pushed by CPU.
        mov rdi, %1          ; Arg1: interrupt number
        mov rsi, 0           ; Arg2: dummy error code
    %endif

    call isr_handler

    ; Restore all registers
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

    ; For interrupts with an error code, the CPU-pushed value needs to be popped.
    %if %1 == 8 || (%1 >= 10 && %1 <= 14) || %1 == 17
        add rsp, 8
    %endif

    iretq
%endmacro

%assign i 0
%rep 256
  ISR_STUB i
%assign i i+1
%endrep
