// Remove all code from this file. The serial port is now implemented in Rust.
#include "serial.h"
#include "kernel.h"
#include <stdarg.h>

void kernel_log(const char* format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);
    
    // Simple sprintf implementation for kernel logging
    int len = 0;
    const char* p = format;
    char* buf_ptr = buffer;
    
    while (*p && len < 1023) {
        if (*p == '%' && *(p + 1)) {
            p++; // Skip '%'
            switch (*p) {
                case 'd': {
                    int val = va_arg(args, int);
                    // Simple integer to string conversion
                    if (val == 0) {
                        *buf_ptr++ = '0';
                        len++;
                    } else {
                        char temp[32];
                        int temp_len = 0;
                        int is_negative = 0;
                        
                        if (val < 0) {
                            is_negative = 1;
                            val = -val;
                        }
                        
                        while (val > 0) {
                            temp[temp_len++] = '0' + (val % 10);
                            val /= 10;
                        }
                        
                        if (is_negative) {
                            *buf_ptr++ = '-';
                            len++;
                        }
                        
                        for (int i = temp_len - 1; i >= 0 && len < 1023; i--) {
                            *buf_ptr++ = temp[i];
                            len++;
                        }
                    }
                    break;
                }
                case 's': {
                    const char* str = va_arg(args, const char*);
                    while (*str && len < 1023) {
                        *buf_ptr++ = *str++;
                        len++;
                    }
                    break;
                }
                case 'c': {
                    char c = (char)va_arg(args, int);
                    *buf_ptr++ = c;
                    len++;
                    break;
                }
                default:
                    *buf_ptr++ = *p;
                    len++;
                    break;
            }
        } else {
            *buf_ptr++ = *p;
            len++;
        }
        p++;
    }
    
    *buf_ptr = '\0';
    va_end(args);
    
    // Send to serial port (implemented in Rust)
    extern void serial_write(const char* str);
    serial_write(buffer);
}
