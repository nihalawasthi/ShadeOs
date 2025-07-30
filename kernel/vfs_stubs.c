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
extern void* rust_kmalloc(size_t size);

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
    
    // Implement safe path resolution through Rust VFS
    // For now, we'll return a fallback node for existing files
    // In a full implementation, this would traverse the directory tree
    
    // Try to read the file to see if it exists
    char test_buf[1];
    int result = rust_vfs_read(path, test_buf, 1);
    
    if (result >= 0) {
        // File exists, create a temporary node representation
        vfs_node_t* temp_node = (vfs_node_t*)rust_kmalloc(sizeof(vfs_node_t));
        if (!temp_node) return NULL; // Handle memory allocation failure
        *temp_node = (vfs_node_t){
            .used = 1,
            .node_type = VFS_TYPE_FILE,
            .name = "temp",
            .size = 0,
            .parent = NULL,
            .child = NULL,
            .sibling = NULL
        };
        temp_node->size = result;
        return temp_node;
    }
    
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
    
    if (parent == NULL || !parent->used || parent->node_type != VFS_TYPE_DIR) {
        vga_print("[VFS] Invalid parent in vfs_create\n");
        return NULL;
    }

    // Call Rust VFS to create a new file or directory
    int result;
    if (type == VFS_TYPE_DIR) {
        result = rust_vfs_mkdir(name);
    } else if (type == VFS_TYPE_FILE) {
        result = rust_vfs_create_file(name);
    } else {
        vga_print("[VFS] Unknown type in vfs_create\n");
        return NULL;
    }

    if (result != 0) {
        vga_print("[VFS] Failed to create node\n");
        return NULL;
    }

    // Assume success and return a simple node reference
    static vfs_node_t temp_node;
    temp_node.used = 1;
    temp_node.node_type = type;
    safe_strncpy(temp_node.name, name, sizeof(temp_node.name));
    temp_node.size = 0;
    temp_node.parent = parent;
    temp_node.child = NULL;
    temp_node.sibling = NULL;

    return &temp_node;
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
    
    // Convert node to path (simplified, real implementation would be more complex)
    const char* path = node->name;

    // Use Rust VFS to write to a file
    uint64_t result = rust_vfs_write(path, buf, size);

    if (result > 0) {
        return (int)result;
    } else {
        vga_print("[VFS] Write failed\n");
        return -1;
    }
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
    
    // Convert node to path (simplified, real implementation would be more complex)
    const char* path = node->name;

    // Use Rust VFS to read from a file
    int result = rust_vfs_read(path, buf, size);

    if (result >= 0) {
        return result;
    } else {
        vga_print("[VFS] Read failed\n");
        return -1;
    }
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
    
    if (dir->node_type != VFS_TYPE_DIR) {
        vga_print("[VFS] Node is not a directory\n");
        return;
    }

    // Call Rust VFS to list directory contents
    rust_vfs_ls(dir->name);
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
