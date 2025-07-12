#include "vfs.h"
#include "heap.h"
#include "kernel.h"
#include "string.h"
#include "fat.h"
#include "serial.h"
#include <stdint.h>

static vfs_node_t* nodes = NULL;
static vfs_node_t* root;
static uint32_t* nodes_canary = NULL;

vfs_node_t* vfs_get_root() { return root; }

void vfs_init() {
    serial_write("[VFS] vfs_init start\n");
    // Print address of nodes (will be set after allocation)
    // Print stack pointer
    uint64_t rsp_val;
    __asm__ volatile ("mov %%rsp, %0" : "=r"(rsp_val));
    serial_write("[VFS] RSP: 0x");
    char rsp_hex[17];
    for (int i = 0; i < 16; i++) {
        int nibble = (rsp_val >> ((15 - i) * 4)) & 0xF;
        rsp_hex[i] = (nibble < 10) ? ('0' + nibble) : ('A' + nibble - 10);
    }
    rsp_hex[16] = 0;
    serial_write(rsp_hex);
    serial_write("\n");
    
    // Test small allocation first
    serial_write("[VFS] Testing small allocation\n");
    void* test_ptr = kmalloc(16);
    if (test_ptr) {
        *(uint32_t*)test_ptr = 0x12345678;
        if (*(uint32_t*)test_ptr == 0x12345678) {
            serial_write("[VFS] Small allocation test passed\n");
        } else {
            serial_write("[VFS] ERROR: Small allocation test failed\n");
            return;
        }
    } else {
        serial_write("[VFS] ERROR: Small allocation failed\n");
        return;
    }
    
    size_t alloc_size = sizeof(vfs_node_t) * MAX_FILES;
    nodes = (vfs_node_t*)kmalloc(alloc_size);
    serial_write("[VFS] kmalloc completed\n");
    if (!nodes) {
        serial_write("[VFS] ERROR: kmalloc for nodes failed!\n");
        return;
    }
    // Alignment check
    if (((uint64_t)nodes) % 8 != 0) {
        serial_write("[VFS] ERROR: nodes array is not 8-byte aligned!\n");
        return;
    }
    
    // Test memory access
    serial_write("[VFS] Testing memory access\n");
    *(uint32_t*)nodes = 0x12345678;
    if (*(uint32_t*)nodes == 0x12345678) {
        serial_write("[VFS] Memory access test passed\n");
    } else {
        serial_write("[VFS] ERROR: Memory access test failed\n");
        return;
    }
    
    // Canary removed - not enough space allocated
    serial_write("[VFS] Starting node initialization\n");
    
    // Test direct access to nodes array
    serial_write("[VFS] Testing nodes array access\n");
    if (nodes) {
        serial_write("[VFS] nodes pointer is valid\n");
        // Try to read the first byte of the first node
        uint8_t test_byte = *(uint8_t*)nodes;
        serial_write("[VFS] First byte read successfully\n");
        
        // Test structure alignment by accessing the first field
        serial_write("[VFS] Testing structure alignment\n");
        vfs_node_t* first_node = &nodes[0];
        if (first_node) {
            serial_write("[VFS] First node pointer valid\n");
            // Try to access the first field (name array)
            first_node->name[0] = 0;
            serial_write("[VFS] Structure alignment test passed\n");
            
            // Test accessing the used field specifically
            serial_write("[VFS] Testing used field access\n");
            first_node->used = 0;
            serial_write("[VFS] Used field access test passed\n");
        } else {
            serial_write("[VFS] ERROR: First node pointer invalid\n");
            return;
        }
    } else {
        serial_write("[VFS] ERROR: nodes pointer is NULL\n");
        return;
    }
    
    for (int i = 0; i < MAX_FILES; i++) {
        nodes[i].used = 0;
        nodes[i].data = NULL;
        nodes[i].parent = NULL;
        nodes[i].child = NULL;
        nodes[i].sibling = NULL;
        nodes[i].type = 0;
        nodes[i].size = 0;
        nodes[i].pos = 0;
        
        // Clear name array
        for (int j = 0; j < MAX_FILE_NAME; j++) {
            nodes[i].name[j] = 0;
        }
        
        // Clear fat_filename
        for (int j = 0; j < 12; j++) {
            nodes[i].fat_filename[j] = 0;
        }
    }
    serial_write("[VFS] Setting root\n");
    root = &nodes[0];
    root->used = 1;
    root->type = VFS_TYPE_DIR;
    root->size = 0;
    root->pos = 0;
    serial_write("[VFS] Root size and pos set\n");
    root->parent = NULL;
    root->child = NULL;
    root->sibling = NULL;
    serial_write("[VFS] Root parent/child/sibling set\n");
    root->name[0] = '/';
    root->name[1] = 0;
    serial_write("[VFS] Root name set\n");
    serial_write("[VFS] vfs_init complete\n");

    serial_write("[VFS] vfs_init complete\n");
}

