#ifndef KERNEL_SOCKET_H
#define KERNEL_SOCKET_H

#include <stdint.h>
#include <stddef.h>
#include "net.h"

/* POSIX compliance */
typedef long ssize_t;

#define AF_INET 2
#define SOCK_STREAM 1
#define SOCK_DGRAM 2

/* Poll events */
#define POLLIN  0x0001
#define POLLOUT 0x0004

int socket_open(int domain, int type, int protocol);
int socket_bind(int sock, uint16_t port);
int socket_connect(int sock, struct ip_addr dest, uint16_t port);
int socket_listen(int sock, int backlog);
int socket_accept(int sock);
int socket_send(int sock, const void *buf, int len);
int socket_recv(int sock, void *buf, int maxlen);
int socket_close(int sock);
int socket_set_blocking(int sock, int blocking);
int socket_poll(int sock, int events, int timeout_ms);
void netstat_dump(void);

#endif /* KERNEL_SOCKET_H */
