#include "kernel.h"
#include "string.h"
#include <stdarg.h>

static uint8_t* heap_start = (uint8_t*)0x100000; // 1MB
static uint8_t* heap_current = (uint8_t*)0x100000;
static const size_t HEAP_SIZE = 0x100000; // 1MB heap

static int sscanf_ipv4(const char* str, int* a, int* b, int* c, int* d);
static int sscanf_num(const char** p, int* out);

void memory_init() {
    // Initialize basic heap
    heap_current = heap_start;
}

// Robust memset for kernel: no inlining, no alignment assumptions
__attribute__((noinline)) void* memset(void* dest, int val, size_t len) {
    void* r = rust_memset(dest, val, len);
    if (r) return r;
    volatile uint8_t* d = (volatile uint8_t*)dest;
    for (size_t i = 0; i < len; i++) {
        d[i] = (uint8_t)val;
    }
    return dest;
}

void* memcpy(void* dest, const void* src, size_t len) {
    void* r = rust_memcpy(dest, src, len);
    if (r) return r;
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

uint32_t inl(uint16_t port) {
    uint32_t result;
    __asm__ volatile("inl %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

void outl(uint16_t port, uint32_t data) {
    __asm__ volatile("outl %0, %1" : : "a"(data), "Nd"(port));
}

int strcmp(const char* a, const char* b) {
    while (*a && (*a == *b)) { a++; b++; }
    return *(const unsigned char*)a - *(const unsigned char*)b;
}

char* strncpy(char* dest, const char* src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    for (; i < n; i++) {
        dest[i] = '\0';
    }
    return dest;
}

int memcmp(const void* s1, const void* s2, size_t n) {
    const unsigned char* a = (const unsigned char*)s1;
    const unsigned char* b = (const unsigned char*)s2;
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return a[i] - b[i];
    }
    return 0;
}

// Minimal snprintf: supports %s and %d only
int snprintf(char* str, size_t size, const char* format, ...) {
    va_list args;
    va_start(args, format);
    size_t i = 0;
    const char* p = format;
    while (*p && i + 1 < size) {
        if (*p == '%') {
            p++;
            if (*p == 's') {
                const char* s = va_arg(args, const char*);
                while (*s && i + 1 < size) str[i++] = *s++;
            } else if (*p == 'd') {
                int val = va_arg(args, int);
                char buf[16];
                int n = 0;
                if (val < 0) { str[i++] = '-'; val = -val; }
                do { buf[n++] = '0' + (val % 10); val /= 10; } while (val && n < 15);
                for (int j = n - 1; j >= 0 && i + 1 < size; j--) str[i++] = buf[j];
            } else {
                str[i++] = '%';
                if (i + 1 < size) str[i++] = *p;
            }
        } else {
            str[i++] = *p;
        }
        p++;
    }
    str[i] = '\0';
    va_end(args);
    return i;
}

// Minimal sscanf: supports "%d.%d.%d.%d" for IPv4 only
int sscanf(const char* str, const char* format, ...) {
    va_list args;
    va_start(args, format);
    int* a = va_arg(args, int*);
    int* b = va_arg(args, int*);
    int* c = va_arg(args, int*);
    int* d = va_arg(args, int*);
    int n = 0;
    if (format && strcmp(format, "%d.%d.%d.%d") == 0) {
        n = (sscanf_ipv4(str, a, b, c, d));
    }
    va_end(args);
    return n;
}

// Helper for IPv4 parsing
int sscanf_ipv4(const char* str, int* a, int* b, int* c, int* d) {
    int n = 0;
    if (str && a && b && c && d) {
        n = 4;
        const char* p = str;
        *a = 0; *b = 0; *c = 0; *d = 0;
        if (sscanf_num(&p, a) && *p == '.') p++;
        else return 0;
        if (sscanf_num(&p, b) && *p == '.') p++;
        else return 0;
        if (sscanf_num(&p, c) && *p == '.') p++;
        else return 0;
        if (sscanf_num(&p, d) && (*p == '\0' || *p == '\n')) return 4;
        else return 0;
    }
    return n;
}

// Helper for number parsing
int sscanf_num(const char** p, int* out) {
    int val = 0;
    const char* s = *p;
    if (*s < '0' || *s > '9') return 0;
    while (*s >= '0' && *s <= '9') {
        val = val * 10 + (*s - '0');
        s++;
    }
    *out = val;
    *p = s;
    return 1;
}
