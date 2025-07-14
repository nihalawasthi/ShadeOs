#ifndef VFS_H
#define VFS_H

#include "kernel.h"

#define MAX_FILE_NAME 256

// File types (might still be useful for shell logic, but actual VFS is Rust-managed)
#define VFS_TYPE_MEM 1
#define VFS_TYPE_DIR 2
#define VFS_TYPE_FAT 3

// Simplified vfs_node_t for compatibility, but actual file system state is in Rust
// This struct is now primarily a placeholder for the C shell's internal logic
// and will not directly represent file system nodes managed by Rust.
typedef struct vfs_node {
    char name[MAX_FILE_NAME];
    int type;
    int size;
    int pos;
    int used; // Indicates if this entry in the C-side 'nodes' array is in use
    void* data; // For VFS_TYPE_MEM, if we keep that concept
    struct vfs_node* parent;
    struct vfs_node* child;
    struct vfs_node* sibling;
    char fat_filename[12]; // No longer directly used by Rust VFS
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
extern int rust_vfs_write(const char* path, const void* buf, int len);

#endif
