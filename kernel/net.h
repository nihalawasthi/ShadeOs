#ifndef NET_H
#define NET_H

#include "kernel.h"

struct ip_addr { uint8_t addr[4]; };
struct mac_addr { uint8_t addr[6]; };

void net_init(struct ip_addr local_ip);

/* Local addresses */
void net_get_local_ip(uint8_t out_ip[4]);
void net_get_local_mac(uint8_t out_mac[6]);

/* Ethernet */
int net_send_eth_frame(const uint8_t dst_mac[6], uint16_t ethertype, const void *payload, int len);
void net_input_eth_frame(const uint8_t *frame, int len);

/* IPv4 */
int net_ipv4_send(const uint8_t dst_ip[4], uint8_t proto, const void *payload, int payload_len);

/* UDP */
int udp_send(struct ip_addr dest, uint16_t port, const void* data, int len);
int udp_poll_recv(struct ip_addr* src, uint16_t* port, void* buf, int maxlen);

/* Periodic NIC RX poller */
void net_poll_rx(void);

#endif
