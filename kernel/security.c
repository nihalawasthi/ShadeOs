#include "security.h"
#include "task.h"
#include "kernel.h"
#include "acl.h"

#define MAX_USERS 16
static user_record_t users[MAX_USERS];
static int mac_enforce = 0;

/* Map task id -> creds; fallback global for boot context */
typedef struct { uint8_t used; int task_id; credentials_t cred; } cred_slot_t;
static cred_slot_t cred_map[MAX_TASKS];
static credentials_t boot_cred;

static int find_user(const char* username) {
    for (int i=0;i<MAX_USERS;i++) if (users[i].used && strcmp(users[i].username, username)==0) return i;
    return -1;
}

void sec_init(void) {
    for (int i=0;i<MAX_USERS;i++) users[i].used = 0;
    for (int i=0;i<MAX_TASKS;i++) cred_map[i].used = 0;

    /* root user */
    users[0].used = 1;
    users[0].uid = 0; users[0].gid = 0;
    snprintf(users[0].username, sizeof(users[0].username), "root");
    users[0].passhash = sec_hash_pw("root");
    users[0].default_caps = 0xFFFFFFFFu; /* all caps for root */

    /* boot credentials: run as root */
    boot_cred.uid = 0; boot_cred.gid = 0; boot_cred.caps = 0xFFFFFFFFu; boot_cred.mac_label = 0;
}

credentials_t sec_get_current(void) {
    if (current) {
        int tid = current->id;
        for (int i=0;i<MAX_TASKS;i++) {
            if (cred_map[i].used && cred_map[i].task_id == tid) return cred_map[i].cred;
        }
    }
    return boot_cred;
}

void sec_set_current(credentials_t cred) {
    if (current) {
        int tid = current->id;
        int free_i = -1;
        for (int i=0;i<MAX_TASKS;i++) {
            if (cred_map[i].used && cred_map[i].task_id == tid) { cred_map[i].cred = cred; return; }
            if (!cred_map[i].used && free_i==-1) free_i = i;
        }
        if (free_i != -1) { cred_map[free_i].used=1; cred_map[free_i].task_id=tid; cred_map[free_i].cred=cred; return; }
    }
    boot_cred = cred;
}

uid_t sec_geteuid(void) { return sec_get_current().uid; }

int sec_seteuid(uid_t uid) {
    credentials_t c = sec_get_current();
    if (c.uid == 0 || (c.caps & CAP_SETUID)) {
        c.uid = uid;
        sec_set_current(c);
        return 0;
    }
    return -1;
}

int user_add(const char* username, const char* password, uid_t* out_uid) {
    credentials_t c = sec_get_current();
    if (!(c.uid==0 || (c.caps & CAP_SYS_ADMIN))) return -1;
    if (!username || !password) return -1;
    if (find_user(username) >= 0) return -1;
    for (int i=0;i<MAX_USERS;i++) {
        if (!users[i].used) {
            users[i].used=1;
            users[i].uid = (i==0)?0:(1000+i);
            users[i].gid = users[i].uid;
            snprintf(users[i].username, sizeof(users[i].username), "%s", username);
            users[i].passhash = sec_hash_pw(password);
            users[i].default_caps = 0; /* regular user */
            if (out_uid) *out_uid = users[i].uid;
            return 0;
        }
    }
    return -1;
}

int user_del(const char* username) {
    credentials_t c = sec_get_current();
    if (!(c.uid==0 || (c.caps & CAP_SYS_ADMIN))) return -1;
    int idx = find_user(username);
    if (idx <= 0) return -1; /* disallow deleting root or non-existent */
    users[idx].used=0;
    return 0;
}

int user_auth(const char* username, const char* password, credentials_t* out_cred) {
    int idx = find_user(username);
    if (idx < 0) return -1;
    if (users[idx].passhash != sec_hash_pw(password)) return -1;
    credentials_t cred; cred.uid=users[idx].uid; cred.gid=users[idx].gid; cred.caps=users[idx].default_caps; cred.mac_label=0;
    if (out_cred) *out_cred = cred;
    return 0;
}

/* ACL overlay wrappers */
int sec_check_path(const char* path, int access_required) {
    uid_t owner; gid_t group; uint16_t mode;
    if (acl_lookup(path, &owner, &group, &mode) != 0) {
        /* No explicit ACL: allow for now */
        return 0;
    }
    credentials_t c = sec_get_current();
    if (c.uid == 0 || (c.caps & CAP_DAC_OVERRIDE)) return 0;

    int shift = (c.uid == owner) ? 6 : 0; /* simplistic: owner vs others */
    /* No group handling in this minimal pass */
    if ((access_required & SEC_ACCESS_READ)  && !((mode >> (shift+2)) & 1)) return -1;
    if ((access_required & SEC_ACCESS_WRITE) && !((mode >> (shift+1)) & 1)) return -1;
    if ((access_required & SEC_ACCESS_EXEC)  && !((mode >> (shift+0)) & 1)) return -1;

    if (mac_enforce) {
        uint32_t plabel = 0; uint32_t tlabel = c.mac_label;
        (void)plabel; (void)tlabel; /* extend: pull path label from ACL entry; require match */
        /* if (plabel && plabel != tlabel && !(c.caps & CAP_MAC_OVERRIDE)) return -1; */
    }
    return 0;
}

int sec_set_acl(const char* path, uid_t owner, gid_t group, uint16_t mode) {
    return acl_set(path, owner, group, mode);
}

int sec_get_acl(const char* path, uid_t* owner, gid_t* group, uint16_t* mode) {
    return acl_lookup(path, owner, group, mode);
}

void sec_mac_enable(int enable) { mac_enforce = enable ? 1 : 0; }
int  sec_mac_is_enabled(void) { return mac_enforce; }
int  sec_mac_set_path_label(const char* path, uint32_t label) { return acl_set_label(path, label); }
int  sec_mac_set_task_label(int task_id, uint32_t label) {
    for (int i=0;i<MAX_TASKS;i++) if (cred_map[i].used && cred_map[i].task_id == task_id) { cred_map[i].cred.mac_label = label; return 0; }
    return -1;
}

/* FNV-1a 64-bit (placeholder) */
uint64_t sec_hash_pw(const char* s) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i=0; s && s[i]; ++i) { h ^= (uint8_t)s[i]; h *= 1099511628211ull; }
    return h;
}