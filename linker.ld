/* linker.ld - Fixed linker script with proper multiboot placement */
ENTRY(_start)

SECTIONS
{
    . = 1M;
    
    /* Multiboot header MUST be in first 32KB */
    .multiboot ALIGN(8) : {
        *(.multiboot)
    }

    /* Text section with entry point */
    .text ALIGN(4K) : {
        PROVIDE(_kernel_start = .);
        *(.text)
    }

    /* Read-only data */
    .rodata ALIGN(4K) : {
        *(.rodata)
    }

    /* Initialized data */
    .data ALIGN(4K) : {
        *(.data)
    }

    /* Uninitialized data */
    .bss ALIGN(4K) : {
        *(COMMON)
        *(.bss)
    }
    
    . = ALIGN(4K);
    PROVIDE(_kernel_end = .);
}
