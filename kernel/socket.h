#ifndef KERNEL_SOCKET_H
#define KERNEL_SOCKET_H

#include <stdint.h>
#include <stddef.h>

int sock_socket(void);
int sock_bind(int s, const uint8_t ip[4], uint16_t port);
int sock_listen(int s, int backlog);
int sock_accept(int s, uint8_t out_ip[4], uint16_t *out_port);
int sock_connect(int s, const uint8_t ip[4], uint16_t port);
ssize_t sock_send(int s, const void *buf, size_t len);
ssize_t sock_recv(int s, void *buf, size_t len);
int sock_close(int s);
int sock_set_nonblock(int s, int nonblock);
int sock_poll(int *fds, int nfds, int *events_out, int timeout_ms);
void netstat_dump(void);

#endif /* KERNEL_SOCKET_H */
