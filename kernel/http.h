#ifndef HTTP_H
#define HTTP_H

#include "kernel.h"
#include "net.h"

int http_get(const uint8_t dst_ip[4], const char *host, const char *path);
int http_start_simple_server(uint16_t port);

#endif /* HTTP_H */
