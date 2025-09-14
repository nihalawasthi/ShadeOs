#include "ext2.h"
#include "heap.h"
#include "string.h"
#include "serial.h"
#include "vga.h"
#include "memory.h"
#include <stddef.h>

// Helper functions for string operations
static const char* strrchr(const char* str, int c) {
    const char* last = NULL;
    while (*str) {
        if (*str == c) {
            last = str;
        }
        str++;
    }
    return last;
}

static void strcpy(char* dest, const char* src) {
    while (*src) {
        *dest = *src;
        dest++;
        src++;
    }
    *dest = '\0';
}

// Global filesystem context
static ext2_fs_t* mounted_fs = NULL;
static int ext2_initialized = 0;

// ext2 filesystem initialization
int ext2_init(blockdev_t* device) {
    if (!device || !device->read) {
        vga_print("[EXT2] Invalid device\n");
        return -1;
    }
    
    vga_print("[EXT2] Initializing ext2 filesystem\n");
    serial_write("[EXT2] Initializing ext2 filesystem\n");
    
    // Allocate filesystem context
    mounted_fs = (ext2_fs_t*)rust_kmalloc(sizeof(ext2_fs_t));
    if (!mounted_fs) {
        vga_print("[EXT2] Failed to allocate filesystem context\n");
        serial_write("[EXT2] Failed to allocate filesystem context\n");
        return -1;
    }
    
    mounted_fs->device = device;
    
    // Read superblock from block 1 (block 0 is boot sector)
    uint8_t superblock_buf[512]; // Standard sector size
    if (device->read(1, superblock_buf, 1) != 0) {
        vga_print("[EXT2] Failed to read superblock\n");
        serial_write("[EXT2] Failed to read superblock\n");
        rust_kfree(mounted_fs);
        return -1;
    }
    
    // Copy superblock
    memcpy(&mounted_fs->superblock, superblock_buf, sizeof(ext2_superblock_t));
    
    // Verify magic number
    if (mounted_fs->superblock.s_magic != EXT2_SUPER_MAGIC) {
        vga_print("[EXT2] Invalid ext2 magic number\n");
        serial_write("[EXT2] Invalid ext2 magic number\n");
        rust_kfree(mounted_fs);
        return -1;
    }
    
    // Calculate filesystem parameters
    mounted_fs->block_size = 1024 << mounted_fs->superblock.s_log_block_size;
    mounted_fs->inodes_per_group = mounted_fs->superblock.s_inodes_per_group;
    mounted_fs->blocks_per_group = mounted_fs->superblock.s_blocks_per_group;
    mounted_fs->group_count = (mounted_fs->superblock.s_blocks_count + mounted_fs->blocks_per_group - 1) / mounted_fs->blocks_per_group;
    mounted_fs->inode_size = mounted_fs->superblock.s_inode_size;
    mounted_fs->first_inode = mounted_fs->superblock.s_first_ino;
    
    // Allocate and read group descriptors
    size_t gd_size = mounted_fs->group_count * sizeof(ext2_group_desc_t);
    mounted_fs->group_descriptors = (ext2_group_desc_t*)rust_kmalloc(gd_size);
    if (!mounted_fs->group_descriptors) {
        vga_print("[EXT2] Failed to allocate group descriptors\n");
        serial_write("[EXT2] Failed to allocate group descriptors\n");
        rust_kfree(mounted_fs);
        return -1;
    }
    
    // Group descriptors start at block 2
    uint32_t gd_blocks = (gd_size + mounted_fs->block_size - 1) / mounted_fs->block_size;
    uint8_t* gd_buf = (uint8_t*)rust_kmalloc(gd_blocks * mounted_fs->block_size);
    if (!gd_buf) {
        vga_print("[EXT2] Failed to allocate group descriptor buffer\n");
        serial_write("[EXT2] Failed to allocate group descriptor buffer\n");
        rust_kfree(mounted_fs->group_descriptors);
        rust_kfree(mounted_fs);
        return -1;
    }
    
    if (device->read(2, gd_buf, gd_blocks) != 0) {
        vga_print("[EXT2] Failed to read group descriptors\n");
        serial_write("[EXT2] Failed to read group descriptors\n");
        rust_kfree(gd_buf);
        rust_kfree(mounted_fs->group_descriptors);
        rust_kfree(mounted_fs);
        return -1;
    }
    
    memcpy(mounted_fs->group_descriptors, gd_buf, gd_size);
    rust_kfree(gd_buf);
    
    ext2_initialized = 1;
    vga_print("[EXT2] ext2 filesystem initialized successfully\n");
    serial_write("[EXT2] ext2 filesystem initialized successfully\n");
    
    return 0;
}

