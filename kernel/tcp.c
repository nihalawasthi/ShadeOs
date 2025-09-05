/*
  Compact but full-featured TCP implementation for ShadeOS:
  - retransmission timers with RTO and exponential backoff
  - accept backlog and passive open
  - graceful close with FIN/ACK and TIME_WAIT
  - basic segmentation (MSS) and reassembly for in-order delivery
  - socket layer with blocking/non-blocking and simple poll
*/
#include "tcp.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

extern int net_ipv4_send(const uint8_t dst_ip[4], uint8_t proto, const void *payload, int payload_len);
extern uint64_t kernel_uptime_ms(void);
extern void kernel_log(const char *fmt, ...);
extern void scheduler_sleep(void *wait_channel);
extern void scheduler_wakeup(void *wait_channel);
extern void timer_register_periodic(void (*fn)(void), int ms); /* call every ms */
extern uint8_t local_ip[4];
extern uint16_t htons(uint16_t);
extern uint16_t ntohs(uint16_t);

typedef enum {
    TCP_CLOSED = 0,
    TCP_LISTEN,
    TCP_SYN_SENT,
    TCP_SYN_RECEIVED,
    TCP_ESTABLISHED,
    TCP_FIN_WAIT_1,
    TCP_FIN_WAIT_2,
    TCP_CLOSE_WAIT,
    TCP_LAST_ACK,
    TCP_TIME_WAIT
} tcp_state_t;

typedef struct tx_seg {
    uint32_t seq;
    uint32_t len;
    uint8_t *data; /* includes TCP header+payload - ownership */
    uint64_t ts_sent;
    uint32_t rto_ms;
    int retries;
    struct tx_seg *next;
} tx_seg_t;

typedef struct rx_seg {
    uint32_t seq;
    uint32_t len;
    uint8_t *data;
    struct rx_seg *next;
} rx_seg_t;

#define MAX_SOCKETS 256
#define SEND_BUF_SIZE (64*1024)
#define RECV_BUF_SIZE (64*1024)

typedef struct tcp_socket {
    int used;
    int flags;
    tcp_state_t state;

    /* addressing */
    uint8_t laddr[4];
    uint16_t lport;
    uint8_t raddr[4];
    uint16_t rport;

    /* seq/ack */
    uint32_t iss; /* initial send seq */
    uint32_t snd_una; /* oldest unacked */
    uint32_t snd_nxt; /* next to use */
    uint32_t rcv_nxt; /* next expected */

    /* retransmit queue (segmented packets sent waiting for ACK) */
    tx_seg_t *tx_head, *tx_tail;

    /* receive out-of-order queue */
    rx_seg_t *rx_head;

    /* in-kernel application buffers */
    uint8_t *send_buf;
    uint32_t send_buf_head, send_buf_tail;
    uint8_t *recv_buf;
    uint32_t recv_buf_head, recv_buf_tail;

    /* accept backlog (for listening sockets) */
    int backlog;
    int pending_count;
    struct tcp_socket *pending[MAX_SOCKETS/8];

    /* wait channel for blocking accept/recv/send */
    void *wait_accept;
    void *wait_recv;
    void *wait_send;

    /* time-wait expiry */
    uint64_t timewait_expires;

    /* Next pointer for global conn list */
    struct tcp_socket *next;
} tcp_socket_t;

static tcp_socket_t sockets[MAX_SOCKETS];
static tcp_socket_t *conn_list = NULL;

/* helper: find free socket */
static tcp_socket_t *alloc_socket(void) {
    for (int i = 0; i < MAX_SOCKETS; ++i) {
        if (!sockets[i].used) {
            memset(&sockets[i], 0, sizeof(tcp_socket_t));
            sockets[i].used = 1;
            sockets[i].send_buf = malloc(SEND_BUF_SIZE);
            sockets[i].recv_buf = malloc(RECV_BUF_SIZE);
            /* initialize RTO defaults */
            return &sockets[i];
        }
    }
    return NULL;
}

