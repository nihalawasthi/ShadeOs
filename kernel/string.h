#ifndef STRING_H
#define STRING_H

#include "kernel.h"

// Safe string operations with bounds checking
void* safe_memcpy(void* dest, const void* src, size_t len);
void* safe_memset(void* dest, int val, size_t len);
size_t safe_strlen(const char* str, size_t max_len);
int safe_strcmp(const char* a, const char* b, size_t max_len);
char* safe_strncpy(char* dest, const char* src, size_t dest_size);
int safe_memcmp(const void* s1, const void* s2, size_t n);
int safe_snprintf(char* str, size_t size, const char* format, ...);
int safe_sscanf(const char* str, const char* format, ...);

// Validation functions
int is_valid_pointer(const void* ptr);
int is_valid_string(const char* str, size_t max_len);
int is_valid_buffer(const void* buf, size_t len);

#endif // STRING_H
