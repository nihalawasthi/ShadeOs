#include "device.h"
#include "serial.h"
#include "vga.h"

static device_t dev_tbl[MAX_DEVICES];
static int next_id = 1;

void device_framework_init(void) {
    for (int i = 0; i < MAX_DEVICES; ++i) dev_tbl[i].in_use = 0;
    next_id = 1;
}

int device_register(device_class_t cls, const char *name, void *impl, int parent) {
    for (int i = 0; i < MAX_DEVICES; ++i) {
        if (!dev_tbl[i].in_use) {
            dev_tbl[i].in_use = 1;
            dev_tbl[i].id = next_id++;
            dev_tbl[i].cls = cls;
            dev_tbl[i].name = name;
            dev_tbl[i].impl = impl;
            dev_tbl[i].parent = parent;
            return dev_tbl[i].id;
        }
    }
    return -1;
}

int device_unregister(int id) {
    for (int i = 0; i < MAX_DEVICES; ++i) {
        if (dev_tbl[i].in_use && dev_tbl[i].id == id) {
            dev_tbl[i].in_use = 0;
            dev_tbl[i].impl = 0;
            return 0;
        }
    }
    return -1;
}

const device_t *device_get(int id) {
    for (int i = 0; i < MAX_DEVICES; ++i) {
        if (dev_tbl[i].in_use && dev_tbl[i].id == id) return &dev_tbl[i];
    }
    return 0;
}

int device_find_first(device_class_t cls) {
    for (int i = 0; i < MAX_DEVICES; ++i) {
        if (dev_tbl[i].in_use && dev_tbl[i].cls == cls) return dev_tbl[i].id;
    }
    return -1;
}

void device_tree_print(void) {
    serial_write("[DEV] Device tree:\n");
    for (int i = 0; i < MAX_DEVICES; ++i) {
        if (!dev_tbl[i].in_use) continue;
        serial_write("  - "); serial_write(dev_tbl[i].name); serial_write(" (id=");
        unsigned long id = dev_tbl[i].id; char buf[21]; int j=20; buf[j--]=0; if(!id)buf[j--]='0';
        while (id) { buf[j--] = '0' + (id % 10); id/=10; }
        serial_write(&buf[j+1]); serial_write(") class=");
        switch (dev_tbl[i].cls) {
            case DEV_CLASS_NET: serial_write("net\n"); break;
            case DEV_CLASS_BLOCK: serial_write("block\n"); break;
            case DEV_CLASS_CHAR: serial_write("char\n"); break;
            default: serial_write("unknown\n"); break;
        }
    }
}
