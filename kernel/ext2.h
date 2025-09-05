#ifndef EXT2_H
#define EXT2_H

#include "kernel.h"
#include "blockdev.h"
#include "vfs.h"
#include <stdint.h>

// Define stat structure for filesystem operations
struct stat {
    uint64_t st_dev;
    uint64_t st_ino;
    uint16_t st_mode;
    uint16_t st_nlink;
    uint16_t st_uid;
    uint16_t st_gid;
    uint64_t st_size;
    uint64_t st_atime;
    uint64_t st_mtime;
    uint64_t st_ctime;
};

// ext2/ext4 constants
#define EXT2_SUPER_MAGIC 0xEF53
#define EXT2_REV0 0
#define EXT2_REV1 1
#define EXT2_DYNAMIC_REV 1

// Inode types
#define EXT2_S_IFSOCK 0xC000
#define EXT2_S_IFLNK  0xA000
#define EXT2_S_IFREG  0x8000
#define EXT2_S_IFBLK  0x6000
#define EXT2_S_IFDIR  0x4000
#define EXT2_S_IFCHR  0x2000
#define EXT2_S_IFIFO  0x1000

// File permissions
#define EXT2_S_IRWXU 0x01C0
#define EXT2_S_IRUSR 0x0100
#define EXT2_S_IWUSR 0x0080
#define EXT2_S_IXUSR 0x0040
#define EXT2_S_IRWXG 0x0038
#define EXT2_S_IRGRP 0x0020
#define EXT2_S_IWGRP 0x0010
#define EXT2_S_IXGRP 0x0008
#define EXT2_S_IRWXO 0x0007
#define EXT2_S_IROTH 0x0004
#define EXT2_S_IWOTH 0x0002
#define EXT2_S_IXOTH 0x0001

// ext2 superblock structure
typedef struct ext2_superblock {
    uint32_t s_inodes_count;
    uint32_t s_blocks_count;
    uint32_t s_r_blocks_count;
    uint32_t s_free_blocks_count;
    uint32_t s_free_inodes_count;
    uint32_t s_first_data_block;
    uint32_t s_log_block_size;
    uint32_t s_log_frag_size;
    uint32_t s_blocks_per_group;
    uint32_t s_frags_per_group;
    uint32_t s_inodes_per_group;
    uint32_t s_mtime;
    uint32_t s_wtime;
    uint16_t s_mnt_count;
    uint16_t s_max_mnt_count;
    uint16_t s_magic;
    uint16_t s_state;
    uint16_t s_errors;
    uint16_t s_minor_rev_level;
    uint32_t s_lastcheck;
    uint32_t s_checkinterval;
    uint32_t s_creator_os;
    uint32_t s_rev_level;
    uint16_t s_def_resuid;
    uint16_t s_def_resgid;
    uint32_t s_first_ino;
    uint16_t s_inode_size;
    uint16_t s_block_group_nr;
    uint32_t s_feature_compat;
    uint32_t s_feature_incompat;
    uint32_t s_feature_ro_compat;
    uint8_t s_uuid[16];
    uint8_t s_volume_name[16];
    uint8_t s_last_mounted[64];
    uint8_t s_journal_uuid[16];
} __attribute__((packed)) ext2_superblock_t;

// ext2 group descriptor structure
typedef struct ext2_group_desc {
    uint32_t bg_block_bitmap;
    uint32_t bg_inode_bitmap;
    uint32_t bg_inode_table;
    uint16_t bg_free_blocks_count;
    uint16_t bg_free_inodes_count;
    uint16_t bg_used_dirs_count;
    uint16_t bg_pad;
    uint8_t bg_reserved[12];
} __attribute__((packed)) ext2_group_desc_t;