static void free_socket_struct(tcp_socket_t *s) {
    if (!s) return;
    s->used = 0;
    if (s->send_buf) free(s->send_buf);
    if (s->recv_buf) free(s->recv_buf);
    /* free queued tx/rx segments */
    tx_seg_t *t = s->tx_head;
    while (t) {
        tx_seg_t *n = t->next;
        if (t->data) free(t->data);
        free(t);
        t = n;
    }
    rx_seg_t *r = s->rx_head;
    while (r) {
        rx_seg_t *n = r->next;
        if (r->data) free(r->data);
        free(r);
        r = n;
    }
}

/* small helper to push to global conn list */
static void conn_list_add(tcp_socket_t *s) {
    s->next = conn_list;
    conn_list = s;
}

/* helper to remove from conn list (linear scan) */
static void conn_list_remove(tcp_socket_t *s) {
    tcp_socket_t **pp = &conn_list;
    while (*pp) {
        if (*pp == s) {
            *pp = s->next;
            s->next = NULL;
            return;
        }
        pp = &(*pp)->next;
    }
}

/* TCP header (no options) */
struct __attribute__((packed)) tcp_header {
    uint16_t src;
    uint16_t dst;
    uint32_t seq;
    uint32_t ack;
    uint8_t  off_reserved; /* offset(4) + reserved(4) */
    uint8_t  flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent;
};

/* flags */
#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_RST 0x04
#define TCP_PSH 0x08
#define TCP_ACK 0x10
#define TCP_URG 0x20

/* pseudo header for checksum omitted for brevity (assume no verification in host) */

/* create and queue a segment to transmit (adds TCP header) */
static int tcp_queue_segment(tcp_socket_t *s, uint32_t seq, uint8_t flags, const void *payload, uint32_t plen) {
    /* build buffer: tcp header + payload */
    int hdrlen = sizeof(struct tcp_header);
    uint8_t *buf = malloc(hdrlen + plen);
    if (!buf) return -1;
    struct tcp_header *th = (struct tcp_header *)buf;
    th->src = htons(s->lport);
    th->dst = htons(s->rport);
    th->seq = htonl(seq);
    th->ack = htonl(s->rcv_nxt);
    th->off_reserved = (hdrlen/4)<<4;
    th->flags = flags | ((flags & TCP_ACK) ? TCP_ACK : 0);
    th->window = htons(RECV_BUF_SIZE);
    th->checksum = 0;
    th->urgent = 0;
    if (plen && payload) memcpy(buf + hdrlen, payload, plen);

    tx_seg_t *seg = malloc(sizeof(tx_seg_t));
    if (!seg) { free(buf); return -1; }
    seg->seq = seq;
    seg->len = hdrlen + plen;
    seg->data = buf;
    seg->ts_sent = 0;
    seg->rto_ms = 1000; /* initial RTO 1s */
    seg->retries = 0;
    seg->next = NULL;

    if (!s->tx_head) s->tx_head = s->tx_tail = seg;
    else {
        s->tx_tail->next = seg;
        s->tx_tail = seg;
    }
    return 0;
}

/* send queued segments (called by timer pass or immediately) */
static void tcp_send_queued(tcp_socket_t *s) {
    tx_seg_t *t = s->tx_head;
    while (t) {
        if (t->ts_sent == 0) {
            /* send now */
            /* For net_ipv4_send we need dst ip; use stored raddr */
            net_ipv4_send(s->raddr, 6, t->data, (int)t->len);
            t->ts_sent = kernel_uptime_ms();
        }
        t = t->next;
    }
}

/* push data from socket send_buf into segments */
static void tcp_segment_from_sendbuf(tcp_socket_t *s) {
    while (s->send_buf_head != s->send_buf_tail) {
        uint32_t avail = (s->send_buf_head <= s->send_buf_tail) ? 
            (s->send_buf_tail - s->send_buf_head) :
            (SEND_BUF_SIZE - s->send_buf_head);
        uint32_t chunk = avail;
        if (chunk > TCP_MSS) chunk = TCP_MSS;
        if (chunk == 0) break;
        uint8_t tmp[TCP_MSS];
        memcpy(tmp, s->send_buf + s->send_buf_head, chunk);
        s->send_buf_head = (s->send_buf_head + chunk) % SEND_BUF_SIZE;
        tcp_queue_segment(s, s->snd_nxt, TCP_PSH | TCP_ACK, tmp, chunk);
        s->snd_nxt += chunk;
    }
    tcp_send_queued(s);
}