// C wrapper for ext2_init (for Rust integration)
int ext2_c_init(blockdev_t* device) {
    return ext2_init(device);
}

// Read a block from the filesystem
int ext2_read_block(ext2_fs_t* fs, uint32_t block_num, void* buf) {
    if (!fs || !fs->device || !buf) return -1;
    
    uint32_t sector = block_num * (fs->block_size / 512);
    uint32_t sectors = fs->block_size / 512;
    
    return fs->device->read(sector, (uint8_t*)buf, sectors);
}

// Write a block to the filesystem
int ext2_write_block(ext2_fs_t* fs, uint32_t block_num, const void* buf) {
    if (!fs || !fs->device || !buf) return -1;
    
    uint32_t sector = block_num * (fs->block_size / 512);
    uint32_t sectors = fs->block_size / 512;
    
    return fs->device->write(sector, (uint8_t*)buf, sectors);
}

// Convert inode number to block number
uint32_t ext2_inode_to_block(ext2_fs_t* fs, uint32_t inode_num) {
    if (!fs || inode_num == 0) return 0;
    
    uint32_t group = (inode_num - 1) / fs->inodes_per_group;
    uint32_t index = (inode_num - 1) % fs->inodes_per_group;
    
    if (group >= fs->group_count) return 0;
    
    uint32_t inode_table_block = fs->group_descriptors[group].bg_inode_table;
    uint32_t inode_block = inode_table_block + (index * fs->inode_size) / fs->block_size;
    
    return inode_block;
}

// Read an inode from disk
int ext2_read_inode(ext2_fs_t* fs, uint32_t inode_num, ext2_inode_t* inode) {
    if (!fs || !inode || inode_num == 0) return -1;
    
    uint32_t inode_block = ext2_inode_to_block(fs, inode_num);
    if (inode_block == 0) return -1;
    
    uint8_t* block_buf = (uint8_t*)rust_kmalloc(fs->block_size);
    if (!block_buf) return -1;
    
    if (ext2_read_block(fs, inode_block, block_buf) != 0) {
        rust_kfree(block_buf);
        return -1;
    }
    
    uint32_t inode_offset = ((inode_num - 1) % fs->inodes_per_group) * fs->inode_size % fs->block_size;
    memcpy(inode, block_buf + inode_offset, sizeof(ext2_inode_t));
    
    rust_kfree(block_buf);
    return 0;
}

// Write an inode to disk
int ext2_write_inode(ext2_fs_t* fs, uint32_t inode_num, const ext2_inode_t* inode) {
    if (!fs || !inode || inode_num == 0) return -1;
    
    uint32_t inode_block = ext2_inode_to_block(fs, inode_num);
    if (inode_block == 0) return -1;
    
    uint8_t* block_buf = (uint8_t*)rust_kmalloc(fs->block_size);
    if (!block_buf) return -1;
    
    if (ext2_read_block(fs, inode_block, block_buf) != 0) {
        rust_kfree(block_buf);
        return -1;
    }
    
    uint32_t inode_offset = ((inode_num - 1) % fs->inodes_per_group) * fs->inode_size % fs->block_size;
    memcpy(block_buf + inode_offset, inode, sizeof(ext2_inode_t));
    
    int result = ext2_write_block(fs, inode_block, block_buf);
    rust_kfree(block_buf);
    
    return result;
}

// Get block number from inode's block array
int ext2_get_block_number(ext2_fs_t* fs, const ext2_inode_t* inode, uint32_t block_index, uint32_t* block_num) {
    if (!fs || !inode || !block_num) return -1;
    
    if (block_index < 12) {
        // Direct blocks
        *block_num = inode->i_block[block_index];
        return 0;
    } else if (block_index < 12 + fs->block_size / 4) {
        // Single indirect block
        uint32_t indirect_block = inode->i_block[12];
        if (indirect_block == 0) {
            *block_num = 0;
            return 0;
        }
        
        uint32_t* block_array = (uint32_t*)rust_kmalloc(fs->block_size);
        if (!block_array) return -1;
        
        if (ext2_read_block(fs, indirect_block, block_array) != 0) {
            rust_kfree(block_array);
            return -1;
        }
        
        uint32_t index = block_index - 12;
        *block_num = block_array[index];
        rust_kfree(block_array);
        return 0;
    }
    
    // Double and triple indirect blocks not implemented for simplicity
    *block_num = 0;
    return -1;
}

