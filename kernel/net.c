#include "net.h"
#include "rtl8139.h"
#include "kernel.h"
#include "string.h"
#include "endian.h"
#include "arp.h"
#include "icmp.h"
#include "netdev.h"

static struct ip_addr local_ip_s;
static struct mac_addr local_mac_s;

static uint16_t ip_checksum(const void* vdata, size_t length) {
    const uint8_t* data = (const uint8_t*)vdata;
    uint32_t acc = 0;
    for (size_t i = 0; i+1 < length; i += 2) {
        uint16_t word = (uint16_t)((data[i] << 8) | data[i+1]);
        acc += word;
    }
    if (length & 1) acc += (uint32_t)data[length-1] << 8;
    while (acc >> 16) acc = (acc & 0xFFFF) + (acc >> 16);
    return (uint16_t)~acc;
}

void net_get_local_ip(uint8_t out_ip[4]) { memcpy(out_ip, local_ip_s.addr, 4); }
void net_get_local_mac(uint8_t out_mac[6]) { memcpy(out_mac, local_mac_s.addr, 6); }

/* Provided by timer */
extern uint64_t timer_get_ticks(void);

void net_init(struct ip_addr ip) {
    local_ip_s = ip;
    struct mac_addr m;
    net_device_t *nd = netdev_get_default();
    if (nd) {
        memcpy(m.addr, nd->mac, 6);
    } else {
        m = rtl8139_get_mac();
    }
    local_mac_s = m;
}

int net_send_eth_frame(const uint8_t dst_mac[6], uint16_t ethertype, const void *payload, int len) {
    uint8_t frame[14 + 1514];
    if (len > 1514) return -1;
    /* Ethernet header */
    memcpy(frame + 0, dst_mac, 6);
    memcpy(frame + 6, local_mac_s.addr, 6);
    frame[12] = (uint8_t)(ethertype >> 8);
    frame[13] = (uint8_t)(ethertype & 0xFF);
    memcpy(frame + 14, payload, len);
    net_device_t *nd = netdev_get_default();
    if (nd && nd->send) return nd->send(nd, frame, 14+len);
    return rtl8139_send(frame, 14 + len);
}

int net_ipv4_send(const uint8_t dst_ip[4], uint8_t proto, const void *payload, int payload_len) {
    /* Resolve MAC via ARP table */
    uint8_t mac[6];
    if (arp_resolve_sync(dst_ip, mac) != 0) {
        /* ARP request sent, drop for now */
        return -1;
    }
    /* Build IPv4 header + payload */
    uint8_t buf[20 + 1500];
    if (payload_len > 1500) return -1;
    int ip_off = 0;
    buf[ip_off+0] = 0x45; /* Version/IHL */
    buf[ip_off+1] = 0x00; /* DSCP/ECN */
    uint16_t total_len = (uint16_t)(20 + payload_len);
    buf[ip_off+2] = (uint8_t)(total_len >> 8); buf[ip_off+3] = (uint8_t)(total_len & 0xFF);
    buf[ip_off+4] = 0; buf[ip_off+5] = 0; /* ID */
    buf[ip_off+6] = 0x40; buf[ip_off+7] = 0; /* Flags/frag */
    buf[ip_off+8] = 64; /* TTL */
    buf[ip_off+9] = proto; /* Protocol */
    buf[ip_off+10] = buf[ip_off+11] = 0; /* checksum */
    memcpy(buf+ip_off+12, local_ip_s.addr, 4);
    memcpy(buf+ip_off+16, dst_ip, 4);
    uint16_t csum = ip_checksum(buf+ip_off, 20);
    buf[ip_off+10] = (uint8_t)(csum >> 8); buf[ip_off+11] = (uint8_t)(csum & 0xFF);
    memcpy(buf+20, payload, payload_len);
    return net_send_eth_frame(mac, 0x0800, buf, 20 + payload_len);
}

/* Simple UDP receive queue */
#define UDP_Q_CAP 8
static struct {
    uint8_t src_ip[4];
    uint16_t src_port;
    int len;
    uint8_t data[512];
    int in_use;
} udp_q[UDP_Q_CAP];

