#ifndef RTL8139_H
#define RTL8139_H

#include "kernel.h"

struct mac_addr {
    uint8_t addr[6];
};

void rtl8139_init();
int rtl8139_send(const void* data, int len);
int rtl8139_poll_recv(void* buf, int maxlen);
struct mac_addr rtl8139_get_mac();

#endif 