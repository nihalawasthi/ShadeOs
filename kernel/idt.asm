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
  ; Check if this interrupt pushes an error code (8, 10-14, 17)
  %if %1 == 8 || (%1 >= 10 && %1 <= 14) || %1 == 17
      ; Interrupts with error code: CPU already pushed error code
      ; Stack: [error_code, RIP, CS, RFLAGS, RSP, SS]
      push %1               ; Push interrupt number
      ; Stack: [int_no, error_code, RIP, CS, RFLAGS, RSP, SS]
      mov rdi, [rsp]        ; rdi = int_no
      mov rsi, [rsp+8]      ; rsi = error_code
      call isr_handler
      add rsp, 16           ; Clean up int_no and error_code
  %else
      ; Interrupts without error code: CPU did not push error code
      ; Stack: [RIP, CS, RFLAGS, RSP, SS]
      push 0                ; Push dummy error code
      push %1               ; Push interrupt number
      ; Stack: [int_no, dummy_error_code, RIP, CS, RFLAGS, RSP, SS]
      mov rdi, [rsp]        ; rdi = int_no
      mov rsi, [rsp+8]      ; rsi = dummy_error_code
      call isr_handler
      add rsp, 16           ; Clean up int_no and dummy_error_code
  %endif
  iretq
%endmacro

%assign i 0
%rep 256
  ISR_STUB i
%assign i i+1
%endrep