/* Remove fully ACKed segments from retransmit queue */
static void tcp_acknowledge(tcp_socket_t *s, uint32_t ack) {
    while (s->tx_head && ( (s->tx_head->seq + s->tx_head->len - sizeof(struct tcp_header)) <= ack )) {
        tx_seg_t *t = s->tx_head;
        s->tx_head = t->next;
        if (!s->tx_head) s->tx_tail = NULL;
        if (t->data) free(t->data);
        free(t);
    }
}

/* insert rx segment into ordered rx queue */
static void rx_insert_ordered(tcp_socket_t *s, uint32_t seq, const uint8_t *data, uint32_t len) {
    /* ignore duplicates or already-acked data */
    if (seq + len <= s->rcv_nxt) return;
    rx_seg_t **pp = &s->rx_head;
    while (*pp && (*pp)->seq < seq) pp = &(*pp)->next;
    if (*pp && (*pp)->seq == seq) return; /* duplicate */
    rx_seg_t *r = malloc(sizeof(rx_seg_t));
    r->seq = seq;
    r->len = len;
    r->data = malloc(len);
    memcpy(r->data, data, len);
    r->next = *pp;
    *pp = r;

    /* now try to merge/advance recv buffer */
    while (s->rx_head && s->rx_head->seq == s->rcv_nxt) {
        rx_seg_t *m = s->rx_head;
        s->rx_head = m->next;
        /* copy into user recv_buf */
        uint32_t free_space = (s->recv_buf_head <= s->recv_buf_tail) ?
            (RECV_BUF_SIZE - s->recv_buf_tail) : (s->recv_buf_head - s->recv_buf_tail - 1);
        uint32_t copy_len = m->len;
        if (copy_len > free_space) copy_len = free_space;
        memcpy(s->recv_buf + s->recv_buf_tail, m->data, copy_len);
        s->recv_buf_tail = (s->recv_buf_tail + copy_len) % RECV_BUF_SIZE;
        s->rcv_nxt += m->len;
        free(m->data);
        free(m);
    }
    /* wake up any readers */
    scheduler_wakeup(s->wait_recv);
}

