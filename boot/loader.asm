; boot/loader.asm - Complete multiboot2 bootloader with proper 32->64 bit transition
BITS 32

; Multiboot2 header
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

section .text
global _start
extern kernel_main

_start:
    cli
    
    ; Set up stack
    mov esp, stack_top
    
    ; Save multiboot2 info
    push ebx    ; multiboot info pointer
    push eax    ; multiboot2 magic (should be 0x36d76289)
    
    ; Verify we were loaded by a multiboot2 bootloader
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
    ; Clear the page tables
    mov edi, p4_table
    mov cr3, edi
    xor eax, eax
    mov ecx, 4096
    rep stosd
    mov edi, p4_table

    ; Set up page tables
    mov eax, p3_table
    or eax, 0b11
    mov [p4_table], eax

    mov eax, p2_table
    or eax, 0b11
    mov [p3_table], eax

    ; Map first 1GB as 2MB pages
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
    ; Load page table
    mov eax, p4_table
    mov cr3, eax

    ; Enable PAE
    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax

    ; Enable long mode
    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    wrmsr

    ; Enable paging
    mov eax, cr0
    or eax, 1 << 31
    mov cr0, eax
    ret

; 32-bit string printing
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

    ; Clear screen
    mov rdi, 0xB8000
    mov rax, 0x0F200F200F200F20
    mov rcx, 500
    rep stosq

    ; Print success message
    mov rsi, success_msg
    mov rdi, 0xB8000
    call print_string_64

    ; Pass Multiboot2 info pointer to kernel_main in rdi
    mov rdi, [esp]    ; esp still points to the last value pushed in 32-bit mode (multiboot info pointer)
    call kernel_main

    ; Halt if kernel returns
.hang:
    hlt
    jmp .hang

; 64-bit string printing
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
p4_table:
    resb 4096
p3_table:
    resb 4096
p2_table:
    resb 4096

align 16
stack_bottom:
    resb 16384
stack_top:
