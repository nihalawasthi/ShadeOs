#include "netdev.h"
#include "serial.h"
#include "string.h"

static net_device_t netdevs[NETDEV_MAX];
static int in_use[NETDEV_MAX];
static int default_idx = -1;

void netdev_init(void) {
    for (int i=0;i<NETDEV_MAX;++i) in_use[i]=0;
    default_idx = -1;
}

int netdev_register(const char *name, const uint8_t mac[6], int mtu, netdev_send_fn send, void *driver) {
    for (int i=0;i<NETDEV_MAX;++i) {
        if (!in_use[i]) {
            in_use[i] = 1;
            netdevs[i].id = i+1;
            memset(netdevs[i].name, 0, sizeof(netdevs[i].name));
            int n=0; while (name && name[n] && n < (int)sizeof(netdevs[i].name)-1) { netdevs[i].name[n]=name[n]; n++; }
            memcpy(netdevs[i].mac, mac, 6);
            netdevs[i].mtu = mtu;
            netdevs[i].send = send;
            netdevs[i].driver = driver;
            if (default_idx == -1) default_idx = i;
            serial_write("[NETDEV] Registered "); serial_write(netdevs[i].name); serial_write("\n");
            return netdevs[i].id;
        }
    }
    return -1;
}

int netdev_unregister(int id) {
    for (int i=0;i<NETDEV_MAX;++i) {
        if (in_use[i] && netdevs[i].id == id) {
            in_use[i]=0;
            if (default_idx == i) default_idx = -1;
            return 0;
        }
    }
    return -1;
}

net_device_t *netdev_get_default(void) {
    if (default_idx >= 0 && in_use[default_idx]) return &netdevs[default_idx];
    return 0;
}

void netdev_set_default(int id) {
    for (int i=0;i<NETDEV_MAX;++i) {
        if (in_use[i] && netdevs[i].id == id) { default_idx = i; return; }
    }
}
