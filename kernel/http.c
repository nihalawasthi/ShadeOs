#include "http.h"
#include "socket.h"
#include <string.h>

int http_get(const uint8_t dst_ip[4], const char *path) {
    int s = socket_open(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return -1;
    struct ip_addr ip; memcpy(ip.addr, dst_ip, 4);
    if (socket_connect(s, ip, 80) != 0) return -1;
    char req[256];
    int n = 0; const char *p = "GET "; while (*p && n < 250) req[n++] = *p++;
    p = path ? path : "/"; while (*p && n < 250) req[n++] = *p++;
    p = " HTTP/1.0\r\nHost: "; while (*p && n < 250) req[n++] = *p++;
    const char *host = "example"; p = host; while (*p && n < 250) req[n++] = *p++;
    p = "\r\n\r\n"; while (*p && n < 254) req[n++] = *p++;
    req[n] = 0;
    socket_send(s, req, n);
    /* recv not implemented for TCP path */
    socket_close(s);
    return 0;
}

int http_start_simple_server(uint16_t port) {
    (void)port; /* A full server needs TCP recv path; not implemented here. */
    return 0;
}
