#include "vfs.h"
#include "heap.h"
#include "kernel.h"

static vfs_file_t files[MAX_FILES];

void vfs_init() {
    for (int i = 0; i < MAX_FILES; i++) {
        files[i].used = 0;
        files[i].data = NULL;
    }
}

int vfs_create(const char* name, int type) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (!files[i].used) {
            files[i].used = 1;
            files[i].type = type;
            files[i].size = 0;
            files[i].pos = 0;
            files[i].data = kmalloc(MAX_FILE_SIZE);
            for (int j = 0; j < MAX_FILE_NAME; j++) files[i].name[j] = 0;
            for (int j = 0; name[j] && j < MAX_FILE_NAME-1; j++) files[i].name[j] = name[j];
            return i;
        }
    }
    return -1;
}

int vfs_open(const char* name) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (files[i].used && !strcmp(files[i].name, name)) {
            files[i].pos = 0;
            return i;
        }
    }
    return -1;
}

int vfs_read(int fd, void* buf, int size) {
    if (fd < 0 || fd >= MAX_FILES || !files[fd].used) return -1;
    int to_read = size;
    if (files[fd].pos + to_read > files[fd].size) to_read = files[fd].size - files[fd].pos;
    if (to_read <= 0) return 0;
    memcpy(buf, (uint8_t*)files[fd].data + files[fd].pos, to_read);
    files[fd].pos += to_read;
    return to_read;
}

int vfs_write(int fd, const void* buf, int size) {
    if (fd < 0 || fd >= MAX_FILES || !files[fd].used) return -1;
    int to_write = size;
    if (files[fd].pos + to_write > MAX_FILE_SIZE) to_write = MAX_FILE_SIZE - files[fd].pos;
    if (to_write <= 0) return 0;
    memcpy((uint8_t*)files[fd].data + files[fd].pos, buf, to_write);
    files[fd].pos += to_write;
    if (files[fd].pos > files[fd].size) files[fd].size = files[fd].pos;
    return to_write;
}

void vfs_close(int fd) {
    if (fd < 0 || fd >= MAX_FILES || !files[fd].used) return;
    files[fd].pos = 0;
} 