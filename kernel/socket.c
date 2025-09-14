#include "socket.h"
#include "tcp.h"
#include "kernel.h" // For memset, memcpy
#include "timer.h"
#include "serial.h"

/* The compiler suggests udp_send is declared, but not udp_poll_recv. Adding it for completeness. */
int udp_poll_recv(struct ip_addr* src, uint16_t* sport, void* data, int maxlen);

#define MAX_SOCK 32

typedef struct {
    int in_use;
    int domain; int type; int protocol;
    uint16_t lport;
    struct ip_addr rip;
    uint16_t rport;
    int tcp_pcb_id; /* for stream sockets */
    int blocking;
} ksock_t;

static ksock_t socks[MAX_SOCK];

int socket_open(int domain, int type, int protocol) {
    serial_write("[SOCKET] socket_open: entered\r\n");
    (void)protocol;
    for (int i=0;i<MAX_SOCK;++i) if (!socks[i].in_use) {
        if (type == SOCK_STREAM) {
            serial_write("[SOCKET] socket_open: creating TCP socket (calling sock_socket)...");
            int pcb_id = sock_socket();
            if (pcb_id < 0) {
                serial_write("[SOCKET] socket_open: sock_socket() failed\r\n");
                return -1;
            }
            serial_write("[SOCKET] socket_open: sock_socket() OK.\r\n");
            serial_write("[SOCKET] about to assign pcb_id\r\n");
            socks[i].tcp_pcb_id = pcb_id;
            serial_write("[SOCKET] pcb_id assigned\r\n");
        } else {
            socks[i].tcp_pcb_id = -1;
        }
        serial_write("[SOCKET] socket_open: setting fields...\r\n");
        socks[i].in_use=1;
        serial_write("1\n");
        socks[i].domain=domain;
        serial_write("2\n");
        socks[i].type=type;
        serial_write("3\n");
        socks[i].protocol=protocol;
        serial_write("4\n");
        socks[i].blocking=1;
        serial_write("5\n");
        socks[i].lport=0;
        serial_write("6\n");
        memset(&socks[i].rip,0,sizeof(socks[i].rip));
        serial_write("7\n");
        socks[i].rport=0;
        serial_write("8\n");
        return i+1;
    }
    serial_write("[SOCKET] socket_open: failed, no free sockets\r\n");
    return -1;
}

int socket_bind(int sock, uint16_t port) {
    serial_write("[SOCKET] socket_bind: entered\r\n");
    int idx = sock-1; if (idx<0||idx>=MAX_SOCK||!socks[idx].in_use) return -1;
    socks[idx].lport = port;
    if (socks[idx].type == SOCK_STREAM) {
        uint8_t local_ip[4] = {0,0,0,0}; // Bind to INADDR_ANY
        serial_write("[SOCKET] socket_bind: calling TCP sock_bind\r\n");
        return sock_bind(socks[idx].tcp_pcb_id, local_ip, port);
    }
    return 0;
}

int socket_connect(int sock, struct ip_addr dest, uint16_t port) {
    serial_write("[SOCKET] socket_connect: entered\r\n");
    int idx = sock-1; if (idx<0||idx>=MAX_SOCK||!socks[idx].in_use) return -1;
    socks[idx].rip = dest; socks[idx].rport = port;
    if (socks[idx].type == SOCK_STREAM) {
        serial_write("[SOCKET] socket_connect: calling TCP sock_connect...\r\n");
        int result = sock_connect(socks[idx].tcp_pcb_id, dest.addr, port);
        if (result == 0) {
            serial_write("[SOCKET] socket_connect: TCP sock_connect returned success.\r\n");
        } else {
            serial_write("[SOCKET] socket_connect: TCP sock_connect returned failure.\r\n");
        }
        return result;
    }
    serial_write("[SOCKET] socket_connect: UDP, returning 0.\r\n");
    return 0; // UDP is connectionless
}

int socket_listen(int sock, int backlog) {
    int idx = sock-1; if (idx<0||idx>=MAX_SOCK||!socks[idx].in_use) return -1;
    if (socks[idx].type == SOCK_STREAM) {
        return sock_listen(socks[idx].tcp_pcb_id, backlog);
    }
    return -1; // Not applicable for UDP
}

