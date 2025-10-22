; boot/loader.asm - Bulletproof Multiboot2 loader with correct info pointer passing
BITS 32

section .multiboot
align 8
multiboot_start:
    dd 0xE85250D6                ; magic number
    dd 0                         ; architecture (i386)
    dd multiboot_end - multiboot_start ; header length
    dd 0x100000000 - (0xE85250D6 + 0 + (multiboot_end - multiboot_start)) ; checksum

    ; End tag
    align 8
    dw 0        ; type = end tag
    dw 0        ; flags
    dd 8        ; size
multiboot_end:

section .bss
align 16
stack_bottom: resb 16384
stack_top:
mb2_info_ptr: resd 1

section .text
global _start
extern kernel_main

_start:
    cli
    mov esp, stack_top
    
    ; Save Multiboot2 info pointer (ebx) to a known location
    mov [mb2_info_ptr], ebx
    
    ; Check Multiboot2 magic
    cmp eax, 0x36d76289
    jne error
    
    ; Print loading message to screen
    mov esi, loading_msg
    mov edi, 0xB8000
    call print_string_32
    
    ; Check for required CPU features
    call check_cpuid
    call check_long_mode
    
    ; Set up paging for long mode
    call setup_page_tables
    call enable_paging
    
    ; Load 64-bit GDT
    lgdt [gdt64.pointer]
    
    ; Jump to 64-bit code
    jmp gdt64.code_segment:long_mode_start

error:
    mov esi, error_msg
    mov edi, 0xB8000
    call print_string_32
    jmp halt

check_cpuid:
    pushfd
    pop eax
    mov ecx, eax
    xor eax, 1 << 21
    push eax
    popfd
    pushfd
    pop eax
    push ecx
    popfd
    cmp eax, ecx
    je error
    ret

check_long_mode:
    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb error
    mov eax, 0x80000001
    cpuid
    test edx, 1 << 29
    jz error
    ret

setup_page_tables:
    mov edi, p4_table
    mov cr3, edi
    xor eax, eax
    mov ecx, 4096
    rep stosd
    mov edi, p4_table
    mov eax, p3_table
    or eax, 0b11
    mov [p4_table], eax
    mov eax, p2_table
    or eax, 0b11
    mov [p3_table], eax
    mov ecx, 0
.map_p2_table:
    mov eax, 0x200000
    mul ecx
    or eax, 0b10000011
    mov [p2_table + ecx * 8], eax
    inc ecx
    cmp ecx, 512
    jne .map_p2_table
    

    ret

enable_paging:
    mov eax, p4_table
    mov cr3, eax
    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax
    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    wrmsr
    mov eax, cr0
    or eax, 1 << 31
    mov cr0, eax
    ret

print_string_32:
    mov ah, 0x0F
.loop:
    lodsb
    test al, al
    jz .done
    stosw
    jmp .loop
.done:
    ret

halt:
    hlt
    jmp halt

BITS 64
long_mode_start:
    ; Set up segments
    mov ax, gdt64.data_segment
    mov ss, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; Set up stack
    mov rsp, stack_top

    ; Enable FPU/SSE to avoid #UD when compiler emits SSE2 instructions
    ; CR0: clear EM (bit 2), set MP (bit 1), clear TS (bit 3)
    mov rax, cr0
    and rax, ~(1 << 2)        ; CR0.EM = 0 (enable FPU instructions)
    or  rax, (1 << 1)         ; CR0.MP = 1 (monitor coprocessor)
    and rax, ~(1 << 3)        ; CR0.TS = 0 (clear task-switched)
    mov cr0, rax

    ; CR4: set OSFXSR (bit 9) and OSXMMEXCPT (bit 10)
    mov rax, cr4
    or  rax, (1 << 9) | (1 << 10)
    mov cr4, rax

    ; Initialize FPU state
    fninit

    ; Clear screen
    mov rdi, 0xB8000
    mov rax, 0x0F200F200F200F20
    mov rcx, 500
    rep stosq

    ; Print success message
    mov rsi, success_msg
    mov rdi, 0xB8000
    call print_string_64

    ; Load Multiboot2 info pointer from known location
    ; The Multiboot2 info structure is at a 32-bit address
    ; We need to zero-extend it to 64-bit for proper access
    mov rsi, mb2_info_ptr
    mov edi, [rsi]    ; Load the 32-bit value into rdi (zero-extends to 64-bit)
    call kernel_main

.hang:
    hlt
    jmp .hang

print_string_64:
    mov ah, 0x0A  ; light green
.loop:
    lodsb
    test al, al
    jz .done
    stosw
    jmp .loop
.done:
    ret

section .rodata
loading_msg: db 'Loading ShadeOS...', 0
success_msg: db '64-bit mode active...', 0
error_msg: db 'Boot failed!', 0

gdt64:
    dq 0                                     ; null descriptor
.code_segment: equ $ - gdt64
    dq (1<<43) | (1<<44) | (1<<47) | (1<<53) ; code descriptor
.data_segment: equ $ - gdt64
    dq (1<<44) | (1<<47) | (1<<41)           ; data descriptor
.pointer:
    dw $ - gdt64 - 1
    dq gdt64

section .bss
align 4096
p4_table: resb 4096
p3_table: resb 4096
p2_table: resb 4096
