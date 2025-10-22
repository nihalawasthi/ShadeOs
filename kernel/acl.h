#ifndef ACL_H
#define ACL_H

#include "kernel.h"
#include <stdint.h>
#include "security.h"

int acl_init(void);
int acl_set(const char* path, uid_t owner, gid_t group, uint16_t mode);
int acl_lookup(const char* path, uid_t* owner, gid_t* group, uint16_t* mode);
int acl_set_label(const char* path, uint32_t label);

#endif