#ifndef HEAP_H
#define HEAP_H

#include "kernel.h"

void* rust_kmalloc(size_t size);
void rust_kfree(void* ptr);

#endif 