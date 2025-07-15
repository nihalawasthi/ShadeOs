#include "heap.h"
#include "paging.h"
#include "pmm.h"
#include "kernel.h"
#include "vga.h"
#include "serial.h"

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
  char addr_str[32];
  addr_str[0] = '0';
  addr_str[1] = 'x';
  for (int i = 0; i < 16; i++) {
      uint8_t digit = (addr >> (60 - i * 4)) & 0xF;
      addr_str[2 + i] = (digit < 10) ? '0' + digit : 'A' + digit - 10;
  }
  addr_str[18] = '\0';
  
  void* phys = alloc_page();
  if (!phys) {
      serial_write("[HEAP] ERROR: Failed to allocate physical page\n");
      vga_print("[HEAP] ERROR: Failed to allocate physical page\n");
      return;
  }
  
  // Print physical address in hex
  char phys_str[32];
  phys_str[0] = '0';
  phys_str[1] = 'x';
  for (int i = 0; i < 16; i++) {
      uint8_t digit = ((uint64_t)phys >> (60 - i * 4)) & 0xF;
      phys_str[2 + i] = (digit < 10) ? ('0' + digit) : ('A' + digit - 10);
  }
  phys_str[18] = '\0';
  
  map_page(addr, (uint64_t)phys, PAGE_PRESENT | PAGE_RW);
}

void heap_init() {
  serial_write("[HEAP] Starting heap initialization\n");
  vga_print("[HEAP] Starting heap initialization\n");
  
  serial_write("[DEBUG] sizeof(heap_block_t): ");
  char size_str[16];
  snprintf(size_str, sizeof(size_str), "%d", sizeof(heap_block_t));
  serial_write(size_str);
  serial_write("\n");
  
  // Map initial heap pages first
  heap_end = HEAP_START + HEAP_INITIAL_SIZE;
  for (uint64_t addr = HEAP_START; addr < heap_end; addr += PAGE_SIZE) {
      map_heap_page(addr);
  }
  // Now set up the heap block structure
  heap_head = (heap_block_t*)HEAP_START;
  serial_write("[HEAP] Heap head set to 0xFFFF800000000000\n");
  
  heap_head->size = HEAP_INITIAL_SIZE - sizeof(heap_block_t);
  serial_write("[HEAP] Initial heap size: ");
  // Print size in decimal
  uint64_t size = heap_head->size;
  char size_str2[32];
  int i = 0;
  do {
      size_str2[i++] = '0' + (size % 10);
      size /= 10;
  } while (size > 0);
  size_str2[i] = '\0';
  // Reverse the string
  for (int j = 0; j < i/2; j++) {
      char t = size_str2[j];
      size_str2[j] = size_str2[i-1-j];
      size_str2[i-1-j] = t;
  }
  serial_write(size_str2);
  serial_write(" bytes\n");
  
  heap_head->free = 1;
  heap_head->next = 0;
  
  serial_write("[HEAP] Heap initialization complete\n");
  vga_print("[HEAP] Heap initialization complete\n");
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
    void* ptr = rust_kmalloc(size);
    if (ptr) return ptr;
    // fallback to C heap if Rust fails
  if (size == 0) return NULL;
  serial_write("[HEAP] kmalloc request: ");
  char size_hex[17];
  for (int i = 0; i < 16; i++) {
      int nibble = (size >> ((15 - i) * 4)) & 0xF;
      size_hex[i] = (nibble < 10) ? ('0' + nibble) : ('A' + nibble - 10);
  }
  size_hex[16] = 0;
  serial_write(size_hex);
  serial_write("\n");
  heap_block_t* block = heap_head;
  while (block) {
      if (block->free && block->size >= size) {
          split_block(block, size);
          block->free = 0;
          void* ret = (void*)((uint8_t*)block + sizeof(heap_block_t));
          serial_write("[HEAP] kmalloc returns: 0x");
          char addr_hex[17];
          uint64_t addr = (uint64_t)ret;
          for (int i = 0; i < 16; i++) {
              int nibble = (addr >> ((15 - i) * 4)) & 0xF;
              addr_hex[i] = (nibble < 10) ? ('0' + nibble) : ('A' + nibble - 10);
          }
          addr_hex[16] = 0;
          serial_write(addr_hex);
          serial_write("\n");
          return ret;
      }
      if (!block->next) break;
      block = block->next;
  }
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
  void* ret = (void*)((uint8_t*)new_block + sizeof(heap_block_t));
  serial_write("[HEAP] kmalloc returns: 0x");
  char addr_hex2[17];
  uint64_t addr2 = (uint64_t)ret;
  for (int i = 0; i < 16; i++) {
      int nibble = (addr2 >> ((15 - i) * 4)) & 0xF;
      addr_hex2[i] = (nibble < 10) ? ('0' + nibble) : ('A' + nibble - 10);
  }
  addr_hex2[16] = 0;
  serial_write(addr_hex2);
  serial_write("\n");
  return ret;
}

void kfree(void* ptr) {
    rust_kfree(ptr);
    // Optionally, also free in C heap if needed
  if (!ptr) return;
  heap_block_t* block = (heap_block_t*)((uint8_t*)ptr - sizeof(heap_block_t));
  block->free = 1;
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
