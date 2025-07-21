#ifndef VFS_H
#define VFS_H

#include "kernel.h"
#include <stdint.h>

#define MAX_FILE_NAME 256

// File types
#define VFS_TYPE_UNUSED 0
#define VFS_TYPE_DIR 1
#define VFS_TYPE_FILE 2

// FFI-compatible vfs_node_t (matches Rust VfsNode)
typedef struct vfs_node {
    uint8_t used;
    uint8_t node_type;
    char name[32];
    uint32_t size;
    struct vfs_node* parent;
    struct vfs_node* child;
    struct vfs_node* sibling;
} vfs_node_t;

// These functions will now call into the Rust VFS
void vfs_init();
vfs_node_t* vfs_create(const char* name, int type, vfs_node_t* parent);
vfs_node_t* vfs_find(const char* path, vfs_node_t* cwd);
int vfs_read(vfs_node_t* node, void* buf, int size);
int vfs_write(vfs_node_t* node, const void* buf, int size);
void vfs_list(vfs_node_t* dir);

// For shell integration
vfs_node_t* vfs_get_root();

// Rust VFS function declarations
extern int rust_vfs_init();
extern int rust_vfs_mkdir(const char* path);
extern int rust_vfs_ls(const char* path);
extern int rust_vfs_read(const char* path, void* buf, int max_len);
extern uint64_t rust_vfs_write(const char* path, const void* buf, uint64_t len);
extern void* rust_vfs_get_root();
extern int rust_vfs_create_file(const char* path);
extern int rust_vfs_unlink(const char* path);
extern int rust_vfs_stat(const char* path, struct vfs_node* stat_out);

#endif
