#ifndef VFS_H
#define VFS_H

#include "kernel.h"

#define MAX_FILES 64
#define MAX_FILE_NAME 32
#define MAX_FILE_SIZE 4096

// File types
#define VFS_TYPE_MEM 1
#define VFS_TYPE_DIR 2
#define VFS_TYPE_FAT 3 // FAT16-backed file

// File/directory node
typedef struct vfs_node {
    char name[MAX_FILE_NAME];
    int type;
    int size;
    int pos;
    void* data;
    int used;
    struct vfs_node* parent;
    struct vfs_node* child;
    struct vfs_node* sibling;
    char fat_filename[12]; // 8.3 name for FAT files, null-terminated
} vfs_node_t;

void vfs_init();
vfs_node_t* vfs_create(const char* name, int type, vfs_node_t* parent);
vfs_node_t* vfs_find(const char* path, vfs_node_t* cwd);
int vfs_read(vfs_node_t* node, void* buf, int size);
int vfs_write(vfs_node_t* node, const void* buf, int size);
void vfs_list(vfs_node_t* dir);

// For shell integration
vfs_node_t* vfs_get_root();

#endif 