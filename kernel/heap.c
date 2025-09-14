// Remove all C heap logic except FFI stubs
#include "heap.h"
#include "kernel.h"

// FFI stubs for Rust heap (if needed)
void* rust_kmalloc(size_t size);
void rust_kfree(void* ptr);

void* kmalloc(size_t size) {
    return rust_kmalloc(size);
}

void kfree(void* ptr) {
    rust_kfree(ptr);
}

// All other C heap code removed.
