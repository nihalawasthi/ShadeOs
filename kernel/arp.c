#include "arp.h"
#include "endian.h"
#include "net.h"
#include "kernel.h"
#include "serial.h"
#include <stdint.h>

/* Replace these with your kernel's time and send helpers */
extern uint64_t kernel_uptime_ms(void);
/* net_send_eth_frame provided by net.c */

struct __attribute__((packed)) arp_pkt {
    uint16_t htype;
    uint16_t ptype;
    uint8_t hlen;
    uint8_t plen;
    uint16_t oper;
    uint8_t sha[6];
    uint8_t spa[4];
    uint8_t tha[6];
    uint8_t tpa[4];
};

static arp_entry_t arp_table[ARP_TABLE_SIZE];

void arp_init(void) {
    memset(arp_table, 0, sizeof(arp_table));
}

static void arp_table_insert(const uint8_t ip[4], const uint8_t mac[6]) {
    int i;
    for (i = 0; i < ARP_TABLE_SIZE; ++i) {
        if (arp_table[i].valid && memcmp(arp_table[i].ip, ip, 4) == 0) {
            memcpy(arp_table[i].mac, mac, 6);
            arp_table[i].last_seen = kernel_uptime_ms();
            return;
        }
    }
    for (i = 0; i < ARP_TABLE_SIZE; ++i) {
        if (!arp_table[i].valid) {
            arp_table[i].valid = 1;
            memcpy(arp_table[i].ip, ip, 4);
            memcpy(arp_table[i].mac, mac, 6);
            arp_table[i].last_seen = kernel_uptime_ms();
            return;
        }
    }
    /* simple eviction: replace oldest */
    uint64_t oldest = (uint64_t)-1;
    int oldest_idx = 0;
    for (i = 0; i < ARP_TABLE_SIZE; ++i) {
        if (arp_table[i].last_seen < oldest) {
            oldest = arp_table[i].last_seen;
            oldest_idx = i;
        }
    }
    memcpy(arp_table[oldest_idx].ip, ip, 4);
    memcpy(arp_table[oldest_idx].mac, mac, 6);
    arp_table[oldest_idx].last_seen = kernel_uptime_ms();
    arp_table[oldest_idx].valid = 1;
}

int arp_resolve_sync(const uint8_t ip[4], uint8_t out_mac[6]) {
    // Hardcoded ARP entry for QEMU default gateway 10.0.2.2
    if (ip[0] == 10 && ip[1] == 0 && ip[2] == 2 && ip[3] == 2) {
        serial_write("[ARP] Hardcoded MAC for 10.0.2.2\n");
        out_mac[0] = 0x52;
        out_mac[1] = 0x54;
        out_mac[2] = 0x00;
        out_mac[3] = 0x12;
        out_mac[4] = 0x34;
        out_mac[5] = 0x56;
        return 0;
    }
    int i;
    for (i = 0; i < ARP_TABLE_SIZE; ++i) {
        if (arp_table[i].valid && memcmp(arp_table[i].ip, ip, 4) == 0) {
            serial_write("[ARP] Table hit for IP: ");
            for (int j = 0; j < 4; ++j) serial_write_dec("", ip[j]), serial_write(j < 3 ? "." : "\n");
            memcpy(out_mac, arp_table[i].mac, 6);
            return 0;
        }
    }
    /* Send an ARP request and return -1 (non-blocking/simple implementation). */
    struct arp_pkt req;
    req.htype = htons(ARP_HW_ETH);
    req.ptype = htons(ARP_PROTO_IPV4);
    req.hlen = 6;
    req.plen = 4;
    req.oper = htons(ARP_OP_REQUEST);
    uint8_t lmac[6]; uint8_t lip[4];
    net_get_local_mac(lmac);
    net_get_local_ip(lip);
    memcpy(req.sha, lmac, 6);
    memcpy(req.spa, lip, 4);
    memset(req.tha, 0, 6);
    memcpy(req.tpa, ip, 4);
    /* Broadcast */
    uint8_t bcast[6] = {0xff,0xff,0xff,0xff,0xff,0xff};
    serial_write("[ARP] Sending ARP request for IP: ");
    for (int j = 0; j < 4; ++j) serial_write_dec("", ip[j]), serial_write(j < 3 ? "." : "\n");
    net_send_eth_frame(bcast, 0x0806, &req, sizeof(req));
    return -1;
}

void arp_handle_frame(const uint8_t *frame, int len) {
    if (len < (int)sizeof(struct arp_pkt)) return;
    struct arp_pkt pkt;
    memcpy(&pkt, frame, sizeof(pkt));
    if (ntohs(pkt.htype) != ARP_HW_ETH) return;
    if (ntohs(pkt.ptype) != ARP_PROTO_IPV4) return;
    if (pkt.hlen != 6 || pkt.plen != 4) return;
    uint16_t op = ntohs(pkt.oper);
    /* Insert sender into ARP table */
    arp_table_insert(pkt.spa, pkt.sha);
    if (op == ARP_OP_REQUEST) {
        /* if target IP is ours, reply */
        uint8_t lip[4];
        net_get_local_ip(lip);
        if (memcmp(pkt.tpa, lip, 4) == 0) {
            struct arp_pkt reply;
            reply.htype = htons(ARP_HW_ETH);
            reply.ptype = htons(ARP_PROTO_IPV4);
            reply.hlen = 6;
            reply.plen = 4;
            reply.oper = htons(ARP_OP_REPLY);
            uint8_t lmac[6]; net_get_local_mac(lmac);
            memcpy(reply.sha, lmac, 6);
            memcpy(reply.spa, lip, 4);
            memcpy(reply.tha, pkt.sha, 6);
            memcpy(reply.tpa, pkt.spa, 4);
            net_send_eth_frame(pkt.sha, 0x0806, &reply, sizeof(reply));
        }
    }
    /* replies handled by inserted table above */
}

void arp_periodic(void) {
    uint64_t now = kernel_uptime_ms();
    for (int i = 0; i < ARP_TABLE_SIZE; ++i) {
        if (!arp_table[i].valid) continue;
        if (now - arp_table[i].last_seen > 5 * 60 * 1000) { /* 5min */
            arp_table[i].valid = 0;
        }
    }
}
