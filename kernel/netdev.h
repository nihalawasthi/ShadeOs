#ifndef NETDEV_H
#define NETDEV_H

#include "kernel.h"

#define NETDEV_MAX 4

struct net_device;
typedef int (*netdev_send_fn)(struct net_device *dev, const void *frame, int len);

typedef struct net_device {
    int id;
    char name[16];
    uint8_t mac[6];
    int mtu;
    netdev_send_fn send;
    void *driver; /* driver-private */
} net_device_t;

void netdev_init(void);
int netdev_register(const char *name, const uint8_t mac[6], int mtu, netdev_send_fn send, void *driver);
int netdev_unregister(int id);
net_device_t *netdev_get_default(void);
void netdev_set_default(int id);

#endif /* NETDEV_H */
