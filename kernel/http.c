#include "http.h"
#include "socket.h"
#include <string.h>
#include "serial.h"
#include "heap.h"

int http_get(const uint8_t dst_ip[4], const char *host, const char *path) {
    char ip_str[16];
    snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d", dst_ip[0], dst_ip[1], dst_ip[2], dst_ip[3]);
    serial_write("[HTTP] GET request to ");
    serial_write(ip_str);
    serial_write("...\n");
    int s = socket_open(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        serial_write("[HTTP] socket_open failed\n");
        return -1;
    }
    serial_write("[HTTP] socket_open returned a socket descriptor.\n");

    struct ip_addr ip; memcpy(ip.addr, dst_ip, 4);
    if (socket_connect(s, ip, 80) != 0) {
        serial_write("[HTTP] socket_connect failed\n");
        socket_close(s);
        return -1;
    }
    serial_write("[HTTP] Connected.\n");

    char req[256];
    int n = snprintf(req, sizeof(req), "GET %s HTTP/1.0\r\nHost: %s\r\n\r\n",
                     path ? path : "/",
                     host ? host : "localhost");

    socket_send(s, req, n);
    serial_write("[HTTP] Request sent.\n");

    char* buf = kmalloc(1501);
    if (!buf) {
        serial_write("[HTTP] kmalloc for recv buffer failed\n");
        socket_close(s);
        return -1;
    }

    serial_write("[HTTP] Receiving response...\n---\n");
    int bytes_recvd;
    while ((bytes_recvd = socket_recv(s, buf, 1500)) > 0) {
        buf[bytes_recvd] = 0;
        serial_write(buf);
    }
    serial_write("\n---\n[HTTP] Connection finished.\n");

    kfree(buf);
    socket_close(s);
    return 0;
}

int http_start_simple_server(uint16_t port) {
    (void)port; /* A full server needs TCP recv path; not implemented here. */
    return 0;
}