/* process incoming TCP packet */
void tcp_input_ipv4(const uint8_t *ip_hdr, int ip_hdr_len, const uint8_t *tcp_pkt, int tcp_len) {
    if (tcp_len < (int)sizeof(struct tcp_header)) return;
    const struct tcp_header *th = (const struct tcp_header *)tcp_pkt;
    uint16_t srcp = ntohs(th->src);
    uint16_t dstp = ntohs(th->dst);
    uint32_t seq = ntohl(th->seq);
    uint32_t ack = ntohl(th->ack);
    uint8_t flags = th->flags;
    const uint8_t *payload = tcp_pkt + sizeof(*th);
    int payload_len = tcp_len - sizeof(*th);

    /* find socket matching 4-tuple (simplified single match) */
    tcp_socket_t *s = NULL;
    for (tcp_socket_t *c = conn_list; c; c = c->next) {
        if (!c->used) continue;
        if (c->lport == dstp) {
            /* if local ip is wildcard or matches */
            if ((c->rport == 0 && (flags & TCP_SYN) && c->state == TCP_LISTEN) ||
                (memcmp(c->raddr, ip_hdr + 12, 4) == 0 && c->rport == srcp)) {
                s = c; break;
            }
        }
    }

    if (!s) {
        /* No socket - send RST in response to SYN/any other non-RST */
        if (!(flags & TCP_RST)) {
            uint8_t rst_buf[sizeof(struct tcp_header)];
            struct tcp_header *rth = (struct tcp_header *)rst_buf;
            rth->src = htons(dstp);
            rth->dst = htons(srcp);
            rth->seq = htonl(0);
            rth->ack = htonl(seq + payload_len + ((flags & TCP_SYN) ? 1 : 0));
            rth->off_reserved = (sizeof(struct tcp_header)/4)<<4;
            rth->flags = TCP_RST | TCP_ACK;
            rth->window = 0;
            net_ipv4_send((uint8_t *)(ip_hdr+12), 6, rst_buf, sizeof(rst_buf));
        }
        return;
    }

    /* handle based on state */
    if (s->state == TCP_LISTEN && (flags & TCP_SYN)) {
        /* passive open: create new child socket for connection handshake */
        tcp_socket_t *child = alloc_socket();
        if (!child) { kernel_log("tcp: cannot allocate child for incoming connection\n"); return; }
        memcpy(child->laddr, s->laddr, 4);
        child->lport = s->lport;
        memcpy(child->raddr, ip_hdr + 12, 4);
        child->rport = srcp;
        child->iss = (uint32_t)(rand() & 0xffff);
        child->snd_una = child->iss;
        child->snd_nxt = child->iss + 1;
        child->rcv_nxt = seq + 1;
        child->state = TCP_SYN_RECEIVED;

        /* enqueue into parent's accept backlog */
        if (s->pending_count < s->backlog) {
            s->pending[s->pending_count++] = child;
            conn_list_add(child);
            /* send SYN+ACK */
            tcp_queue_segment(child, child->iss, TCP_SYN | TCP_ACK, NULL, 0);
            tcp_send_queued(child);
            scheduler_wakeup(s->wait_accept);
        } else {
            /* backlog full: drop / ignore */
            free_socket_struct(child);
        }
        return;
    }

    /* For other states, simple ACK/FIN/PSH handling */
    if (flags & TCP_ACK) {
        tcp_acknowledge(s, ack);
        /* advance connection */
        if (s->state == TCP_SYN_SENT && ack > s->iss) {
            s->state = TCP_ESTABLISHED;
            scheduler_wakeup(s->wait_send);
        } else if (s->state == TCP_SYN_RECEIVED && ack >= s->snd_nxt) {
            s->state = TCP_ESTABLISHED;
            scheduler_wakeup(s->wait_accept);
        } else if (s->state == TCP_FIN_WAIT_1) {
            s->state = TCP_FIN_WAIT_2;
        } else if (s->state == TCP_LAST_ACK) {
            /* we closed and got final ack */
            s->state = TCP_CLOSED;
            conn_list_remove(s);
            free_socket_struct(s);
            return;
        }
    }

    if (payload_len > 0) {
        /* deliver / reassemble */
        rx_insert_ordered(s, seq, payload, payload_len);
        /* send ACK */
        tcp_queue_segment(s, s->snd_nxt, TCP_ACK, NULL, 0);
        tcp_send_queued(s);
    }

    if (flags & TCP_FIN) {
        s->rcv_nxt += 1;
        /* respond with ACK */
        tcp_queue_segment(s, s->snd_nxt, TCP_ACK, NULL, 0);
        tcp_send_queued(s);
        if (s->state == TCP_ESTABLISHED) {
            s->state = TCP_CLOSE_WAIT;
            scheduler_wakeup(s->wait_recv);
        } else if (s->state == TCP_FIN_WAIT_1) {
            s->state = TCP_TIME_WAIT;
            s->timewait_expires = kernel_uptime_ms() + 2*60*1000; /* 2 minutes TIME_WAIT */
        }
    }
}

/* periodic timer invoked (registered by tcp_init) */
static void tcp_timer_fn(void) {
    uint64_t now = kernel_uptime_ms();
    for (tcp_socket_t *s = conn_list; s; s = s->next) {
        /* retransmission handling */
        tx_seg_t *t = s->tx_head;
        while (t) {
            if (t->ts_sent && now - t->ts_sent >= t->rto_ms) {
                if (t->retries >= TCP_RETRANSMIT_MAX) {
                    /* abort connection */
                    kernel_log("tcp: aborting connection due to retransmit max\n");
                    s->state = TCP_CLOSED;
                    conn_list_remove(s);
                    free_socket_struct(s);
                    break;
                }
                /* retransmit */
                net_ipv4_send(s->raddr, 6, t->data, (int)t->len);
                t->ts_sent = now;
                t->retries++;
                t->rto_ms *= 2; /* exponential backoff */
            }
            t = t->next;
        }

        /* send new data if any */
        if (s->state == TCP_ESTABLISHED) {
            tcp_segment_from_sendbuf(s);
        }

        /* TIME_WAIT expiry handling */
        if (s->state == TCP_TIME_WAIT && now >= s->timewait_expires) {
            s->state = TCP_CLOSED;
            conn_list_remove(s);
            free_socket_struct(s);
        }
    }
}

