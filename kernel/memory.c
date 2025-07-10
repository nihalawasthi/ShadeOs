// kernel/memory.c - Basic memory management
#include "kernel.h"

static uint8_t* heap_start = (uint8_t*)0x100000; // 1MB
static uint8_t* heap_current = (uint8_t*)0x100000;
static const size_t HEAP_SIZE = 0x100000; // 1MB heap

void memory_init() {
    // Initialize basic heap
    heap_current = heap_start;
}

void* kmalloc(size_t size) {
    if (heap_current + size > heap_start + HEAP_SIZE) {
        return NULL; // Out of memory
    }
    
    void* ptr = heap_current;
    heap_current += size;
    return ptr;
}

void kfree(void* ptr) {
    // Simple allocator - no actual freeing for now
    // TODO: Implement proper memory management
    (void)ptr;
}

void* memset(void* dest, int val, size_t len) {
    uint8_t* d = (uint8_t*)dest;
    for (size_t i = 0; i < len; i++) {
        d[i] = (uint8_t)val;
    }
    return dest;
}

void* memcpy(void* dest, const void* src, size_t len) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    for (size_t i = 0; i < len; i++) {
        d[i] = s[i];
    }
    return dest;
}

size_t strlen(const char* str) {
    size_t len = 0;
    while (str[len]) len++;
    return len;
}

uint8_t inb(uint16_t port) {
    uint8_t result;
    __asm__ volatile("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

void outb(uint16_t port, uint8_t data) {
    __asm__ volatile("outb %0, %1" : : "a"(data), "Nd"(port));
}

uint16_t inw(uint16_t port) {
    uint16_t result;
    __asm__ volatile("inw %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

void outw(uint16_t port, uint16_t data) {
    __asm__ volatile("outw %0, %1" : : "a"(data), "Nd"(port));
}
