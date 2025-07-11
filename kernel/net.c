#include "net.h"
#include "rtl8139.h"
#include "kernel.h"
#include "string.h"

static struct ip_addr local_ip;
static struct mac_addr local_mac;
static struct mac_addr gw_mac = {{0xff,0xff,0xff,0xff,0xff,0xff}}; // Broadcast for demo

static uint16_t ip_checksum(const void* vdata, size_t length) {
    const uint8_t* data = vdata;
    uint32_t acc = 0;
    for (size_t i = 0; i+1 < length; i += 2) {
        uint16_t word = (data[i] << 8) | data[i+1];
        acc += word;
    }
    if (length & 1) acc += data[length-1] << 8;
    while (acc >> 16) acc = (acc & 0xFFFF) + (acc >> 16);
    return ~acc;
}

void net_init(struct ip_addr ip) {
    local_ip = ip;
    local_mac = rtl8139_get_mac();
}

int udp_send(struct ip_addr dest, uint16_t port, const void* data, int len) {
    uint8_t pkt[1514] = {0};
    // Ethernet
    memcpy(pkt, gw_mac.addr, 6);
    memcpy(pkt+6, local_mac.addr, 6);
    pkt[12] = 0x08; pkt[13] = 0x00; // IPv4
    // IPv4
    int ip_off = 14;
    pkt[ip_off+0] = 0x45; // Version/IHL
    pkt[ip_off+1] = 0x00; // DSCP/ECN
    uint16_t total_len = 20+8+len;
    pkt[ip_off+2] = total_len >> 8; pkt[ip_off+3] = total_len & 0xFF;
    pkt[ip_off+4] = 0; pkt[ip_off+5] = 0; // ID
    pkt[ip_off+6] = 0x40; pkt[ip_off+7] = 0; // Flags/frag
    pkt[ip_off+8] = 64; // TTL
    pkt[ip_off+9] = 17; // UDP
    pkt[ip_off+10] = pkt[ip_off+11] = 0; // Checksum (to fill)
    memcpy(pkt+ip_off+12, local_ip.addr, 4);
    memcpy(pkt+ip_off+16, dest.addr, 4);
    uint16_t csum = ip_checksum(pkt+ip_off, 20);
    pkt[ip_off+10] = csum >> 8; pkt[ip_off+11] = csum & 0xFF;
    // UDP
    int udp_off = ip_off+20;
    pkt[udp_off+0] = 0x12; pkt[udp_off+1] = 0x34; // Src port 0x1234
    pkt[udp_off+2] = port >> 8; pkt[udp_off+3] = port & 0xFF;
    uint16_t udp_len = 8+len;
    pkt[udp_off+4] = udp_len >> 8; pkt[udp_off+5] = udp_len & 0xFF;
    pkt[udp_off+6] = pkt[udp_off+7] = 0; // UDP checksum (skip)
    memcpy(pkt+udp_off+8, data, len);
    return rtl8139_send(pkt, 14+20+8+len);
}

int udp_poll_recv(struct ip_addr* src, uint16_t* port, void* buf, int maxlen) {
    uint8_t pkt[1514];
    int n = rtl8139_poll_recv(pkt, sizeof(pkt));
    if (n < 42) return 0;
    // Check IPv4/UDP
    if (pkt[12] != 0x08 || pkt[13] != 0x00) return 0;
    if ((pkt[14]>>4) != 4) return 0;
    if (pkt[23] != 17) return 0;
    // UDP
    int udp_off = 14+20;
    if (src) memcpy(src->addr, pkt+26, 4);
    if (port) *port = (pkt[udp_off+0]<<8) | pkt[udp_off+1];
    int len = ((pkt[udp_off+4]<<8) | pkt[udp_off+5]) - 8;
    if (len > maxlen) len = maxlen;
    memcpy(buf, pkt+udp_off+8, len);
    return len;
} 