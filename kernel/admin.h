#ifndef ADMIN_H
#define ADMIN_H

#include "kernel.h"
#include "security.h"

int admin_adduser(const char* username, const char* password);
int admin_deluser(const char* username);

#endif