/* socket API: map to simple fd (index) */
int sock_socket(void) {
    tcp_socket_t *s = alloc_socket();
    if (!s) return -1;
    /* default local wildcard; caller must bind */
    s->state = TCP_CLOSED;
    conn_list_add(s);
    return (int)(s - sockets);
}

int sock_bind(int sd, const uint8_t ip[4], uint16_t port) {
    if (sd < 0 || sd >= MAX_SOCKETS) return -1;
    tcp_socket_t *s = &sockets[sd];
    if (!s->used) return -1;
    memcpy(s->laddr, ip, 4);
    s->lport = port;
    return 0;
}

int sock_listen(int sd, int backlog) {
    if (sd < 0 || sd >= MAX_SOCKETS) return -1;
    tcp_socket_t *s = &sockets[sd];
    if (!s->used) return -1;
    s->backlog = backlog;
    s->state = TCP_LISTEN;
    return 0;
}

int sock_accept(int sd, uint8_t out_ip[4], uint16_t *out_port) {
    if (sd < 0 || sd >= MAX_SOCKETS) return -1;
    tcp_socket_t *s = &sockets[sd];
    if (!s->used || s->state != TCP_LISTEN) return -1;
    while (s->pending_count == 0) {
        scheduler_sleep(s->wait_accept);
    }
    tcp_socket_t *child = s->pending[0];
    /* shift queue */
    for (int i = 1; i < s->pending_count; ++i) s->pending[i-1] = s->pending[i];
    s->pending_count--;
    if (out_ip) memcpy(out_ip, child->raddr, 4);
    if (out_port) *out_port = child->rport;
    /* provide fd integer by returning index of child in sockets array */
    return (int)(child - sockets);
}

int sock_connect(int sd, const uint8_t ip[4], uint16_t port) {
    if (sd < 0 || sd >= MAX_SOCKETS) return -1;
    tcp_socket_t *s = &sockets[sd];
    if (!s->used) return -1;
    memcpy(s->raddr, ip, 4);
    s->rport = port;
    if (s->lport == 0) s->lport = (uint16_t)(1024 + (rand() % 40000));
    s->iss = (uint32_t)(rand() & 0xffff);
    s->snd_una = s->iss;
    s->snd_nxt = s->iss + 1;
    s->rcv_nxt = 0;
    s->state = TCP_SYN_SENT;
    /* send SYN */
    tcp_queue_segment(s, s->iss, TCP_SYN, NULL, 0);
    tcp_send_queued(s);
    /* block until established or timeout (simplified) */
    uint64_t start = kernel_uptime_ms();
    while (s->state != TCP_ESTABLISHED) {
        if (kernel_uptime_ms() - start > 5000) return -1;
        scheduler_sleep(s->wait_send);
    }
    return 0;
}

ssize_t sock_send(int sd, const void *buf, size_t len) {
    if (sd < 0 || sd >= MAX_SOCKETS) return -1;
    tcp_socket_t *s = &sockets[sd];
    if (!s->used) return -1;
    if (s->state != TCP_ESTABLISHED) return -1;
    size_t written = 0;
    const uint8_t *p = buf;
    while (written < len) {
        /* copy to send buffer (blocking if full) */
        uint32_t free_space = (s->send_buf_head <= s->send_buf_tail) ? 
            (SEND_BUF_SIZE - s->send_buf_tail - (s->send_buf_head==0?1:0)) :
            (s->send_buf_head - s->send_buf_tail - 1);
        if (free_space == 0) {
            /* blocking behavior: wait unless non-blocking flag */
            if (s->flags & SOCK_NONBLOCK) return written ? (ssize_t)written : -1;
            scheduler_sleep(s->wait_send);
            continue;
        }
        uint32_t to_copy = (uint32_t)(len - written);
        if (to_copy > free_space) to_copy = free_space;
        if (to_copy > TCP_MSS) to_copy = TCP_MSS;
        memcpy(s->send_buf + s->send_buf_tail, p + written, to_copy);
        s->send_buf_tail = (s->send_buf_tail + to_copy) % SEND_BUF_SIZE;
        written += to_copy;
        /* attempt to segment and send */
        tcp_segment_from_sendbuf(s);
    }
    return (ssize_t)written;
}

