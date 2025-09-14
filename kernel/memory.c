#include "kernel.h"
#include "string.h"
#include <stdarg.h>
#include <stddef.h>
#include "memory.h"
#include "serial.h"

uint8_t* heap_start = (uint8_t*)0x100000; // 1MB
uint8_t* heap_current = (uint8_t*)0x100000;
static const uint8_t* heap_end = (uint8_t*)0x200000; // 2MB limit

// Kernel memory boundaries from linker script
extern uint8_t _kernel_start, _kernel_end;

// Validation functions
int is_valid_pointer(const void* ptr) {
    if (!ptr) return 0;
    uintptr_t addr = (uintptr_t)ptr;
    uintptr_t kernel_start_addr = (uintptr_t)&_kernel_start;
    uintptr_t kernel_end_addr = (uintptr_t)&_kernel_end;

    // Pointer is within the kernel's own loaded memory space (.text, .data, .bss)
    if (addr >= kernel_start_addr && addr < kernel_end_addr) {
        return 1;
    }

    // Pointer is in a region managed by the PMM/heap allocator.
    // This is a broad check, assuming memory above the kernel is valid.
    if (addr >= kernel_end_addr && addr < (512 * 1024 * 1024)) { // Up to 512MB
        return 1;
    }

    // Allow low memory for hardware access (e.g., VGA buffer at 0xB8000)
    if (addr >= 0x1000 && addr < 0x100000) {
        return 1;
    }

    return 0; // Pointer is likely invalid
}

int is_valid_string(const char* str, size_t max_len) {
    if (!is_valid_pointer(str)) return 0;
    
    for (size_t i = 0; i < max_len; i++) {
        if (!is_valid_pointer(str + i)) return 0;
        if (str[i] == '\0') return 1;
    }
    return 0; // No null terminator found within max_len
}

int is_valid_buffer(const void* buf, size_t len) {
    if (!buf || len == 0) return 0;
    if (len > 0x100000) return 0; // Sanity check: no buffer > 1MB
    
    const uint8_t* start = (const uint8_t*)buf;
    const uint8_t* end = start + len - 1;
    
    return is_valid_pointer(start) && is_valid_pointer(end);
}

void memory_init() {
    heap_current = heap_start;
    serial_write("[MEMORY] Memory subsystem initialized safely\n");
}

// Safe memory operations with bounds checking
void* safe_memset(void* dest, int val, size_t len) {
    if (!dest || len == 0) return NULL;
    if (!is_valid_buffer(dest, len)) return NULL;
    if (len > 0x100000) return NULL; // Prevent integer overflow
    
    volatile uint8_t* d = (volatile uint8_t*)dest;
    uint8_t value = (uint8_t)(val & 0xFF);
    
    for (size_t i = 0; i < len; i++) {
        if (!is_valid_pointer(d + i)) break;
        d[i] = value;
    }
    return dest;
}

void* safe_memcpy(void* dest, const void* src, size_t len) {
    if (!dest || !src || len == 0) return NULL;
    if (!is_valid_buffer(dest, len) || !is_valid_buffer(src, len)) return NULL;
    if (len > 0x100000) return NULL; // Prevent integer overflow
    
    // Check for overlap
    uintptr_t dest_start = (uintptr_t)dest;
    uintptr_t dest_end = dest_start + len;
    uintptr_t src_start = (uintptr_t)src;
    uintptr_t src_end = src_start + len;
    
    if ((dest_start < src_end) && (src_start < dest_end)) {
        // Overlapping regions - use memmove logic
        if (dest_start < src_start) {
            // Copy forward
            uint8_t* d = (uint8_t*)dest;
            const uint8_t* s = (const uint8_t*)src;
            for (size_t i = 0; i < len; i++) {
                if (!is_valid_pointer(d + i) || !is_valid_pointer(s + i)) break;
                d[i] = s[i];
            }
        } else {
            // Copy backward
            uint8_t* d = (uint8_t*)dest;
            const uint8_t* s = (const uint8_t*)src;
            for (size_t i = len; i > 0; i--) {
                if (!is_valid_pointer(d + i - 1) || !is_valid_pointer(s + i - 1)) break;
                d[i - 1] = s[i - 1];
            }
        }
    } else {
        // Non-overlapping regions
        uint8_t* d = (uint8_t*)dest;
        const uint8_t* s = (const uint8_t*)src;
        for (size_t i = 0; i < len; i++) {
            if (!is_valid_pointer(d + i) || !is_valid_pointer(s + i)) break;
            d[i] = s[i];
        }
    }
    return dest;
}

size_t safe_strlen(const char* str, size_t max_len) {
    if (!str || max_len == 0) return 0;
    if (!is_valid_pointer(str)) return 0;
    
    size_t len = 0;
    while (len < max_len && is_valid_pointer(str + len) && str[len] != '\0') {
        len++;
    }
    return len;
}

