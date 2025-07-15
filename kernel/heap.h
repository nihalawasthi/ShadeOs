#ifndef HEAP_H
#define HEAP_H

#include "kernel.h"

void heap_init();
void* kmalloc(size_t size);
void kfree(void* ptr);
void* rust_kmalloc(size_t size);
void rust_kfree(void* ptr);

#endif 