// Allocate a new block
uint32_t ext2_alloc_block(ext2_fs_t* fs) {
    if (!fs) return 0;
    
    // Simple allocation strategy: find first free block
    // In a real implementation, this would use the block bitmap
    for (uint32_t group = 0; group < fs->group_count; group++) {
        if (fs->group_descriptors[group].bg_free_blocks_count > 0) {
            // For simplicity, we'll just return the next available block
            // In reality, this would scan the bitmap
            uint32_t block = fs->group_descriptors[group].bg_inode_table + fs->inodes_per_group;
            return block;
        }
    }
    
    return 0;
}

// Allocate a new inode
uint32_t ext2_alloc_inode(ext2_fs_t* fs) {
    if (!fs) return 0;
    
    // Simple allocation strategy: find first free inode
    for (uint32_t group = 0; group < fs->group_count; group++) {
        if (fs->group_descriptors[group].bg_free_inodes_count > 0) {
            // For simplicity, we'll just return the next available inode
            // In reality, this would scan the inode bitmap
            uint32_t inode = group * fs->inodes_per_group + 1;
            return inode;
        }
    }
    
    return 0;
}

// Path to inode resolution
int ext2_path_to_inode(ext2_fs_t* fs, const char* path, uint32_t* inode_num) {
    if (!fs || !path || !inode_num) return -1;
    
    // Start from root inode (always 2 in ext2)
    uint32_t current_inode = 2;
    
    // Skip leading slash
    if (path[0] == '/') path++;
    
    // Handle root directory
    if (path[0] == '\0') {
        *inode_num = current_inode;
        return 0;
    }
    
    // Parse path components
    char component[256];
    const char* p = path;
    
    while (*p) {
        // Extract next component
        int i = 0;
        while (*p && *p != '/' && i < 255) {
            component[i++] = *p++;
        }
        component[i] = '\0';
        
        if (i == 0) {
            if (*p == '/') p++;
            continue;
        }
        
        // Look up component in current directory
        ext2_inode_t dir_inode;
        if (ext2_read_inode(fs, current_inode, &dir_inode) != 0) {
            return -1;
        }
        
        // Check if it's a directory
        if ((dir_inode.i_mode & 0xF000) != EXT2_S_IFDIR) {
            return -1;
        }
        
        // Search directory for component
        uint8_t* dir_buf = (uint8_t*)rust_kmalloc(fs->block_size);
        if (!dir_buf) return -1;
        
        int found = 0;
        uint32_t block_index = 0;
        
        while (block_index * fs->block_size < dir_inode.i_size) {
            uint32_t block_num;
            if (ext2_get_block_number(fs, &dir_inode, block_index, &block_num) != 0) {
                rust_kfree(dir_buf);
                return -1;
            }
            
            if (block_num == 0) break;
            
            if (ext2_read_block(fs, block_num, dir_buf) != 0) {
                rust_kfree(dir_buf);
                return -1;
            }
            
            // Parse directory entries
            ext2_dir_entry_t* entry = (ext2_dir_entry_t*)dir_buf;
            uint32_t offset = 0;
            
            while (offset < fs->block_size && entry->inode != 0) {
                if (entry->inode != 0 && entry->name_len > 0) {
                    char entry_name[256];
                    memcpy(entry_name, entry->name, entry->name_len);
                    entry_name[entry->name_len] = '\0';
                    
                    if (strcmp(entry_name, component) == 0) {
                        current_inode = entry->inode;
                        found = 1;
                        break;
                    }
                }
                
                offset += entry->rec_len;
                entry = (ext2_dir_entry_t*)((uint8_t*)entry + entry->rec_len);
            }
            
            if (found) break;
            block_index++;
        }
        
        rust_kfree(dir_buf);
        
        if (!found) {
            return -1; // Component not found
        }
        
        // Skip trailing slash
        if (*p == '/') p++;
    }
    
    *inode_num = current_inode;
    return 0;
}

