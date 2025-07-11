#ifndef NET_H
#define NET_H

#include "kernel.h"

struct ip_addr {
    uint8_t addr[4];
};

void net_init(struct ip_addr local_ip);
int udp_send(struct ip_addr dest, uint16_t port, const void* data, int len);
int udp_poll_recv(struct ip_addr* src, uint16_t* port, void* buf, int maxlen);

#endif 