static void udp_q_push(const uint8_t src_ip[4], uint16_t src_port, const uint8_t *data, int len) {
    for (int i=0; i<UDP_Q_CAP; ++i) {
        if (!udp_q[i].in_use) {
            udp_q[i].in_use = 1;
            memcpy(udp_q[i].src_ip, src_ip, 4);
            udp_q[i].src_port = src_port;
            if (len > (int)sizeof(udp_q[i].data)) len = (int)sizeof(udp_q[i].data);
            udp_q[i].len = len;
            memcpy(udp_q[i].data, data, len);
            return;
        }
    }
}

static int udp_q_pop(struct ip_addr *src, uint16_t *port, void *buf, int maxlen) {
    for (int i=0; i<UDP_Q_CAP; ++i) {
        if (udp_q[i].in_use) {
            if (src) memcpy(src->addr, udp_q[i].src_ip, 4);
            if (port) *port = udp_q[i].src_port;
            int n = udp_q[i].len;
            if (n > maxlen) n = maxlen;
            memcpy(buf, udp_q[i].data, n);
            udp_q[i].in_use = 0;
            return n;
        }
    }
    return 0;
}

int udp_send(struct ip_addr dest, uint16_t port, const void* data, int len) {
    /* Build UDP over IPv4 */
    uint8_t pkt[8 + 1500];
    if (len > 1500) return -1;
    uint8_t *p = pkt;
    /* src port: fixed 0x1234 for now */
    p[0] = 0x12; p[1] = 0x34;
    p[2] = (uint8_t)(port >> 8); p[3] = (uint8_t)(port & 0xFF);
    uint16_t ulen = (uint16_t)(8 + len);
    p[4] = (uint8_t)(ulen >> 8); p[5] = (uint8_t)(ulen & 0xFF);
    p[6] = p[7] = 0; /* checksum skipped */
    memcpy(pkt+8, data, len);
    return net_ipv4_send(dest.addr, 17, pkt, 8 + len);
}

int udp_poll_recv(struct ip_addr* src, uint16_t* port, void* buf, int maxlen) {
    return udp_q_pop(src, port, buf, maxlen);
}

/* RX entry point from NIC driver */
void net_input_eth_frame(const uint8_t *frame, int len) {
    if (len < 14) return;
    uint16_t ethertype = (uint16_t)((frame[12] << 8) | frame[13]);
    const uint8_t *payload = frame + 14;
    int payload_len = len - 14;
    if (ethertype == 0x0806) { /* ARP */
        arp_handle_frame(payload, payload_len);
        return;
    } else if (ethertype == 0x0800) { /* IPv4 */
        if (payload_len < 20) return;
        uint8_t ihl = payload[0] & 0x0f;
        int ip_hdr_len = ihl * 4;
        if (payload_len < ip_hdr_len) return;
        uint8_t proto = payload[9];
        const uint8_t *src_ip = payload + 12;
        const uint8_t *dst_ip = payload + 16;
        const uint8_t *ip_payload = payload + ip_hdr_len;
        int ip_payload_len = payload_len - ip_hdr_len;
        if (proto == 1) { /* ICMP */
            icmp_handle_ipv4(src_ip, ip_payload, ip_payload_len);
        } else if (proto == 17) { /* UDP */
            if (ip_payload_len >= 8) {
                uint16_t src_port = (uint16_t)((ip_payload[0] << 8) | ip_payload[1]);
                const uint8_t *udp_data = ip_payload + 8;
                int udp_len = ((ip_payload[4] << 8) | ip_payload[5]) - 8;
                if (udp_len > ip_payload_len - 8) udp_len = ip_payload_len - 8;
                udp_q_push(src_ip, src_port, udp_data, udp_len);
            }
        } else if (proto == 6) {
            /* TCP: handled in tcp.c if present */
            extern void tcp_input_ipv4(const uint8_t *src_ip, const uint8_t *seg, int len);
            tcp_input_ipv4(src_ip, ip_payload, ip_payload_len);
        }
    }
}
