#ifndef SECURITY_H
#define SECURITY_H

#include "kernel.h"
#include <stdint.h>

/* Simple security model: users, caps, ACL overlay, optional MAC labels */

typedef uint32_t uid_t;
typedef uint32_t gid_t;

enum {
    SEC_ACCESS_READ  = 1 << 0,
    SEC_ACCESS_WRITE = 1 << 1,
    SEC_ACCESS_EXEC  = 1 << 2
};

/* Capabilities (bitmask) */
#define CAP_DAC_OVERRIDE   (1u << 0)
#define CAP_SYS_ADMIN      (1u << 1)
#define CAP_NET_ADMIN      (1u << 2)
#define CAP_SETUID         (1u << 3)
#define CAP_MAC_OVERRIDE   (1u << 4)

typedef struct credentials {
    uid_t uid;
    gid_t gid;
    uint32_t caps;      /* capability bitmask */
    uint32_t mac_label; /* optional MAC label */
} credentials_t;

/* User database entry */
typedef struct user_record {
    uint8_t used;
    uid_t uid;
    gid_t gid;
    char username[32];
    uint64_t passhash; /* NOT CRYPTOGRAPHIC; placeholder */
    uint32_t default_caps;
} user_record_t;

void sec_init(void);

/* Credentials for current task */
credentials_t sec_get_current(void);
void sec_set_current(credentials_t cred);
uid_t sec_geteuid(void);
int sec_seteuid(uid_t uid); /* requires CAP_SETUID or root */

/* User management */
int user_add(const char* username, const char* password, uid_t* out_uid);
int user_del(const char* username);
int user_auth(const char* username, const char* password, credentials_t* out_cred);

/* Permission checks (path-based overlay) */
int sec_check_path(const char* path, int access_required);
int sec_set_acl(const char* path, uid_t owner, gid_t group, uint16_t mode);
int sec_get_acl(const char* path, uid_t* owner, gid_t* group, uint16_t* mode);

/* MAC (optional) */
void sec_mac_enable(int enable);
int  sec_mac_is_enabled(void);
int  sec_mac_set_path_label(const char* path, uint32_t label);
int  sec_mac_set_task_label(int task_id, uint32_t label);

/* Utilities */
uint64_t sec_hash_pw(const char* s); /* simple placeholder hash */

#endif