#include "vfs.h"
#include "heap.h"
#include "kernel.h"
#include "string.h"
#include "fat.h"

static vfs_node_t nodes[MAX_FILES];
static vfs_node_t* root = 0;

vfs_node_t* vfs_get_root() { return root; }

void vfs_init() {
    for (int i = 0; i < MAX_FILES; i++) {
        nodes[i].used = 0;
        nodes[i].data = NULL;
        nodes[i].parent = nodes[i].child = nodes[i].sibling = NULL;
    }
    root = &nodes[0];
    root->used = 1;
    root->type = VFS_TYPE_DIR;
    root->size = 0;
    root->pos = 0;
    root->parent = NULL;
    root->child = NULL;
    root->sibling = NULL;
    root->name[0] = '/'; root->name[1] = 0;
}

vfs_node_t* vfs_create(const char* name, int type, vfs_node_t* parent) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (!nodes[i].used) {
            nodes[i].used = 1;
            nodes[i].type = type;
            nodes[i].size = 0;
            nodes[i].pos = 0;
            nodes[i].data = (type == VFS_TYPE_MEM) ? kmalloc(MAX_FILE_SIZE) : NULL;
            nodes[i].parent = parent;
            nodes[i].child = NULL;
            nodes[i].sibling = NULL;
            for (int j = 0; j < MAX_FILE_NAME; j++) nodes[i].name[j] = 0;
            for (int j = 0; name[j] && j < MAX_FILE_NAME-1; j++) nodes[i].name[j] = name[j];
            nodes[i].fat_filename[0] = 0;
            // FAT16 support: if creating in root, also create in FAT
            if (parent == root && type == VFS_TYPE_MEM) {
                if (fat_create(name) == 0) {
                    nodes[i].type = VFS_TYPE_FAT;
                    // Store 8.3 name for FAT
                    int n = 0, k = 0;
                    while (name[k] && name[k] != '.' && n < 8) nodes[i].fat_filename[n++] = (name[k] >= 'a' && name[k] <= 'z') ? name[k]-32 : name[k++];
                    while (n < 8) nodes[i].fat_filename[n++] = ' ';
                    if (name[k] == '.') k++;
                    int e = 0;
                    while (name[k] && e < 3) nodes[i].fat_filename[8+e++] = (name[k] >= 'a' && name[k] <= 'z') ? name[k]-32 : name[k++];
                    while (e < 3) nodes[i].fat_filename[8+e++] = ' ';
                    nodes[i].fat_filename[11] = 0;
                }
            }
            // Insert as child of parent
            if (parent) {
                if (!parent->child) parent->child = &nodes[i];
                else {
                    vfs_node_t* sib = parent->child;
                    while (sib->sibling) sib = sib->sibling;
                    sib->sibling = &nodes[i];
                }
            }
            return &nodes[i];
        }
    }
    return NULL;
}

// Find node by path (absolute or relative to cwd)
vfs_node_t* vfs_find(const char* path, vfs_node_t* cwd) {
    if (!path || !*path) return NULL;
    vfs_node_t* cur = (path[0] == '/') ? root : cwd;
    int i = (path[0] == '/') ? 1 : 0;
    char part[MAX_FILE_NAME];
    while (path[i]) {
        int j = 0;
        while (path[i] && path[i] != '/') part[j++] = path[i++];
        part[j] = 0;
        if (path[i] == '/') i++;
        if (!part[0]) continue;
        vfs_node_t* child = cur->child;
        while (child && strcmp(child->name, part)) child = child->sibling;
        if (!child) return NULL;
        cur = child;
    }
    return cur;
}

void vfs_list(vfs_node_t* dir) {
    if (!dir || dir->type != VFS_TYPE_DIR) return;
    vfs_node_t* child = dir->child;
    while (child) {
        vga_print(child->name);
        if (child->type == VFS_TYPE_DIR) vga_print("/");
        vga_print("  ");
        child = child->sibling;
    }
    vga_print("\n");
}

int vfs_read(vfs_node_t* node, void* buf, int size) {
    if (!node) return -1;
    if (node->type == VFS_TYPE_MEM) {
        int to_read = size;
        if (node->pos + to_read > node->size) to_read = node->size - node->pos;
        if (to_read <= 0) return 0;
        memcpy(buf, (uint8_t*)node->data + node->pos, to_read);
        node->pos += to_read;
        return to_read;
    } else if (node->type == VFS_TYPE_FAT) {
        // Use fat_read
        return fat_read(node->name, buf, size);
    }
    return -1;
}

int vfs_write(vfs_node_t* node, const void* buf, int size) {
    if (!node) return -1;
    if (node->type == VFS_TYPE_MEM) {
        int to_write = size;
        if (node->pos + to_write > MAX_FILE_SIZE) to_write = MAX_FILE_SIZE - node->pos;
        if (to_write <= 0) return 0;
        memcpy((uint8_t*)node->data + node->pos, buf, to_write);
        node->pos += to_write;
        if (node->pos > node->size) node->size = node->pos;
        return to_write;
    } else if (node->type == VFS_TYPE_FAT) {
        // Use fat_write
        return fat_write(node->name, buf, size);
    }
    return -1;
} 