vfs_node_t* vfs_create(const char* name, int type, vfs_node_t* parent) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (!nodes[i].used) {
            nodes[i].used = 1;
            nodes[i].type = type;
            nodes[i].size = 0;
            nodes[i].pos = 0;

            if (type == VFS_TYPE_MEM) {
                nodes[i].data = kmalloc(MAX_FILE_SIZE);
            } else {
                nodes[i].data = NULL;
            }

            nodes[i].parent = parent;
            nodes[i].child = NULL;
            nodes[i].sibling = NULL;

            for (int j = 0; j < MAX_FILE_NAME; j++) nodes[i].name[j] = 0;
            for (int j = 0; name[j] && j < MAX_FILE_NAME-1; j++) nodes[i].name[j] = name[j];

            for (int j = 0; j < 12; j++) nodes[i].fat_filename[j] = 0;

            // FAT16 support: if creating in root, also create in FAT
            if (parent == root && type == VFS_TYPE_MEM) {
                if (fat_create(name) == 0) {
                    nodes[i].type = VFS_TYPE_FAT;
                    int n = 0, k = 0;
                    while (name[k] && name[k] != '.' && n < 8) nodes[i].fat_filename[n++] = (name[k] >= 'a' && name[k] <= 'z') ? name[k]-32 : name[k++];
                    while (n < 8) nodes[i].fat_filename[n++] = ' ';
                    if (name[k] == '.') k++;
                    int e = 0;
                    while (name[k] && e < 3) nodes[i].fat_filename[8+e++] = (name[k] >= 'a' && name[k] <= 'z') ? name[k]-32 : name[k++];
                    while (e < 3) nodes[i].fat_filename[8+e++] = ' ';
                    nodes[i].fat_filename[11] = 0;
                }
            }

            if (parent) {
                if (!parent->child) parent->child = &nodes[i];
                else {
                    vfs_node_t* sib = parent->child;
                    while (sib->sibling) sib = sib->sibling;
                    sib->sibling = &nodes[i];
                }
            }
            return &nodes[i];
        }
    }
    return NULL;
}

// Find node by path (absolute or relative to cwd)
vfs_node_t* vfs_find(const char* path, vfs_node_t* cwd) {
    if (!path || !*path) return NULL;
    vfs_node_t* cur = (path[0] == '/') ? root : cwd;
    int i = (path[0] == '/') ? 1 : 0;
    char part[MAX_FILE_NAME];
    while (path[i]) {
        int j = 0;
        while (path[i] && path[i] != '/') part[j++] = path[i++];
        part[j] = 0;
        if (path[i] == '/') i++;
        if (!part[0]) continue;
        vfs_node_t* child = cur->child;
        while (child && strcmp(child->name, part)) child = child->sibling;
        if (!child) return NULL;
        cur = child;
    }
    return cur;
}

void vfs_list(vfs_node_t* dir) {
    if (!dir || dir->type != VFS_TYPE_DIR) return;
    vfs_node_t* child = dir->child;
    while (child) {
        vga_print(child->name);
        if (child->type == VFS_TYPE_DIR) vga_print("/");
        vga_print("  ");
        child = child->sibling;
    }
    vga_print("\n");
}

int vfs_read(vfs_node_t* node, void* buf, int size) {
    serial_write("[VFS] vfs_read called\n");
    if (!node) return -1;
    if (node->type == VFS_TYPE_MEM) {
        int to_read = size;
        if (node->pos + to_read > node->size) to_read = node->size - node->pos;
        if (to_read <= 0) return 0;
        memcpy(buf, (uint8_t*)node->data + node->pos, to_read);
        node->pos += to_read;
        serial_write("[VFS] Read from memory file successful\n");
        return to_read;
    } else if (node->type == VFS_TYPE_FAT) {
        serial_write("[VFS] Read from FAT file\n");
        return fat_read(node->name, buf, size);
    }
    serial_write("[VFS] Read failed\n");
    return -1;
}

int vfs_write(vfs_node_t* node, const void* buf, int size) {
    serial_write("[VFS] vfs_write called\n");
    if (!node) return -1;
    if (node->type == VFS_TYPE_MEM) {
        int to_write = size;
        if (node->pos + to_write > MAX_FILE_SIZE) to_write = MAX_FILE_SIZE - node->pos;
        if (to_write <= 0) return 0;
        memcpy((uint8_t*)node->data + node->pos, buf, to_write);
        node->pos += to_write;
        if (node->pos > node->size) node->size = node->pos;
        serial_write("[VFS] Write to memory file successful\n");
        return to_write;
    } else if (node->type == VFS_TYPE_FAT) {
        serial_write("[VFS] Write to FAT file\n");
        return fat_write(node->name, buf, size);
    }
    serial_write("[VFS] Write failed\n");
    return -1;
} 