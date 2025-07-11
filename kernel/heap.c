#include "heap.h"
#include "paging.h"
#include "pmm.h"
#include "kernel.h"

#define HEAP_START 0xFFFF800000000000ULL
#define HEAP_INITIAL_SIZE (PAGE_SIZE * 16)
#define HEAP_MAX_SIZE (PAGE_SIZE * 4096) // 16 MiB

typedef struct heap_block {
    size_t size;
    int free;
    struct heap_block* next;
} heap_block_t;

static heap_block_t* heap_head = 0;
static uint64_t heap_end = HEAP_START;

static void map_heap_page(uint64_t addr) {
    void* phys = alloc_page();
    map_page(addr, (uint64_t)phys, PAGE_PRESENT | PAGE_RW);
}

void heap_init() {
    heap_head = (heap_block_t*)HEAP_START;
    heap_head->size = HEAP_INITIAL_SIZE - sizeof(heap_block_t);
    heap_head->free = 1;
    heap_head->next = 0;
    heap_end = HEAP_START + HEAP_INITIAL_SIZE;
    // Map initial heap pages
    for (uint64_t addr = HEAP_START; addr < heap_end; addr += PAGE_SIZE) {
        map_heap_page(addr);
    }
}

static void split_block(heap_block_t* block, size_t size) {
    if (block->size >= size + sizeof(heap_block_t) + 16) {
        heap_block_t* new_block = (heap_block_t*)((uint8_t*)block + sizeof(heap_block_t) + size);
        new_block->size = block->size - size - sizeof(heap_block_t);
        new_block->free = 1;
        new_block->next = block->next;
        block->size = size;
        block->next = new_block;
    }
}

void* kmalloc(size_t size) {
    if (size == 0) return NULL;
    heap_block_t* block = heap_head;
    while (block) {
        if (block->free && block->size >= size) {
            split_block(block, size);
            block->free = 0;
            return (void*)((uint8_t*)block + sizeof(heap_block_t));
        }
        if (!block->next) break;
        block = block->next;
    }
    // Need to grow heap
    uint64_t old_end = heap_end;
    uint64_t new_end = old_end + ((size + sizeof(heap_block_t) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1));
    if (new_end - HEAP_START > HEAP_MAX_SIZE) return NULL;
    for (uint64_t addr = old_end; addr < new_end; addr += PAGE_SIZE) {
        map_heap_page(addr);
    }
    heap_block_t* new_block = (heap_block_t*)old_end;
    new_block->size = new_end - old_end - sizeof(heap_block_t);
    new_block->free = 0;
    new_block->next = 0;
    block->next = new_block;
    heap_end = new_end;
    split_block(new_block, size);
    return (void*)((uint8_t*)new_block + sizeof(heap_block_t));
}

void kfree(void* ptr) {
    if (!ptr) return;
    heap_block_t* block = (heap_block_t*)((uint8_t*)ptr - sizeof(heap_block_t));
    block->free = 1;
    // Merge adjacent free blocks
    heap_block_t* cur = heap_head;
    while (cur && cur->next) {
        if (cur->free && cur->next->free) {
            cur->size += sizeof(heap_block_t) + cur->next->size;
            cur->next = cur->next->next;
        } else {
            cur = cur->next;
        }
    }
} 