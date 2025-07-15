#include "vfs.h"
#include <stddef.h>
#include "serial.h"
#include "vga.h"

extern int rust_vfs_init();
extern int rust_vfs_mkdir(const char* path);
extern int rust_vfs_ls(const char* path);
extern int rust_vfs_read(const char* path, void* buf, int max_len);
extern int rust_vfs_write(const char* path, const void* buf, int len);
extern void* rust_vfs_get_root();

static int rust_vfs_ready = 0;

static vfs_node_t fallback_root = {
    .used = 1,
    .node_type = VFS_TYPE_DIR,
    .name = "/",
    .size = 0,
    .parent = NULL,
    .child = NULL,
    .sibling = NULL
};

// Add a simple hex print for pointer values
static void print_hex_ptr_vfs(const char* label, void* ptr) {
    vga_print(label);
    serial_write(label);
    vga_print(": 0x");
    serial_write(": 0x");
    unsigned long val = (unsigned long)ptr;
    for (int i = (sizeof(unsigned long) * 2) - 1; i >= 0; i--) {
        unsigned char d = (val >> (i * 4)) & 0xF;
        char c = (d < 10) ? ('0' + d) : ('A' + (d - 10));
        vga_putchar(c);
        serial_write((char[]){c, 0});
    }
    vga_print("\n");
    serial_write("\n");
}

void vfs_init() {
    vga_print("[VFS] Calling rust_vfs_init...\n");
    serial_write("[VFS] Calling rust_vfs_init...\n");
    int result = rust_vfs_init();
    vga_print("[VFS] rust_vfs_init returned: ");
    serial_write("[VFS] rust_vfs_init returned: ");
    char result_str[4];
    result_str[0] = '0' + (result / 100) % 10;
    result_str[1] = '0' + (result / 10) % 10;
    result_str[2] = '0' + result % 10;
    result_str[3] = 0;
    vga_print(result_str);
    serial_write(result_str);
    vga_print("\n");
    serial_write("\n");
    if (result == 0) {
        rust_vfs_ready = 1;
        vga_print("[VFS] Rust VFS initialized successfully\n");
        serial_write("[VFS] Rust VFS initialized successfully\n");
    } else {
        rust_vfs_ready = 0;
        vga_print("[VFS] Rust VFS initialization failed\n");
        serial_write("[VFS] Rust VFS initialization failed\n");
    }
}

vfs_node_t* vfs_get_root() {
    void* result = NULL;
    if (rust_vfs_ready) {
        void* rust_root = rust_vfs_get_root();
        if (rust_root) result = rust_root;
    }
    if (!result) result = &fallback_root;
    print_hex_ptr_vfs("[DEBUG] vfs_get_root returns", result);
    return (vfs_node_t*)result;
}

vfs_node_t* vfs_find(const char* path, vfs_node_t* cwd) {
    if (!rust_vfs_ready) {
        vga_print("[VFS] Rust VFS not ready, cannot find ");
        vga_print(path);
        vga_print("\n");
        return NULL;
    }
    // TODO: Call into Rust to find node and return pointer
    return NULL;
}

vfs_node_t* vfs_create(const char* name, int type, vfs_node_t* parent) {
    if (!rust_vfs_ready) {
        vga_print("[VFS] Rust VFS not ready, cannot create ");
        vga_print(name);
        vga_print("\n");
        return NULL;
    }
    // TODO: Call into Rust to create node and return pointer
    return NULL;
}

int vfs_write(vfs_node_t* node, const void* buf, int size) {
    if (!rust_vfs_ready) {
        vga_print("[VFS] Rust VFS not ready, cannot write\n");
        return -1;
    }
    // TODO: Call into Rust to write
    return -1;
}

int vfs_read(vfs_node_t* node, void* buf, int size) {
    if (!rust_vfs_ready) {
        vga_print("[VFS] Rust VFS not ready, cannot read\n");
        return -1;
    }
    // TODO: Call into Rust to read
    return -1;
}

void vfs_list(vfs_node_t* dir) {
    if (!rust_vfs_ready) {
        vga_print("[VFS] Rust VFS not ready, cannot list\n");
        return;
    }
    // TODO: Call into Rust to list directory
} 