#include "fat.h"
#include "blockdev.h"
#include "vga.h"
#include "string.h"

#define SECTOR_SIZE 512
#define ROOT_DIR_SECTORS 14 // For 224 entries (FAT16 default)

static blockdev_t* fat_dev = 0;
static int fat_root_dir_sector = 0;
static int fat_data_sector = 0;
static int fat_sectors_per_cluster = 0;
static int fat_first_fat_sector = 0;
static int fat_num_fats = 0;
static int fat_fat_size = 0;

struct fat16_dir_entry {
    char name[8];
    char ext[3];
    uint8_t attr;
    uint8_t reserved[10];
    uint16_t time;
    uint16_t date;
    uint16_t start_cluster;
    uint32_t size;
} __attribute__((packed));

int fat_mount(blockdev_t* dev) {
    uint8_t sector[SECTOR_SIZE];
    if (dev->read(0, sector, 1) != 0) return -1;
    fat_dev = dev;
    fat_sectors_per_cluster = sector[13];
    fat_num_fats = sector[16];
    fat_root_dir_sector = sector[14] | (sector[15]<<8) + fat_num_fats * (sector[22] | (sector[23]<<8)) + (sector[11] | (sector[12]<<8));
    fat_fat_size = sector[22] | (sector[23]<<8);
    fat_first_fat_sector = (sector[11] | (sector[12]<<8));
    fat_data_sector = fat_root_dir_sector + ROOT_DIR_SECTORS;
    return 0;
}

void fat_ls(const char* path) {
    (void)path;
    uint8_t sector[SECTOR_SIZE];
    for (int s = 0; s < ROOT_DIR_SECTORS; s++) {
        if (fat_dev->read(fat_root_dir_sector + s, sector, 1) != 0) return;
        for (int i = 0; i < SECTOR_SIZE/32; i++) {
            struct fat16_dir_entry* e = (struct fat16_dir_entry*)(sector + i*32);
            if (e->name[0] == 0x00) return;
            if (e->name[0] == 0xE5) continue;
            char name[13] = {0};
            memcpy(name, e->name, 8);
            for (int j = 7; j >= 0 && name[j] == ' '; j--) name[j] = 0;
            if (e->ext[0] != ' ') {
                int l = strlen(name);
                name[l] = '.';
                memcpy(name+l+1, e->ext, 3);
                for (int j = l+3; j >= l+1 && name[j] == ' '; j--) name[j] = 0;
            }
            vga_print(name);
            vga_print("  ");
        }
    }
    vga_print("\n");
}

int fat_read(const char* path, void* buf, int maxlen) {
    // Only support root dir, 8.3 names
    char name[11] = {0};
    int n = 0, i = 0;
    while (path[i] && path[i] != '.' && n < 8) name[n++] = (path[i] >= 'a' && path[i] <= 'z') ? path[i]-32 : path[i];
    while (n < 8) name[n++] = ' ';
    if (path[i] == '.') i++;
    int e = 0;
    while (path[i] && e < 3) name[8+e++] = (path[i] >= 'a' && path[i] <= 'z') ? path[i]-32 : path[i];
    while (e < 3) name[8+e++] = ' ';
    uint8_t sector[SECTOR_SIZE];
    for (int s = 0; s < ROOT_DIR_SECTORS; s++) {
        if (fat_dev->read(fat_root_dir_sector + s, sector, 1) != 0) return -1;
        for (int i = 0; i < SECTOR_SIZE/32; i++) {
            struct fat16_dir_entry* e = (struct fat16_dir_entry*)(sector + i*32);
            if (memcmp(e->name, name, 11) == 0) {
                int cluster = e->start_cluster;
                int size = e->size;
                int to_read = (maxlen < size) ? maxlen : size;
                int read = 0;
                while (to_read > 0) {
                    int sec = fat_data_sector + (cluster-2)*fat_sectors_per_cluster;
                    for (int j = 0; j < fat_sectors_per_cluster && to_read > 0; j++) {
                        int chunk = (to_read > SECTOR_SIZE) ? SECTOR_SIZE : to_read;
                        if (fat_dev->read(sec+j, (uint8_t*)buf+read, 1) != 0) return -1;
                        read += chunk;
                        to_read -= chunk;
                    }
                    // Only support contiguous files (no FAT chain follow)
                    break;
                }
                return read;
            }
        }
    }
    return -1;
}

