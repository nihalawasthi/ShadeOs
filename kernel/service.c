#include "service.h"
#include "task.h"
#include "kernel.h"

#define MAX_SERVICES 16

typedef struct {
    uint8_t used;
    char name[32];
    service_entry_t entry;
    int restart;
    int task_id;
} svc_t;

static svc_t services[MAX_SERVICES];

int svc_init(void) {
    for (int i=0;i<MAX_SERVICES;i++) services[i].used=0;
    return 0;
}

static int svc_find(const char* name) {
    for (int i=0;i<MAX_SERVICES;i++) if (services[i].used && strcmp(services[i].name, name)==0) return i;
    return -1;
}

int svc_register(const char* name, service_entry_t entry, int restart_on_exit) {
    for (int i=0;i<MAX_SERVICES;i++) if (!services[i].used) {
        services[i].used=1; snprintf(services[i].name, sizeof(services[i].name), "%s", name);
        services[i].entry = entry; services[i].restart = restart_on_exit; services[i].task_id=-1; return 0;
    }
    return -1;
}

static void svc_wrapper(void) {
    /* Find our entry by matching current task id */
    int tid = current ? current->id : -1;
    for (int i=0;i<MAX_SERVICES;i++) if (services[i].used && services[i].task_id==tid) {
        services[i].entry();
        break;
    }
    /* Exit when done */
    task_exit();
}

int svc_start(const char* name) {
    int idx = svc_find(name); if (idx<0) return -1;
    int tid = task_create(svc_wrapper);
    if (tid < 0) return -1;
    services[idx].task_id = tid;
    return 0;
}

int svc_stop(const char* name) {
    int idx = svc_find(name); if (idx<0) return -1;
    /* For now, no signal/kill mechanism; return -1 to indicate unsupported */
    return -1;
}