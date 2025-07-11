#ifndef STRING_H
#define STRING_H

#include "kernel.h"

void* memcpy(void* dest, const void* src, size_t len);
void* memset(void* dest, int val, size_t len);
size_t strlen(const char* str);
int strcmp(const char* a, const char* b);
int memcmp(const void* s1, const void* s2, size_t n);
int snprintf(char* str, size_t size, const char* format, ...);
int sscanf(const char* str, const char* format, ...);

#endif // STRING_H 