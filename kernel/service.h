#ifndef SERVICE_H
#define SERVICE_H

#include "kernel.h"

typedef void (*service_entry_t)(void);

int svc_init(void);
int svc_register(const char* name, service_entry_t entry, int restart_on_exit);
int svc_start(const char* name);
int svc_stop(const char* name);

#endif