// ext2 inode structure
typedef struct ext2_inode {
    uint16_t i_mode;
    uint16_t i_uid;
    uint32_t i_size;
    uint32_t i_atime;
    uint32_t i_ctime;
    uint32_t i_mtime;
    uint32_t i_dtime;
    uint16_t i_gid;
    uint16_t i_links_count;
    uint32_t i_blocks;
    uint32_t i_flags;
    uint32_t i_osd1;
    uint32_t i_block[15];
    uint32_t i_generation;
    uint32_t i_file_acl;
    uint32_t i_dir_acl;
    uint32_t i_faddr;
    uint8_t i_frag;
    uint8_t i_fsize;
    uint16_t i_pad1;
    uint16_t i_reserved2[2];
} __attribute__((packed)) ext2_inode_t;

// ext2 directory entry structure
typedef struct ext2_dir_entry {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t name_len;
    uint8_t file_type;
    char name[];
} __attribute__((packed)) ext2_dir_entry_t;

// ext2 filesystem context
typedef struct ext2_fs {
    blockdev_t* device;
    ext2_superblock_t superblock;
    ext2_group_desc_t* group_descriptors;
    uint32_t block_size;
    uint32_t inodes_per_group;
    uint32_t blocks_per_group;
    uint32_t group_count;
    uint32_t inode_size;
    uint32_t first_inode;
} ext2_fs_t;

// ext2 file handle
typedef struct ext2_file {
    ext2_fs_t* fs;
    uint32_t inode_num;
    ext2_inode_t inode;
    uint32_t position;
    uint32_t size;
    uint8_t* buffer;
    uint32_t buffer_block;
    int buffer_dirty;
} ext2_file_t;

// Function declarations
int ext2_init(blockdev_t* device);
int ext2_c_init(blockdev_t* device);
int ext2_mount(const char* path, blockdev_t* device);
int ext2_umount(const char* path);

// File operations
ext2_file_t* ext2_open(const char* path, int flags);
int ext2_close(ext2_file_t* file);
int ext2_read(ext2_file_t* file, void* buf, size_t count);
int ext2_write(ext2_file_t* file, const void* buf, size_t count);
int ext2_seek(ext2_file_t* file, int64_t offset, int whence);

// Directory operations
int ext2_mkdir(const char* path);
int ext2_rmdir(const char* path);
int ext2_readdir(ext2_file_t* dir, ext2_dir_entry_t* entry);

// Inode operations
int ext2_create_inode(const char* path, uint16_t mode);
int ext2_delete_inode(const char* path);
int ext2_stat(const char* path, struct stat* statbuf);

// Block operations
int ext2_read_block(ext2_fs_t* fs, uint32_t block_num, void* buf);
int ext2_write_block(ext2_fs_t* fs, uint32_t block_num, const void* buf);
uint32_t ext2_alloc_block(ext2_fs_t* fs);
int ext2_free_block(ext2_fs_t* fs, uint32_t block_num);

// Inode operations
int ext2_read_inode(ext2_fs_t* fs, uint32_t inode_num, ext2_inode_t* inode);
int ext2_write_inode(ext2_fs_t* fs, uint32_t inode_num, const ext2_inode_t* inode);
uint32_t ext2_alloc_inode(ext2_fs_t* fs);
int ext2_free_inode(ext2_fs_t* fs, uint32_t inode_num);

// Utility functions
uint32_t ext2_inode_to_block(ext2_fs_t* fs, uint32_t inode_num);
uint32_t ext2_block_to_inode(ext2_fs_t* fs, uint32_t block_num);
int ext2_path_to_inode(ext2_fs_t* fs, const char* path, uint32_t* inode_num);
int ext2_get_block_number(ext2_fs_t* fs, const ext2_inode_t* inode, uint32_t block_index, uint32_t* block_num);

// VFS integration
int ext2_vfs_mount(const char* path, blockdev_t* device);
int ext2_vfs_open(const char* path, int flags);
int ext2_vfs_read(const char* path, void* buf, int max_len);
int ext2_vfs_write(const char* path, const void* buf, int len);
int ext2_vfs_mkdir(const char* path);
int ext2_vfs_rmdir(const char* path);
int ext2_vfs_unlink(const char* path);
int ext2_vfs_stat(const char* path, struct stat* statbuf);

#endif // EXT2_H 