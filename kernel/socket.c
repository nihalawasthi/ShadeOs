#include "socket.h"
#include "tcp.h"
#include <string.h>
#include "timer.h"
#include "serial.h"

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
    (void)protocol;
    for (int i=0;i<MAX_SOCK;++i) if (!socks[i].in_use) { socks[i].in_use=1; socks[i].domain=domain; socks[i].type=type; socks[i].protocol=protocol; socks[i].tcp_pcb_id=-1; socks[i].blocking=1; socks[i].lport=0; memset(&socks[i].rip,0,sizeof(socks[i].rip)); socks[i].rport=0; return i+1; }
    return -1;
}

int socket_bind(int sock, uint16_t port) {
    int idx = sock-1; if (idx<0||idx>=MAX_SOCK||!socks[idx].in_use) return -1;
    socks[idx].lport = port;
    if (socks[idx].type == SOCK_STREAM) {
        /* allocate a listener pcb */
        int pcb = tcp_listen(port);
        if (pcb < 0) return -1;
        socks[idx].tcp_pcb_id = pcb;
    }
    return 0;
}

int socket_connect(int sock, struct ip_addr dest, uint16_t port) {
    int idx = sock-1; if (idx<0||idx>=MAX_SOCK||!socks[idx].in_use) return -1;
    socks[idx].rip = dest; socks[idx].rport = port;
    if (socks[idx].type == SOCK_DGRAM) {
        return 0; /* UDP doesn't need handshake */
    } else if (socks[idx].type == SOCK_STREAM) {
        int pcb = tcp_connect(dest.addr, port);
        socks[idx].tcp_pcb_id = pcb;
        if (pcb < 0) return -1;
        if (socks[idx].blocking) {
            uint64_t start = kernel_uptime_ms();
            while (tcp_get_state(pcb) != TCP_ESTABLISHED) {
                if (kernel_uptime_ms() - start > 3000) return -1; /* 3s timeout */
            }
        }
        return 0;
    }
    return -1;
}

int socket_listen(int sock, int backlog) {
    (void)backlog; int idx = sock-1; if (idx<0||idx>=MAX_SOCK||!socks[idx].in_use) return -1;
    return 0; /* already handled in bind for this minimal stack */
}

int socket_accept(int sock) {
    int idx = sock-1; if (idx<0||idx>=MAX_SOCK||!socks[idx].in_use) return -1;
    if (socks[idx].type != SOCK_STREAM) return -1;
    int lst = socks[idx].tcp_pcb_id;
    int child;
    if (socks[idx].blocking) {
        uint64_t start = kernel_uptime_ms();
        while ((child = tcp_accept(lst)) < 0) {
            if (kernel_uptime_ms() - start > 10000) return -1; /* 10s */
        }
    } else {
        child = tcp_accept(lst);
        if (child < 0) return -1;
    }
    int ns = socket_open(AF_INET, SOCK_STREAM, 0);
    if (ns < 0) return -1;
    socks[ns-1].tcp_pcb_id = child;
    return ns;
}

int socket_send(int sock, const void *buf, int len) {
    int idx = sock-1; if (idx<0||idx>=MAX_SOCK||!socks[idx].in_use) return -1;
    if (socks[idx].type == SOCK_DGRAM) {
        return udp_send(socks[idx].rip, socks[idx].rport, buf, len);
    } else if (socks[idx].type == SOCK_STREAM) {
        int ret;
        while ((ret = tcp_send(socks[idx].tcp_pcb_id, buf, len)) < 0) {
            if (!socks[idx].blocking) return -1;
        }
        return ret;
    }
    return -1;
}

int socket_recv(int sock, void *buf, int maxlen) {
    int idx = sock-1; if (idx<0||idx>=MAX_SOCK||!socks[idx].in_use) return -1;
    if (socks[idx].type == SOCK_DGRAM) {
        struct ip_addr src; uint16_t port; return udp_poll_recv(&src, &port, buf, maxlen);
    } else if (socks[idx].type == SOCK_STREAM) {
        int n;
        while ((n = tcp_recv(socks[idx].tcp_pcb_id, buf, maxlen)) == 0) {
            if (!socks[idx].blocking) return 0;
        }
        return n;
    }
    return -1;
}

int socket_close(int sock) {
    int idx = sock-1; if (idx<0||idx>=MAX_SOCK||!socks[idx].in_use) return -1;
    if (socks[idx].type == SOCK_STREAM && socks[idx].tcp_pcb_id >= 0) tcp_close(socks[idx].tcp_pcb_id);
    socks[idx].in_use = 0;
    return 0;
}

int socket_set_blocking(int sock, int blocking) {
    int idx = sock-1; if (idx<0||idx>=MAX_SOCK||!socks[idx].in_use) return -1;
    socks[idx].blocking = blocking ? 1 : 0;
    return 0;
}

int socket_poll(int sock, int events, int timeout_ms) {
    uint64_t start = kernel_uptime_ms();
    do {
        int revents = 0;
        int idx = sock-1; if (idx<0||idx>=MAX_SOCK||!socks[idx].in_use) return -1;
        if (socks[idx].type == SOCK_DGRAM) {
            uint8_t tmp[1]; struct ip_addr s; uint16_t p; int n = udp_poll_recv(&s,&p,tmp,1);
            if (n > 0) revents |= POLLIN;
            revents |= POLLOUT;
        } else if (socks[idx].type == SOCK_STREAM) {
            int n = tcp_recv(socks[idx].tcp_pcb_id, (void*)0, 0);
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
