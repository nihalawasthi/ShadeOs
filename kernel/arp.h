#ifndef KERNEL_ARP_H
#define KERNEL_ARP_H

#include <stdint.h>

#define ARP_HW_ETH 1
#define ARP_PROTO_IPV4 0x0800
#define ARP_OP_REQUEST 1
#define ARP_OP_REPLY 2

#define ARP_TABLE_SIZE 64

typedef struct {
    uint8_t ip[4];
    uint8_t mac[6];
    uint64_t last_seen; /* uptime or jiffies */
    int valid;
} arp_entry_t;

void arp_init(void);
void arp_handle_frame(const uint8_t *frame, int len); /* called from eth rx path */
int arp_resolve_sync(const uint8_t ip[4], uint8_t out_mac[6]); /* non-blocking: sends request when missing */
void arp_periodic(void); /* call from timer/loop for aging */

#endif /* KERNEL_ARP_H */
