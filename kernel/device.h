#ifndef DEVICE_H
#define DEVICE_H

#include "kernel.h"

#define MAX_DEVICES 32

typedef enum {
    DEV_CLASS_NONE = 0,
    DEV_CLASS_NET  = 1,
    DEV_CLASS_BLOCK= 2,
    DEV_CLASS_CHAR = 3,
} device_class_t;

typedef struct device {
    int id;
    device_class_t cls;
    const char *name;
    void *impl; /* class-specific */
    int in_use;
    int parent; /* index of parent device in table, -1 for root */
} device_t;

void device_framework_init(void);
int device_register(device_class_t cls, const char *name, void *impl, int parent);
int device_unregister(int id);
const device_t *device_get(int id);
int device_find_first(device_class_t cls);
void device_tree_print(void);

#endif /* DEVICE_H */
