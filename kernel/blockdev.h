#ifndef BLOCKDEV_H
#define BLOCKDEV_H

#include "kernel.h"

#define BLOCKDEV_SECTOR_SIZE 512
#define MAX_BLOCKDEVS 4

typedef struct blockdev {
    int id;
    int (*read)(int sector, void* buf, int count);
    int (*write)(int sector, const void* buf, int count);
    int total_sectors;
} blockdev_t;

void blockdev_init();
blockdev_t* blockdev_get(int id);

#endif 