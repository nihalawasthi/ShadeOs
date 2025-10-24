#include "icmp.h"
#include "endian.h"
#include "net.h"
#include <stdint.h>
#include "serial.h"

/* External helper provided by net.c */
extern int net_ipv4_send(const uint8_t dst_ip[4], uint8_t proto, const void *payload, int payload_len);

struct __attribute__((packed)) icmp_hdr {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    union {
        struct {
            uint16_t id;
            uint16_t seq;
        } echo;
        uint32_t unused;
    } u;
};

static uint16_t ip_checksum_local(const void *vdata, int length) {
    const uint8_t *data = (const uint8_t*)vdata;
    uint32_t sum = 0;
    while (length > 1) {
        sum += (uint16_t)((data[0] << 8) | data[1]);
        data += 2;
        length -= 2;
    }
    if (length > 0) sum += (uint32_t)data[0] << 8;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

void icmp_init(void) { }

void icmp_handle_ipv4(const uint8_t src_ip[4], const uint8_t *icmp_payload, int icmp_len) {
    if (icmp_len < (int)sizeof(struct icmp_hdr)) return;
    const struct icmp_hdr *ih = (const struct icmp_hdr *)icmp_payload;
    if (ih->type == 8) { /* Echo request */
        int total_len = icmp_len;
        uint8_t *reply = (uint8_t *)kmalloc(total_len);
        if (!reply) return;
        memcpy(reply, icmp_payload, total_len);
        struct icmp_hdr *rh = (struct icmp_hdr *)reply;
        rh->type = 0; /* echo reply */
        rh->checksum = 0;
        rh->checksum = ip_checksum_local(reply, total_len);
        net_ipv4_send(src_ip, 1 /* ICMP */, reply, total_len);
        kfree(reply);
    } else if (ih->type == 0) {
        /* echo reply received: print notification */
        serial_write("[ICMP] Echo reply received from: ");
        for (int i = 0; i < 4; ++i) {
            char buf[8];
            snprintf(buf, sizeof(buf), "%d", src_ip[i]);
            serial_write(buf);
            if (i < 3) serial_write(".");
        }
        serial_write("\n");
        vga_print("[ICMP] Echo reply received\n");
    }
}

int icmp_send_echo_request(const uint8_t dst_ip[4], uint16_t id, uint16_t seq, const void *data, int dlen) {
    int icmp_len = (int)sizeof(struct icmp_hdr) + dlen;
    uint8_t *buf = (uint8_t*)kmalloc(icmp_len);
    if (!buf) return -1;
    struct icmp_hdr *ih = (struct icmp_hdr *)buf;
    ih->type = 8;
    ih->code = 0;
    ih->checksum = 0;
    ih->u.echo.id = htons(id);
    ih->u.echo.seq = htons(seq);
    if (dlen && data) memcpy(buf + sizeof(*ih), data, dlen);
    ih->checksum = ip_checksum_local(buf, icmp_len);
    int ret = net_ipv4_send(dst_ip, 1 /* ICMP */, buf, icmp_len);
    kfree(buf);
    return ret;
}
