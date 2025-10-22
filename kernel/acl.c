#include "acl.h"
#include "kernel.h"

#define ACL_MAX 128

typedef struct {
    uint8_t used;
    char path[128];
    uid_t owner;
    gid_t group;
    uint16_t mode; /* rwx rwx rwx in lowest 9 bits */
    uint32_t mac_label;
} acl_entry_t;

static acl_entry_t acl_tab[ACL_MAX];

static int acl_find(const char* path) {
    for (int i=0;i<ACL_MAX;i++) if (acl_tab[i].used && strcmp(acl_tab[i].path, path)==0) return i;
    return -1;
}

int acl_init(void) {
    for (int i=0;i<ACL_MAX;i++) acl_tab[i].used = 0;
    return 0;
}

int acl_set(const char* path, uid_t owner, gid_t group, uint16_t mode) {
    if (!path) return -1;
    int idx = acl_find(path);
    if (idx < 0) {
        for (int i=0;i<ACL_MAX;i++) if (!acl_tab[i].used) { idx = i; break; }
        if (idx < 0) return -1;
        acl_tab[idx].used = 1;
        snprintf(acl_tab[idx].path, sizeof(acl_tab[idx].path), "%s", path);
    }
    acl_tab[idx].owner = owner;
    acl_tab[idx].group = group;
    acl_tab[idx].mode = mode & 0777;
    return 0;
}

int acl_lookup(const char* path, uid_t* owner, gid_t* group, uint16_t* mode) {
    int idx = acl_find(path);
    if (idx < 0) return -1;
    if (owner) *owner = acl_tab[idx].owner;
    if (group) *group = acl_tab[idx].group;
    if (mode)  *mode  = acl_tab[idx].mode;
    return 0;
}

int acl_set_label(const char* path, uint32_t label) {
    int idx = acl_find(path);
    if (idx < 0) return -1;
    acl_tab[idx].mac_label = label;
    return 0;
}