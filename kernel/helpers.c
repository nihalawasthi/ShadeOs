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

char* strstr(const char* haystack, const char* needle) {
    if (!*needle) return (char*)haystack;
    for (; *haystack; haystack++) {
        const char *h = haystack, *n = needle;
        while (*h && *n && *h == *n) { h++; n++; }
        if (!*n) return (char*)haystack;
    }
    return 0;
}