int fat_write(const char* path, const void* buf, int len) {
    // Only support root dir, 8.3 names, contiguous allocation
    char name[11] = {0};
    int n = 0, i = 0;
    while (path[i] && path[i] != '.' && n < 8) name[n++] = (path[i] >= 'a' && path[i] <= 'z') ? path[i]-32 : path[i];
    while (n < 8) name[n++] = ' ';
    if (path[i] == '.') i++;
    int e = 0;
    while (path[i] && e < 3) name[8+e++] = (path[i] >= 'a' && path[i] <= 'z') ? path[i]-32 : path[i];
    while (e < 3) name[8+e++] = ' ';
    uint8_t sector[SECTOR_SIZE];
    for (int s = 0; s < ROOT_DIR_SECTORS; s++) {
        if (fat_dev->read(fat_root_dir_sector + s, sector, 1) != 0) return -1;
        for (int i = 0; i < SECTOR_SIZE/32; i++) {
            struct fat16_dir_entry* e = (struct fat16_dir_entry*)(sector + i*32);
            if (memcmp(e->name, name, 11) == 0) {
                int cluster = e->start_cluster;
                int size = e->size;
                int to_write = (len < size) ? len : size;
                int written = 0;
                while (to_write > 0) {
                    int sec = fat_data_sector + (cluster-2)*fat_sectors_per_cluster;
                    for (int j = 0; j < fat_sectors_per_cluster && to_write > 0; j++) {
                        int chunk = (to_write > SECTOR_SIZE) ? SECTOR_SIZE : to_write;
                        if (fat_dev->write(sec+j, (const uint8_t*)buf+written, 1) != 0) return -1;
                        written += chunk;
                        to_write -= chunk;
                    }
                    // Only support contiguous files (no FAT chain follow)
                    break;
                }
                // Update file size if needed
                if (len > size) {
                    e->size = len;
                    fat_dev->write(fat_root_dir_sector + s, sector, 1);
                }
                return written;
            }
        }
    }
    return -1;
}

int fat_create(const char* path) {
    // Only support root dir, 8.3 names, contiguous allocation
    char name[11] = {0};
    int n = 0, i = 0;
    while (path[i] && path[i] != '.' && n < 8) name[n++] = (path[i] >= 'a' && path[i] <= 'z') ? path[i]-32 : path[i];
    while (n < 8) name[n++] = ' ';
    if (path[i] == '.') i++;
    int e = 0;
    while (path[i] && e < 3) name[8+e++] = (path[i] >= 'a' && path[i] <= 'z') ? path[i]-32 : path[i];
    while (e < 3) name[8+e++] = ' ';
    uint8_t sector[SECTOR_SIZE];
    // Find free dir entry
    for (int s = 0; s < ROOT_DIR_SECTORS; s++) {
        if (fat_dev->read(fat_root_dir_sector + s, sector, 1) != 0) return -1;
        for (int i = 0; i < SECTOR_SIZE/32; i++) {
            struct fat16_dir_entry* e = (struct fat16_dir_entry*)(sector + i*32);
            if (e->name[0] == 0x00 || e->name[0] == 0xE5) {
                memcpy(e->name, name, 11);
                e->attr = 0;
                memset(e->reserved, 0, sizeof(e->reserved));
                e->time = 0;
                e->date = 0;
                // Find free cluster (brute force)
                uint8_t fat[SECTOR_SIZE];
                for (int f = 0; f < fat_fat_size; f++) {
                    if (fat_dev->read(fat_first_fat_sector + f, fat, 1) != 0) return -1;
                    for (int c = 2; c < SECTOR_SIZE/2; c++) {
                        uint16_t* entry = (uint16_t*)(fat + c*2);
                        if (*entry == 0x0000) {
                            *entry = 0xFFFF; // Mark as EOF
                            if (fat_dev->write(fat_first_fat_sector + f, fat, 1) != 0) return -1;
                            e->start_cluster = f * (SECTOR_SIZE/2) + c;
                            e->size = 0;
                            fat_dev->write(fat_root_dir_sector + s, sector, 1);
                            return 0;
                        }
                    }
                }
                return -1; // No free cluster
            }
        }
    }
    return -1; // No free dir entry
} 