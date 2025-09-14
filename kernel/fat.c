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

// FAT stubs for VFS linkage
int fat_create(const char* name) { return -1; }
int fat_read(const char* path, void* buf, int maxlen) { return -1; }
int fat_write(const char* path, const void* buf, int len) { return -1; }
