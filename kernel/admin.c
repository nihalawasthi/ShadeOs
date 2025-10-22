#include "admin.h"

int admin_adduser(const char* username, const char* password) {
    /* Requires CAP_SYS_ADMIN */
    credentials_t c = sec_get_current();
    if (!(c.uid==0 || (c.caps & CAP_SYS_ADMIN))) return -1;
    return user_add(username, password, 0);
}

int admin_deluser(const char* username) {
    credentials_t c = sec_get_current();
    if (!(c.uid==0 || (c.caps & CAP_SYS_ADMIN))) return -1;
    return user_del(username);
}