#include "helpers.h"

// These are the actual function definitions that can be called from Rust
void sys_sti(void) { 
    __asm__ volatile ("sti"); 
}

void sys_cli(void) { 
    __asm__ volatile ("cli"); 
}

void pause(void) { 
    __asm__ volatile ("hlt"); 
}