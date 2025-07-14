#include "vfs.h"
#include <stddef.h>

vfs_node_t* vfs_get_root() { return NULL; }
vfs_node_t* vfs_find(const char* path, vfs_node_t* cwd) { (void)path; (void)cwd; return NULL; }
vfs_node_t* vfs_create(const char* name, int type, vfs_node_t* parent) { (void)name; (void)type; (void)parent; return NULL; }
int vfs_write(vfs_node_t* node, const void* buf, int size) { (void)node; (void)buf; (void)size; return -1; }
int vfs_read(vfs_node_t* node, void* buf, int size) { (void)node; (void)buf; (void)size; return -1; }
void vfs_list(vfs_node_t* dir) { (void)dir; } 