// Port I/O with validation
uint8_t inb(uint16_t port) {
    // Validate port range
    if (port > 0xFFFF) return 0;
    
    uint8_t result;
    __asm__ volatile("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

void outb(uint16_t port, uint8_t data) {
    // Validate port range
    if (port > 0xFFFF) return;
    
    __asm__ volatile("outb %0, %1" : : "a"(data), "Nd"(port));
}

uint16_t inw(uint16_t port) {
    if (port > 0xFFFF) return 0;
    
    uint16_t result;
    __asm__ volatile("inw %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

void outw(uint16_t port, uint16_t data) {
    if (port > 0xFFFF) return;
    
    __asm__ volatile("outw %0, %1" : : "a"(data), "Nd"(port));
}

uint32_t inl(uint16_t port) {
    if (port > 0xFFFF) return 0;
    
    uint32_t result;
    __asm__ volatile("inl %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

void outl(uint16_t port, uint32_t data) {
    if (port > 0xFFFF) return;
    
    __asm__ volatile("outl %0, %1" : : "a"(data), "Nd"(port));
}

int safe_strcmp(const char* a, const char* b, size_t max_len) {
    if (!a || !b) return (a == b) ? 0 : (a ? 1 : -1);
    if (!is_valid_pointer(a) || !is_valid_pointer(b)) return 0;
    
    for (size_t i = 0; i < max_len; i++) {
        if (!is_valid_pointer(a + i) || !is_valid_pointer(b + i)) break;
        
        if (a[i] != b[i]) {
            return (unsigned char)a[i] - (unsigned char)b[i];
        }
        if (a[i] == '\0') break;
    }
    return 0;
}

char* safe_strncpy(char* dest, const char* src, size_t dest_size) {
    if (!dest || !src || dest_size == 0) return NULL;
    if (!is_valid_buffer(dest, dest_size) || !is_valid_pointer(src)) return NULL;
    
    size_t i;
    for (i = 0; i < dest_size - 1; i++) {
        if (!is_valid_pointer(src + i)) break;
        dest[i] = src[i];
        if (src[i] == '\0') break;
    }
    dest[i] = '\0'; // Always null terminate
    return dest;
}

int safe_memcmp(const void* s1, const void* s2, size_t n) {
    if (!s1 || !s2 || n == 0) return 0;
    if (!is_valid_buffer(s1, n) || !is_valid_buffer(s2, n)) return 0;
    
    const unsigned char* a = (const unsigned char*)s1;
    const unsigned char* b = (const unsigned char*)s2;
    
    for (size_t i = 0; i < n; i++) {
        if (!is_valid_pointer(a + i) || !is_valid_pointer(b + i)) break;
        if (a[i] != b[i]) return a[i] - b[i];
    }
    return 0;
}

// Safe integer to string conversion
static int safe_int_to_str(int val, char* buf, size_t buf_size) {
    if (!buf || buf_size < 2) return 0;
    
    int is_negative = 0;
    if (val < 0) {
        is_negative = 1;
        val = -val;
    }
    
    char temp[16];
    int pos = 0;
    
    if (val == 0) {
        temp[pos++] = '0';
    } else {
        while (val > 0 && pos < 15) {
            temp[pos++] = '0' + (val % 10);
            val /= 10;
        }
    }
    
    int write_pos = 0;
    if (is_negative && write_pos < (int)buf_size - 1) {
        buf[write_pos++] = '-';
    }
    
    for (int i = pos - 1; i >= 0 && write_pos < (int)buf_size - 1; i--) {
        buf[write_pos++] = temp[i];
    }
    
    buf[write_pos] = '\0';
    return write_pos;
}

// Safe snprintf implementation
int safe_snprintf(char* str, size_t size, const char* format, ...) {
    if (!str || !format || size == 0) return -1;
    if (!is_valid_buffer(str, size) || !is_valid_string(format, 1024)) return -1;
    
    va_list args;
    va_start(args, format);
    
    size_t written = 0;
    const char* p = format;
    
    while (*p && written < size - 1) {
        if (!is_valid_pointer(p)) break;
        
        if (*p == '%' && *(p + 1)) {
            p++;
            switch (*p) {
                case 's': {
                    const char* s = va_arg(args, const char*);
                    if (s && is_valid_string(s, 1024)) {
                        while (*s && written < size - 1) {
                            str[written++] = *s++;
                        }
                    }
                    break;
                }
                case 'd': {
                    int val = va_arg(args, int);
                    char num_buf[16];
                    int len = safe_int_to_str(val, num_buf, sizeof(num_buf));
                    for (int i = 0; i < len && written < size - 1; i++) {
                        str[written++] = num_buf[i];
                    }
                    break;
                }
                case '%':
                    str[written++] = '%';
                    break;
                default:
                    str[written++] = '%';
                    if (written < size - 1) str[written++] = *p;
                    break;
            }
        } else {
            str[written++] = *p;
        }
        p++;
    }
    
    str[written] = '\0';
    va_end(args);
    return (int)written;
}

// Safe string to integer conversion
static int safe_str_to_int(const char** p, int* out) {
    if (!p || !*p || !out) return 0;
    if (!is_valid_pointer(*p)) return 0;
    
    const char* s = *p;
    int val = 0;
    int digits = 0;
    
    while (*s >= '0' && *s <= '9' && digits < 10) { // Prevent overflow
        if (!is_valid_pointer(s)) break;
        val = val * 10 + (*s - '0');
        s++;
        digits++;
    }
    
    if (digits == 0) return 0;
    
    *out = val;
    *p = s;
    return 1;
}

// Safe IPv4 parsing
static int safe_sscanf_ipv4(const char* str, int* a, int* b, int* c, int* d) {
    if (!str || !a || !b || !c || !d) return 0;
    if (!is_valid_string(str, 256)) return 0;
    
    const char* p = str;
    
    if (!safe_str_to_int(&p, a) || *p != '.') return 0;
    p++;
    if (!safe_str_to_int(&p, b) || *p != '.') return 0;
    p++;
    if (!safe_str_to_int(&p, c) || *p != '.') return 0;
    p++;
    if (!safe_str_to_int(&p, d)) return 0;
    
    // Validate IP components
    if (*a < 0 || *a > 255 || *b < 0 || *b > 255 || 
        *c < 0 || *c > 255 || *d < 0 || *d > 255) {
        return 0;
    }
    
    return 4;
}

// Safe sscanf implementation
int safe_sscanf(const char* str, const char* format, ...) {
    if (!str || !format) return 0;
    if (!is_valid_string(str, 1024) || !is_valid_string(format, 256)) return 0;
    
    va_list args;
    va_start(args, format);
    
    int result = 0;
    
    if (safe_strcmp(format, "%d.%d.%d.%d", 256) == 0) {
        int* a = va_arg(args, int*);
        int* b = va_arg(args, int*);
        int* c = va_arg(args, int*);
        int* d = va_arg(args, int*);
        
        if (a && b && c && d && 
            is_valid_pointer(a) && is_valid_pointer(b) && 
            is_valid_pointer(c) && is_valid_pointer(d)) {
            result = safe_sscanf_ipv4(str, a, b, c, d);
        }
    }
    
    va_end(args);
    return result;
}

// Legacy wrapper functions for compatibility
void* memcpy(void* dest, const void* src, size_t len) {
    return safe_memcpy(dest, src, len);
}

void* memset(void* dest, int val, size_t len) {
    return safe_memset(dest, val, len);
}

size_t strlen(const char* str) {
    return safe_strlen(str, 4096); // Reasonable default limit
}

int strcmp(const char* a, const char* b) {
    return safe_strcmp(a, b, 4096); // Reasonable default limit
}

char* strncpy(char* dest, const char* src, size_t n) {
    return safe_strncpy(dest, src, n);
}

int memcmp(const void* s1, const void* s2, size_t n) {
    return safe_memcmp(s1, s2, n);
}

int snprintf(char* str, size_t size, const char* format, ...) {
    if (!str || !format || size == 0) return -1;
    
    va_list args;
    va_start(args, format);
    
    // Use safe_snprintf logic but with va_list
    size_t written = 0;
    const char* p = format;
    
    while (*p && written < size - 1) {
        if (*p == '%' && *(p + 1)) {
            p++;
            switch (*p) {
                case 's': {
                    const char* s = va_arg(args, const char*);
                    if (s && is_valid_string(s, 1024)) {
                        while (*s && written < size - 1) {
                            str[written++] = *s++;
                        }
                    }
                    break;
                }
                case 'd': {
                    int val = va_arg(args, int);
                    char num_buf[16];
                    int len = safe_int_to_str(val, num_buf, sizeof(num_buf));
                    for (int i = 0; i < len && written < size - 1; i++) {
                        str[written++] = num_buf[i];
                    }
                    break;
                }
                default:
                    str[written++] = '%';
                    if (written < size - 1) str[written++] = *p;
                    break;
            }
        } else {
            str[written++] = *p;
        }
        p++;
    }
    
    str[written] = '\0';
    va_end(args);
    return (int)written;
}

int sscanf(const char* str, const char* format, ...) {
    if (!str || !format) return 0;
    
    va_list args;
    va_start(args, format);
    
    int result = 0;
    if (safe_strcmp(format, "%d.%d.%d.%d", 256) == 0) {
        int* a = va_arg(args, int*);
        int* b = va_arg(args, int*);
        int* c = va_arg(args, int*);
        int* d = va_arg(args, int*);
        result = safe_sscanf_ipv4(str, a, b, c, d);
    }
    
    va_end(args);
    return result;
}