int socket_accept(int sock) {
    int idx = sock-1; if (idx<0||idx>=MAX_SOCK||!socks[idx].in_use) return -1;
    if (socks[idx].type != SOCK_STREAM) return -1;
    uint8_t child_ip[4];
    uint16_t child_port;
    int child_pcb_id = sock_accept(socks[idx].tcp_pcb_id, child_ip, &child_port);
    if (child_pcb_id < 0) return -1;

    int ns = socket_open(AF_INET, SOCK_STREAM, 0);
    if (ns < 0) return -1;
    socks[ns-1].tcp_pcb_id = child_pcb_id;
    memcpy(socks[ns-1].rip.addr, child_ip, 4);
    socks[ns-1].rport = child_port;
    return ns;
}

int socket_send(int sock, const void *buf, int len) {
    int idx = sock-1; if (idx<0||idx>=MAX_SOCK||!socks[idx].in_use) return -1;
    if (socks[idx].type == SOCK_DGRAM) {
        return udp_send(socks[idx].rip, socks[idx].rport, buf, len);
    } else if (socks[idx].type == SOCK_STREAM) {
        return sock_send(socks[idx].tcp_pcb_id, buf, len);
    }
    return -1;
}

int socket_recv(int sock, void *buf, int maxlen) {
    int idx = sock-1; if (idx<0||idx>=MAX_SOCK||!socks[idx].in_use) return -1;
    if (socks[idx].type == SOCK_DGRAM) {
        return udp_poll_recv(&socks[idx].rip, &socks[idx].rport, buf, maxlen);
    } else if (socks[idx].type == SOCK_STREAM) {
        return sock_recv(socks[idx].tcp_pcb_id, buf, maxlen);
    }
    return -1;
}

int socket_close(int sock) {
    int idx = sock-1; if (idx<0||idx>=MAX_SOCK||!socks[idx].in_use) return -1;
    if (socks[idx].type == SOCK_STREAM && socks[idx].tcp_pcb_id >= 0) sock_close(socks[idx].tcp_pcb_id);
    socks[idx].in_use = 0;
    return 0;
}

int socket_set_blocking(int sock, int blocking) {
    int idx = sock-1; if (idx<0||idx>=MAX_SOCK||!socks[idx].in_use) return -1;
    socks[idx].blocking = blocking ? 1 : 0;
    if (socks[idx].type == SOCK_STREAM) {
        sock_set_nonblock(socks[idx].tcp_pcb_id, !blocking);
    }
    return 0;
}

int socket_poll(int sock, int events, int timeout_ms) {
    uint64_t start = kernel_uptime_ms();
    do {
        int revents = 0;
        int idx = sock-1; if (idx<0||idx>=MAX_SOCK||!socks[idx].in_use) return -1;
        if (socks[idx].type == SOCK_DGRAM) {
            uint8_t tmp[1]; struct ip_addr s; uint16_t p; int n = udp_poll_recv(&s,&p,tmp,0); /* Peek */
            if (n > 0) revents |= POLLIN;
            revents |= POLLOUT;
        } else if (socks[idx].type == SOCK_STREAM) {
            int n = sock_recv(socks[idx].tcp_pcb_id, (void*)0, 0);
            if (n > 0) revents |= POLLIN;
            revents |= POLLOUT;
        }
        if (revents & events) return revents;
    } while ((int)(kernel_uptime_ms() - start) < timeout_ms);
    return 0;
}

/* Diagnostics */
extern void tcp_dump_pcbs(void);
void netstat_dump(void) {
    serial_write("[NETSTAT] Sockets:\n");
    for (int i=0;i<MAX_SOCK;++i) {
        if (!socks[i].in_use) continue;
        serial_write("  sock ");
        unsigned long id = i+1; char buf[21]; int j=20; buf[j--]=0; if(!id)buf[j--]='0'; while(id){buf[j--]='0'+(id%10); id/=10;} serial_write(&buf[j+1]);
        serial_write(" type="); serial_write(socks[i].type==SOCK_STREAM?"TCP":"UDP");
        serial_write(" lport="); unsigned long lp=socks[i].lport; j=20; buf[j--]=0; if(!lp)buf[j--]='0'; while(lp){buf[j--]='0'+(lp%10); lp/=10;} serial_write(&buf[j+1]);
        serial_write(" rport="); unsigned long rp=socks[i].rport; j=20; buf[j--]=0; if(!rp)buf[j--]='0'; while(rp){buf[j--]='0'+(rp%10); rp/=10;} serial_write(&buf[j+1]);
        serial_write("\n");
    }
    tcp_dump_pcbs();
}