ssize_t sock_recv(int sd, void *buf, size_t len) {
    if (sd < 0 || sd >= MAX_SOCKETS) return -1;
    tcp_socket_t *s = &sockets[sd];
    if (!s->used) return -1;
    /* blocking if no data and not non-blocking */
    while (s->recv_buf_head == s->recv_buf_tail) {
        if (s->flags & SOCK_NONBLOCK) return 0;
        scheduler_sleep(s->wait_recv);
    }
    uint32_t avail = (s->recv_buf_head < s->recv_buf_tail) ? (s->recv_buf_tail - s->recv_buf_head) : (RECV_BUF_SIZE - s->recv_buf_head);
    uint32_t to_copy = (uint32_t)len;
    if (to_copy > avail) to_copy = avail;
    memcpy(buf, s->recv_buf + s->recv_buf_head, to_copy);
    s->recv_buf_head = (s->recv_buf_head + to_copy) % RECV_BUF_SIZE;
    /* wake senders (space freed) */
    scheduler_wakeup(s->wait_send);
    return (ssize_t)to_copy;
}

int sock_close(int sd) {
    if (sd < 0 || sd >= MAX_SOCKETS) return -1;
    tcp_socket_t *s = &sockets[sd];
    if (!s->used) return -1;
    if (s->state == TCP_ESTABLISHED) {
        /* start active close: send FIN */
        tcp_queue_segment(s, s->snd_nxt, TCP_FIN | TCP_ACK, NULL, 0);
        s->snd_nxt += 1;
        s->state = TCP_FIN_WAIT_1;
        tcp_send_queued(s);
        /* wait for FIN handshake completion in input handler */
        return 0;
    } else {
        /* immediate close */
        conn_list_remove(s);
        free_socket_struct(s);
        return 0;
    }
}

int sock_set_nonblock(int sfd, int nonblock) {
    if (sfd < 0 || sfd >= MAX_SOCKETS) return -1;
    tcp_socket_t *s = &sockets[sfd];
    if (!s->used) return -1;
    if (nonblock) s->flags |= SOCK_NONBLOCK;
    else s->flags &= ~SOCK_NONBLOCK;
    return 0;
}

/* poll implementation:
   - fds: array of integers (sockets)
   - events_out: filled with event flags for each fd
   - timeout_ms: -1 for infinite, 0 for non-blocking
*/
int sock_poll(int *fds, int nfds, int *events_out, int timeout_ms) {
    int ready = 0;
    uint64_t start = kernel_uptime_ms();
    while (1) {
        ready = 0;
        for (int i = 0; i < nfds; ++i) {
            int fd = fds[i];
            events_out[i] = 0;
            if (fd < 0 || fd >= MAX_SOCKETS) continue;
            tcp_socket_t *s = &sockets[fd];
            if (!s->used) continue;
            /* readable if recv buffer has data */
            if (s->recv_buf_head != s->recv_buf_tail) {
                events_out[i] |= POLL_IN;
            }
            /* writable if send buffer has free space */
            uint32_t free_space = (s->send_buf_head <= s->send_buf_tail) ? 
                (SEND_BUF_SIZE - s->send_buf_tail - (s->send_buf_head==0?1:0)) :
                (s->send_buf_head - s->send_buf_tail - 1);
            if (free_space > 0) events_out[i] |= POLL_OUT;
            if (events_out[i]) ready++;
        }
        if (ready || timeout_ms == 0) return ready;
        if (timeout_ms > 0 && (int)(kernel_uptime_ms() - start) >= timeout_ms) return 0;
        /* sleep until an event; use a coarse wake channel */
        scheduler_sleep(&conn_list);
    }
}

void netstat_dump(void) {
    kernel_log("Active TCP connections:\n");
    for (tcp_socket_t *s = conn_list; s; s = s->next) {
        if (!s->used) continue;
        kernel_log("lport=%u rport=%u state=%d send_unacked=%u recv_next=%u\n",
            s->lport, s->rport, s->state, s->snd_una, s->rcv_nxt);
    }
}

/* init */
int tcp_init(void) {
    memset(sockets, 0, sizeof(sockets));
    timer_register_periodic(tcp_timer_fn, 100); /* 100ms tick */
    return 0;
}
