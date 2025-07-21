#include "blockdev.h"
#include "heap.h"
#include "string.h"
#include "serial.h"
#include "vga.h"

#define RAMDISK_SIZE (8*1024*1024) // Reduce to 8MB for BSS safety; use dynamic alloc for larger
static uint8_t ramdisk[RAMDISK_SIZE];

static int ramdisk_read(int sector, void* buf, int count) {
    int offset = sector * BLOCKDEV_SECTOR_SIZE;
    int bytes = count * BLOCKDEV_SECTOR_SIZE;
    if (offset + bytes > RAMDISK_SIZE) return -1;
    memcpy(buf, ramdisk + offset, bytes);
    return 0;
}
static int ramdisk_write(int sector, const void* buf, int count) {
    int offset = sector * BLOCKDEV_SECTOR_SIZE;
    int bytes = count * BLOCKDEV_SECTOR_SIZE;
    if (offset + bytes > RAMDISK_SIZE) return -1;
    memcpy(ramdisk + offset, buf, bytes);
    return 0;
}

static blockdev_t blockdevs[MAX_BLOCKDEVS];

void blockdev_init() {
    vga_print("[BLOCKDEV] Starting blockdev_init\n");
    
    vga_print("[BLOCKDEV] Setting blockdev[0].id = 0\n");
    blockdevs[0].id = 0;
    
    serial_write("[BLOCKDEV] Setting blockdev[0].read = ramdisk_read\n");
    blockdevs[0].read = ramdisk_read;
    
    vga_print("[BLOCKDEV] Setting blockdev[0].write = ramdisk_write\n");
    blockdevs[0].write = ramdisk_write;
    
    serial_write("[BLOCKDEV] Setting blockdev[0].total_sectors\n");
    blockdevs[0].total_sectors = RAMDISK_SIZE / BLOCKDEV_SECTOR_SIZE;
    
    serial_write("[BLOCKDEV] blockdev_init complete\n");
}

blockdev_t* blockdev_get(int id) {
    if (id < 0 || id >= MAX_BLOCKDEVS) return 0;
    if (blockdevs[id].read) return &blockdevs[id];
    return 0;
} 