// Open a file
ext2_file_t* ext2_open(const char* path, int flags) {
    if (!mounted_fs || !path) return NULL;
    
    uint32_t inode_num;
    if (ext2_path_to_inode(mounted_fs, path, &inode_num) != 0) {
        return NULL;
    }
    
    ext2_file_t* file = (ext2_file_t*)rust_kmalloc(sizeof(ext2_file_t));
    if (!file) return NULL;
    
    file->fs = mounted_fs;
    file->inode_num = inode_num;
    file->position = 0;
    file->buffer = NULL;
    file->buffer_block = 0;
    file->buffer_dirty = 0;
    
    if (ext2_read_inode(mounted_fs, inode_num, &file->inode) != 0) {
        rust_kfree(file);
        return NULL;
    }
    
    file->size = file->inode.i_size;
    
    return file;
}

// Close a file
int ext2_close(ext2_file_t* file) {
    if (!file) return -1;
    
    // Flush buffer if dirty
    if (file->buffer_dirty && file->buffer) {
        ext2_write_block(file->fs, file->buffer_block, file->buffer);
        file->buffer_dirty = 0;
    }
    
    if (file->buffer) {
        rust_kfree(file->buffer);
    }
    
    rust_kfree(file);
    return 0;
}

// Read from file
int ext2_read(ext2_file_t* file, void* buf, size_t count) {
    if (!file || !buf) return -1;
    
    if (file->position >= file->size) return 0;
    
    size_t bytes_read = 0;
    uint8_t* read_buf = (uint8_t*)buf;
    
    while (bytes_read < count && file->position < file->size) {
        uint32_t block_index = file->position / file->fs->block_size;
        uint32_t block_offset = file->position % file->fs->block_size;
        uint32_t bytes_to_read = file->fs->block_size - block_offset;
        
        if (bytes_to_read > count - bytes_read) {
            bytes_to_read = count - bytes_read;
        }
        
        if (bytes_to_read > file->size - file->position) {
            bytes_to_read = file->size - file->position;
        }
        
        // Get block number
        uint32_t block_num;
        if (ext2_get_block_number(file->fs, &file->inode, block_index, &block_num) != 0) {
            return -1;
        }
        
        if (block_num == 0) {
            // Sparse file - fill with zeros
            memset(read_buf + bytes_read, 0, bytes_to_read);
        } else {
            // Read block
            uint8_t* block_buf = (uint8_t*)rust_kmalloc(file->fs->block_size);
            if (!block_buf) return -1;
            
            if (ext2_read_block(file->fs, block_num, block_buf) != 0) {
                rust_kfree(block_buf);
                return -1;
            }
            
            memcpy(read_buf + bytes_read, block_buf + block_offset, bytes_to_read);
            rust_kfree(block_buf);
        }
        
        bytes_read += bytes_to_read;
        file->position += bytes_to_read;
    }
    
    return bytes_read;
}

// Write to file
int ext2_write(ext2_file_t* file, const void* buf, size_t count) {
    if (!file || !buf) return -1;
    
    size_t bytes_written = 0;
    const uint8_t* write_buf = (const uint8_t*)buf;
    
    while (bytes_written < count) {
        uint32_t block_index = file->position / file->fs->block_size;
        uint32_t block_offset = file->position % file->fs->block_size;
        uint32_t bytes_to_write = file->fs->block_size - block_offset;
        
        if (bytes_to_write > count - bytes_written) {
            bytes_to_write = count - bytes_written;
        }
        
        // Get or allocate block
        uint32_t block_num;
        if (ext2_get_block_number(file->fs, &file->inode, block_index, &block_num) != 0) {
            return -1;
        }
        
        if (block_num == 0) {
            // Allocate new block
            block_num = ext2_alloc_block(file->fs);
            if (block_num == 0) return -1;
            
            // Update inode block array
            if (block_index < 12) {
                file->inode.i_block[block_index] = block_num;
            }
            // Single indirect block handling would go here
        }
        
        // Read block, modify, write back
        uint8_t* block_buf = (uint8_t*)rust_kmalloc(file->fs->block_size);
        if (!block_buf) return -1;
        
        if (ext2_read_block(file->fs, block_num, block_buf) != 0) {
            rust_kfree(block_buf);
            return -1;
        }
        
        memcpy(block_buf + block_offset, write_buf + bytes_written, bytes_to_write);
        
        if (ext2_write_block(file->fs, block_num, block_buf) != 0) {
            rust_kfree(block_buf);
            return -1;
        }
        
        rust_kfree(block_buf);
        
        bytes_written += bytes_to_write;
        file->position += bytes_to_write;
        
        // Update file size
        if (file->position > file->size) {
            file->size = file->position;
            file->inode.i_size = file->size;
        }
    }
    
    // Update inode
    ext2_write_inode(file->fs, file->inode_num, &file->inode);
    
    return bytes_written;
}

