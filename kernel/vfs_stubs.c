#include "vfs.h"
#include <stddef.h>
#include "serial.h"
#include "vga.h"
#include <stdint.h>
#include "memory.h"
#include "string.h"
#include <string.h>
char *strstr(const char *haystack, const char *needle);

extern int rust_vfs_init();
extern int rust_vfs_mkdir(const char* path);
extern int rust_vfs_ls(const char* path);
extern int rust_vfs_read(const char* path, void* buf, int max_len);
extern uint64_t rust_vfs_write(const char* path, const void* buf, uint64_t len);
extern void* rust_vfs_get_root();
extern int rust_elf_load(const char* path);

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

static int is_valid_path(const char* path) {
    if (!path) return 0;
    if (!is_valid_string(path, 4096)) return 0;
    
    size_t len = safe_strlen(path, 4096);
    if (len == 0 || len >= 4096) return 0;
    
    // Check for path traversal attacks
    if (strstr(path, "..") != NULL) return 0;
    if (strstr(path, "//") != NULL) return 0;
    
    return 1;
}

static void safe_print_hex_ptr(const char* label, void* ptr) {
    if (!label || !is_valid_string(label, 256)) return;
    
    vga_print(label);
    serial_write(label);
    vga_print(": 0x");
    serial_write(": 0x");
    
    unsigned long val = (unsigned long)ptr;
    char hex_buf[17] = {0};
    
    for (int i = 15; i >= 0; i--) {
        unsigned char d = (val >> (i * 4)) & 0xF;
        hex_buf[15-i] = (d < 10) ? ('0' + d) : ('A' + (d - 10));
    }
    hex_buf[16] = '\0';
    
    vga_print(hex_buf);
    serial_write(hex_buf);
    vga_print("\n");
    serial_write("\n");
}

void vfs_init() {
    vga_print("[VFS] Initializing VFS safely...\n");
    serial_write("[VFS] Initializing VFS safely...\n");
    
    int result = rust_vfs_init();
    
    char result_str[16];
    safe_snprintf(result_str, sizeof(result_str), "%d", result);
    
    vga_print("[VFS] rust_vfs_init returned: ");
    vga_print(result_str);
    vga_print("\n");
    
    serial_write("[VFS] rust_vfs_init returned: ");
    serial_write(result_str);
    serial_write("\n");
    
    if (result == 0) {
        rust_vfs_ready = 1;
        vga_print("[VFS] VFS initialized successfully\n");
        serial_write("[VFS] VFS initialized successfully\n");
    } else {
        rust_vfs_ready = 0;
        vga_print("[VFS] VFS initialization failed\n");
        serial_write("[VFS] VFS initialization failed\n");
    }
}

vfs_node_t* vfs_get_root() {
    void* result = NULL;
    
    if (rust_vfs_ready) {
        void* rust_root = rust_vfs_get_root();
        if (rust_root && is_valid_pointer(rust_root)) {
            result = rust_root;
        }
    }
    
    if (!result) {
        result = &fallback_root;
    }
    
    safe_print_hex_ptr("[VFS] vfs_get_root returns", result);
    return (vfs_node_t*)result;
}

vfs_node_t* vfs_find(const char* path, vfs_node_t* cwd) {
    if (!is_valid_path(path)) {
        vga_print("[VFS] Invalid path in vfs_find\n");
        return NULL;
    }
    
    if (!rust_vfs_ready) {
        vga_print("[VFS] VFS not ready, cannot find ");
        vga_print(path);
        vga_print("\n");
        return NULL;
    }
    
    // TODO: Implement safe path resolution
    return NULL;
}

vfs_node_t* vfs_create(const char* name, int type, vfs_node_t* parent) {
    if (!is_valid_path(name)) {
        vga_print("[VFS] Invalid name in vfs_create\n");
        return NULL;
    }
    
    if (!rust_vfs_ready) {
        vga_print("[VFS] VFS not ready, cannot create ");
        vga_print(name);
        vga_print("\n");
        return NULL;
    }
    
    // TODO: Implement safe node creation
    return NULL;
}

int vfs_write(vfs_node_t* node, const void* buf, int size) {
    if (!node || !is_valid_pointer(node)) {
        vga_print("[VFS] Invalid node in vfs_write\n");
        return -1;
    }
    
    if (!buf || size <= 0 || size > 1024*1024) {
        vga_print("[VFS] Invalid buffer in vfs_write\n");
        return -1;
    }
    
    if (!is_valid_buffer(buf, size)) {
        vga_print("[VFS] Invalid buffer pointer in vfs_write\n");
        return -1;
    }
    
    if (!rust_vfs_ready) {
        vga_print("[VFS] VFS not ready, cannot write\n");
        return -1;
    }
    
    // TODO: Implement safe write
    return -1;
}

int vfs_read(vfs_node_t* node, void* buf, int size) {
    if (!node || !is_valid_pointer(node)) {
        vga_print("[VFS] Invalid node in vfs_read\n");
        return -1;
    }
    
    if (!buf || size <= 0 || size > 1024*1024) {
        vga_print("[VFS] Invalid buffer in vfs_read\n");
        return -1;
    }
    
    if (!is_valid_buffer(buf, size)) {
        vga_print("[VFS] Invalid buffer pointer in vfs_read\n");
        return -1;
    }
    
    if (!rust_vfs_ready) {
        vga_print("[VFS] VFS not ready, cannot read\n");
        return -1;
    }
    
    // TODO: Implement safe read
    return -1;
}

void vfs_list(vfs_node_t* dir) {
    if (!dir || !is_valid_pointer(dir)) {
        vga_print("[VFS] Invalid directory in vfs_list\n");
        return;
    }
    
    if (!rust_vfs_ready) {
        vga_print("[VFS] VFS not ready, cannot list\n");
        return;
    }
    
    // TODO: Implement safe directory listing
}

int elf_load(const char* path) {
    if (!is_valid_path(path)) {
        serial_write("[ELF] Invalid path in elf_load\n");
        return -1;
    }
    
    serial_write("[ELF] elf_load: validating path\n");
    
    // Additional path validation
    size_t path_len = safe_strlen(path, 4096);
    if (path_len == 0 || path_len >= 4096) {
        serial_write("[ELF] Path length invalid\n");
        return -2;
    }
    
    serial_write("[ELF] elf_load: calling rust_elf_load\n");
    
    int ret = rust_elf_load(path);
    
    char ret_str[16];
    safe_snprintf(ret_str, sizeof(ret_str), "%d", ret);
    serial_write("[ELF] elf_load: returned ");
    serial_write(ret_str);
    serial_write("\n");
    
    return ret;
}

vfs_node_t* vfs_root = NULL;

void vfs_init_root() {
    if (!vfs_root) {
        vfs_root = vfs_get_root();
    }
}
