#ifndef KERNEL_ICMP_H
#define KERNEL_ICMP_H

#include <stdint.h>

void icmp_init(void);
void icmp_handle_ipv4(const uint8_t src_ip[4], const uint8_t *icmp_payload, int icmp_len);
int icmp_send_echo_request(const uint8_t dst_ip[4], uint16_t id, uint16_t seq, const void *data, int dlen);

#endif /* KERNEL_ICMP_H */