// Create a new file
int ext2_create_inode(const char* path, uint16_t mode) {
    if (!mounted_fs || !path) return -1;
    
    // Parse path to get parent directory and filename
    char parent_path[512];
    char filename[256];
    
    const char* last_slash = strrchr(path, '/');
    if (last_slash) {
        size_t parent_len = last_slash - path;
        if (parent_len >= sizeof(parent_path)) return -1;
        
        memcpy(parent_path, path, parent_len);
        parent_path[parent_len] = '\0';
        
        strcpy(filename, last_slash + 1);
    } else {
        strcpy(parent_path, "/");
        strcpy(filename, path);
    }
    
    // Get parent directory inode
    uint32_t parent_inode;
    if (ext2_path_to_inode(mounted_fs, parent_path, &parent_inode) != 0) {
        return -1;
    }
    
    // Read parent directory inode
    ext2_inode_t parent_dir;
    if (ext2_read_inode(mounted_fs, parent_inode, &parent_dir) != 0) {
        return -1;
    }
    
    // Check if parent is a directory
    if ((parent_dir.i_mode & 0xF000) != EXT2_S_IFDIR) {
        return -1;
    }
    
    // Allocate new inode
    uint32_t new_inode = ext2_alloc_inode(mounted_fs);
    if (new_inode == 0) return -1;
    
    // Initialize new inode
    ext2_inode_t new_inode_data = {0};
    new_inode_data.i_mode = mode;
    new_inode_data.i_uid = 0; // root
    new_inode_data.i_gid = 0; // root
    new_inode_data.i_size = 0;
    new_inode_data.i_atime = 0; // Current time would be set here
    new_inode_data.i_ctime = 0;
    new_inode_data.i_mtime = 0;
    new_inode_data.i_links_count = 1;
    new_inode_data.i_blocks = 0;
    
    // Write new inode
    if (ext2_write_inode(mounted_fs, new_inode, &new_inode_data) != 0) {
        return -1;
    }
    
    // Add directory entry to parent
    // This is a simplified version - in reality, we'd need to handle directory entry allocation
    
    return 0;
}

// VFS integration functions
int ext2_vfs_mount(const char* path, blockdev_t* device) {
    (void)path; // Mount point not used in this implementation
    
    return ext2_init(device);
}

int ext2_vfs_open(const char* path, int flags) {
    (void)flags; // Flags not used in this implementation
    
    ext2_file_t* file = ext2_open(path, flags);
    if (!file) return -1;
    
    ext2_close(file);
    return 0;
}

int ext2_vfs_read(const char* path, void* buf, int max_len) {
    ext2_file_t* file = ext2_open(path, 0);
    if (!file) return -1;
    
    int result = ext2_read(file, buf, max_len);
    ext2_close(file);
    
    return result;
}

int ext2_vfs_write(const char* path, const void* buf, int len) {
    ext2_file_t* file = ext2_open(path, 0);
    if (!file) return -1;
    
    int result = ext2_write(file, buf, len);
    ext2_close(file);
    
    return result;
}

int ext2_vfs_mkdir(const char* path) {
    return ext2_create_inode(path, EXT2_S_IFDIR | 0755);
}

int ext2_vfs_rmdir(const char* path) {
    (void)path; // Not implemented
    return -1;
}

int ext2_vfs_unlink(const char* path) {
    (void)path; // Not implemented
    return -1;
}

int ext2_vfs_stat(const char* path, struct stat* statbuf) {
    if (!statbuf) return -1;
    
    uint32_t inode_num;
    if (ext2_path_to_inode(mounted_fs, path, &inode_num) != 0) {
        return -1;
    }
    
    ext2_inode_t inode;
    if (ext2_read_inode(mounted_fs, inode_num, &inode) != 0) {
        return -1;
    }
    
    // Fill stat structure
    statbuf->st_dev = 1;
    statbuf->st_ino = inode_num;
    statbuf->st_mode = inode.i_mode;
    statbuf->st_nlink = inode.i_links_count;
    statbuf->st_uid = inode.i_uid;
    statbuf->st_gid = inode.i_gid;
    statbuf->st_size = inode.i_size;
    statbuf->st_atime = inode.i_atime;
    statbuf->st_mtime = inode.i_mtime;
    statbuf->st_ctime = inode.i_ctime;
    
    return 0;
}
