#ifndef FAT_H
#define FAT_H

#include "kernel.h"
#include "blockdev.h"

int fat_mount(blockdev_t* dev);
void fat_ls(const char* path);
int fat_read(const char* path, void* buf, int maxlen);
int fat_write(const char* path, const void* buf, int len);
int fat_create(const char* path);

#endif 