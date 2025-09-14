#include "pkg.h"
#include "vfs.h"
#include "heap.h"
#include "kernel.h"
#include "string.h"

static vfs_node_t* pkgs_dir = 0;

void pkg_init() {
    vfs_node_t* root = vfs_get_root();
    pkgs_dir = vfs_find("pkgs", root);
    if (!pkgs_dir) pkgs_dir = vfs_create("pkgs", VFS_TYPE_DIR, root);
}

int pkg_install(const char* name, const char* data, int size) {
    if (!pkgs_dir) return -1;
    if (vfs_find(name, pkgs_dir)) return -1; // already exists
    vfs_node_t* node = vfs_create(name, VFS_TYPE_FILE, pkgs_dir);
    if (!node) return -1;
    node->size = 0;
    vfs_write(node, data, size);
    return 0;
}

int pkg_remove(const char* name) {
    if (!pkgs_dir) return -1;
    vfs_node_t* node = vfs_find(name, pkgs_dir);
    if (!node) return -1;
    node->used = 0;
    return 0;
}

void pkg_list() {
    if (!pkgs_dir) return;
    vfs_list(pkgs_dir);
}

void pkg_info(const char* name) {
    if (!pkgs_dir) return;
    vfs_node_t* node = vfs_find(name, pkgs_dir);
    if (!node) {
        vga_print("pkg: not found\n");
        return;
    }
    char buf[128] = {0};
    int n = vfs_read(node, buf, 127);
    if (n > 0) vga_print(buf);
    vga_print("\n");
}
