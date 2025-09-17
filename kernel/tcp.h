#ifndef KERNEL_TCP_H
#define KERNEL_TCP_H

#include "kernel.h"

/* POSIX compliance */
typedef long ssize_t;

#define TCP_MSS 1460
#define TCP_RETRANSMIT_MAX 5

/* socket-level flags */
#define SOCK_NONBLOCK 0x1

/* socket events for poll */
#define POLL_IN  0x01
#define POLL_OUT 0x02
#define POLL_ERR 0x04

int tcp_init(void);

/* called from IPv4 input path when proto==6 (TCP) */
void tcp_input_ipv4(const uint8_t *ip_hdr, int ip_hdr_len, const uint8_t *tcp_pkt, int tcp_len);

/* socket-facing helpers exported for syscall layer / userland wrappers */
int sock_socket(void);
int sock_bind(int s, const uint8_t ip[4], uint16_t port);
int sock_listen(int s, int backlog);
int sock_accept(int s, uint8_t out_ip[4], uint16_t *out_port); /* blocking by default */
int sock_connect(int s, const uint8_t ip[4], uint16_t port);
ssize_t sock_send(int s, const void *buf, size_t len);
ssize_t sock_recv(int s, void *buf, size_t len);
int sock_close(int s);
int sock_set_nonblock(int s, int nonblock);

/* poll/select like function */
int sock_poll(int *fds, int nfds, int *events_out, int timeout_ms);

/* utility for netstat dump */
void tcp_dump_pcbs(void);

#endif /* KERNEL_TCP_H */
