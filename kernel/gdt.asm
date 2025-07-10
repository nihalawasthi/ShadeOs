; kernel/gdt.asm - GDT assembly functions
BITS 64

global gdt_flush

gdt_flush:
    lgdt [rdi]
    mov ax, 0x10      ; Data segment selector
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    
    ; Far jump to reload CS
    push 0x08         ; Code segment selector
    lea rax, [rel .reload_cs]
    push rax
    retfq
.reload_cs:
    ret
