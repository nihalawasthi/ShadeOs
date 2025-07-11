#ifndef VFS_H
#define VFS_H

#include "kernel.h"

#define MAX_FILES 16
#define MAX_FILE_NAME 32
#define MAX_FILE_SIZE 4096

// File types
#define VFS_TYPE_MEM 1

// File descriptor
typedef struct vfs_file {
    char name[MAX_FILE_NAME];
    int type;
    int size;
    int pos;
    void* data;
    int used;
} vfs_file_t;

void vfs_init();
int vfs_create(const char* name, int type);
int vfs_open(const char* name);
int vfs_read(int fd, void* buf, int size);
int vfs_write(int fd, const void* buf, int size);
void vfs_close(int fd);

#endif 