#ifndef PKG_H
#define PKG_H

#include "kernel.h"

void pkg_init();
int pkg_install(const char* name, const char* data, int size);
int pkg_remove(const char* name);
void pkg_list();
void pkg_info(const char* name);

#endif 