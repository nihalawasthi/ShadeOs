#include "tcp.h"
#include "heap.h"
#include "endian.h"
#include "net.h"
#include "serial.h"

extern uint64_t kernel_uptime_ms(void);
extern void kernel_log(const char *fmt, ...);
extern void scheduler_sleep(void *wait_channel);
extern void scheduler_wakeup(void *wait_channel);
extern void timer_register_periodic(void (*fn)(void), uint32_t ms); /* call every ms */

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

typedef enum {
    TCP_CA_SLOW_START = 0,
    TCP_CA_CONGESTION_AVOIDANCE,
    TCP_CA_FAST_RECOVERY
} tcp_ca_state_t;

typedef struct rtt_sample {
    uint32_t seq;
    uint64_t ts_sent;
    struct rtt_sample *next;
} rtt_sample_t;

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
#define RECV_BUF_SIZE (65535)
#define TCP_MSS 1460
#define TCP_RETRANSMIT_MAX 5

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

    /* congestion control */
    tcp_ca_state_t ca_state;
    uint32_t cwnd;          /* congestion window */
    uint32_t ssthresh;      /* slow start threshold */
    uint32_t snd_wnd;       /* peer's advertised window */
    uint32_t max_window;    /* maximum window seen */
    uint32_t duplicate_acks; /* count of duplicate ACKs */
    
    /* RTT estimation (RFC 6298) */
    uint32_t srtt;          /* smoothed RTT in ms */
    uint32_t rttvar;        /* RTT variation in ms */
    uint32_t rto;           /* retransmission timeout in ms */
    rtt_sample_t *rtt_samples; /* pending RTT samples */

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

#define TCP_RTO_MIN 200     /* minimum RTO in ms */
#define TCP_RTO_MAX 60000   /* maximum RTO in ms */
#define TCP_RTO_INITIAL 1000 /* initial RTO in ms */

/* helper: find free socket */
static tcp_socket_t *alloc_socket(void) {
    serial_write("[TCP] alloc_socket: entered\n");
    for (int i = 0; i < MAX_SOCKETS; ++i) {
        if (!sockets[i].used) {
            serial_write("[TCP] alloc_socket: found free socket struct.\n");
            memset(&sockets[i], 0, sizeof(tcp_socket_t));
            serial_write("[TCP] alloc_socket: memset OK.\n");
            sockets[i].used = 1;
            serial_write("[TCP] alloc_socket: calling kmalloc for send_buf...\n");
            sockets[i].send_buf = kmalloc(SEND_BUF_SIZE);
            if (!sockets[i].send_buf) {
                serial_write("[TCP] alloc_socket: kmalloc for send_buf FAILED.\n");
                sockets[i].used = 0;
                return NULL;
            }
            serial_write("[TCP] alloc_socket: kmalloc for send_buf OK.\n");
            serial_write("[TCP] alloc_socket: calling kmalloc for recv_buf...\n");
            sockets[i].recv_buf = kmalloc(RECV_BUF_SIZE);
            if (!sockets[i].recv_buf) {
                serial_write("[TCP] alloc_socket: kmalloc for recv_buf FAILED.\n");
                kfree(sockets[i].send_buf);
                sockets[i].used = 0;
                return NULL;
            }
            serial_write("[TCP] alloc_socket: kmalloc for recv_buf OK.\n");
            
            serial_write("[TCP] alloc_socket: setting ca_state...\n");
            sockets[i].ca_state = TCP_CA_SLOW_START;
            serial_write("[TCP] alloc_socket: setting cwnd...\n");
            sockets[i].cwnd = TCP_MSS;
            serial_write("[TCP] alloc_socket: setting ssthresh...\n");
            sockets[i].ssthresh = 65535;
            serial_write("[TCP] alloc_socket: setting snd_wnd...\n");
            sockets[i].snd_wnd = 65535;
            serial_write("[TCP] alloc_socket: setting rto...\n");
            sockets[i].rto = TCP_RTO_INITIAL;
            
            /* Initialize wait channels */
            sockets[i].wait_accept = &sockets[i].wait_accept;
            sockets[i].wait_recv = &sockets[i].wait_recv;
            sockets[i].wait_send = &sockets[i].wait_send;

            serial_write("[TCP] alloc_socket: all fields set.\n");

            serial_write("[TCP] alloc_socket: returning new socket.\n");
            return &sockets[i];
        }
    }
    serial_write("[TCP] alloc_socket: no free sockets.\n");
    return NULL;
}

static void free_socket_struct(tcp_socket_t *s) {
    if (!s) return;
    s->used = 0;
    if (s->send_buf) kfree(s->send_buf);
    if (s->recv_buf) kfree(s->recv_buf);
    
    /* free queued tx/rx segments */
    tx_seg_t *t = s->tx_head;
    while (t) {
        tx_seg_t *n = t->next;
        if (t->data) kfree(t->data);
        kfree(t);
        t = n;
    }
    rx_seg_t *r = s->rx_head;
    while (r) {
        rx_seg_t *n = r->next;
        if (r->data) kfree(r->data);
        kfree(r);
        r = n;
    }
    
    /* free RTT samples */
    rtt_sample_t *rtt = s->rtt_samples;
    while (rtt) {
        rtt_sample_t *n = rtt->next;
        kfree(rtt);
        rtt = n;
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

static void tcp_add_rtt_sample(tcp_socket_t *s, uint32_t seq) {
    rtt_sample_t *sample = kmalloc(sizeof(rtt_sample_t));
    if (!sample) return; 
    
    sample->seq = seq;
    sample->ts_sent = kernel_uptime_ms();
    sample->next = s->rtt_samples;
    s->rtt_samples = sample;
}

static void tcp_update_rto(tcp_socket_t *s, uint32_t ack_seq) {
    rtt_sample_t **pp = &s->rtt_samples;
    while (*pp) {
        rtt_sample_t *sample = *pp;
        if (sample->seq <= ack_seq) {
            uint64_t now = kernel_uptime_ms();
            uint32_t rtt = (uint32_t)(now - sample->ts_sent);
            
            if (s->srtt == 0) {
                /* First RTT measurement */
                s->srtt = rtt;
                s->rttvar = rtt / 2;
            } else {
                /* RFC 6298 algorithm */
                uint32_t abs_diff = (rtt > s->srtt) ? (rtt - s->srtt) : (s->srtt - rtt);
                s->rttvar = (3 * s->rttvar + abs_diff) / 4;
                s->srtt = (7 * s->srtt + rtt) / 8;
            }
            
            s->rto = s->srtt + (4 * s->rttvar);
            if (s->rto < TCP_RTO_MIN) s->rto = TCP_RTO_MIN;
            if (s->rto > TCP_RTO_MAX) s->rto = TCP_RTO_MAX;
            
            /* Remove this sample */
            *pp = sample->next;
            kfree(sample);
        } else {
            pp = &sample->next;
        }
    }
}

static void tcp_process_ack_congestion_control(tcp_socket_t *s, uint32_t ack, int is_duplicate) {
    if (is_duplicate) {
        s->duplicate_acks++;
        if (s->duplicate_acks == 3 && s->ca_state != TCP_CA_FAST_RECOVERY) {
            /* Fast retransmit */
            s->ssthresh = s->cwnd / 2;
            if (s->ssthresh < 2 * TCP_MSS) s->ssthresh = 2 * TCP_MSS;
            s->cwnd = s->ssthresh + 3 * TCP_MSS;
            s->ca_state = TCP_CA_FAST_RECOVERY;
            kernel_log("tcp: entering fast recovery, cwnd=%u ssthresh=%u\n", s->cwnd, s->ssthresh);
        } else if (s->ca_state == TCP_CA_FAST_RECOVERY) {
            s->cwnd += TCP_MSS;
        }
    } else {
        /* New ACK */
        s->duplicate_acks = 0;
        tcp_update_rto(s, ack);
        
        if (s->ca_state == TCP_CA_FAST_RECOVERY) {
            s->cwnd = s->ssthresh;
            s->ca_state = TCP_CA_CONGESTION_AVOIDANCE;
        } else if (s->ca_state == TCP_CA_SLOW_START) {
            s->cwnd += TCP_MSS;
            if (s->cwnd >= s->ssthresh) {
                s->ca_state = TCP_CA_CONGESTION_AVOIDANCE;
            }
        } else if (s->ca_state == TCP_CA_CONGESTION_AVOIDANCE) {
            s->cwnd += (TCP_MSS * TCP_MSS) / s->cwnd;
        }
    }
}

/* create and queue a segment to transmit (adds TCP header) */
static int tcp_queue_segment(tcp_socket_t *s, uint32_t seq, uint8_t flags, const void *payload, uint32_t plen) {
    if (!s) {
        serial_write("[TCP] ERROR: NULL socket\n");
        return -1;
    }
    
    if (!s->used) {
        serial_write("[TCP] ERROR: Socket not in use\n");
        return -1;
    }
    
    if (plen > TCP_MSS) {
        serial_write("[TCP] ERROR: Payload too large\n");
        return -1;
    }
    
    serial_write("[TCP] tcp_queue_segment: starting\n");
    
    if (s->lport == 0 || s->rport == 0) {
        serial_write("[TCP] ERROR: Invalid port configuration\n");
        return -1;
    }
    
    /* build buffer: tcp header + payload */
    int hdrlen = sizeof(struct tcp_header);
    serial_write("[TCP] tcp_queue_segment: allocating buffer\n");
    
    uint8_t *buf = kmalloc(hdrlen + plen + 8); // Add padding for safety
    if (!buf) {
        serial_write("[TCP] ERROR: kmalloc failed\n");
        return -1;
    }
    
    memset(buf, 0, hdrlen + plen + 8);
    serial_write("[TCP] tcp_queue_segment: buffer allocated and cleared\n");
    
    if ((uintptr_t)buf % 4 != 0) {
        serial_write("[TCP] WARNING: Buffer not aligned\n");
    }
    
    serial_write("[TCP] tcp_queue_segment: creating header\n");
    struct tcp_header *th = (struct tcp_header *)buf;
    
    serial_write("[TCP] tcp_queue_segment: setting src port\n");
    th->src = htons(s->lport);
    
    serial_write("[TCP] tcp_queue_segment: setting dst port\n");
    th->dst = htons(s->rport);
    
    serial_write("[TCP] tcp_queue_segment: setting sequence\n");
    th->seq = htonl(seq);
    
    serial_write("[TCP] tcp_queue_segment: setting ack\n");
    th->ack = htonl(s->rcv_nxt);
    
    serial_write("[TCP] tcp_queue_segment: setting offset\n");
    th->off_reserved = (hdrlen/4)<<4;
    
    serial_write("[TCP] tcp_queue_segment: setting flags\n");
    th->flags = flags | ((flags & TCP_ACK) ? TCP_ACK : 0);
    
    serial_write("[TCP] tcp_queue_segment: calculating window\n");
    uint32_t recv_space = (s->recv_buf_head <= s->recv_buf_tail) ?
        (RECV_BUF_SIZE - (s->recv_buf_tail - s->recv_buf_head)) :
        (s->recv_buf_head - s->recv_buf_tail);
    
    serial_write("[TCP] tcp_queue_segment: setting window\n");
    th->window = htons((uint16_t)(recv_space > 65535 ? 65535 : recv_space));
    
    serial_write("[TCP] tcp_queue_segment: setting checksum and urgent\n");
    th->checksum = 0;
    th->urgent = 0;
    
    if (plen && payload) {
        serial_write("[TCP] tcp_queue_segment: copying payload\n");
        memcpy(buf + hdrlen, payload, plen);
    }

    serial_write("[TCP] tcp_queue_segment: header filled successfully\n");
    
    serial_write("[TCP] tcp_queue_segment: allocating segment struct\n");
    tx_seg_t *seg = kmalloc(sizeof(tx_seg_t));
    if (!seg) { 
        serial_write("[TCP] ERROR: segment struct allocation failed\n");
        kfree(buf); 
        return -1; 
    }
    
    memset(seg, 0, sizeof(tx_seg_t));
    serial_write("[TCP] tcp_queue_segment: segment struct created\n");
    
    serial_write("[TCP] tcp_queue_segment: setting seg->seq\n");
    seg->seq = seq;
    
    serial_write("[TCP] tcp_queue_segment: setting seg->len\n");
    seg->len = hdrlen + plen;
    
    serial_write("[TCP] tcp_queue_segment: setting seg->data\n");
    seg->data = buf;
    
    serial_write("[TCP] tcp_queue_segment: setting seg->ts_sent\n");
    seg->ts_sent = 0;
    
    serial_write("[TCP] tcp_queue_segment: validating socket rto field\n");
    if ((uintptr_t)&s->rto < 0x1000 || (uintptr_t)&s->rto > 0x7FFFFFFF) {
        serial_write("[TCP] ERROR: Invalid socket rto field address\n");
        kfree(seg->data);
        kfree(seg);
        return -1;
    }
    
    serial_write("[TCP] tcp_queue_segment: setting seg->rto_ms\n");
    seg->rto_ms = s->rto;
    
    serial_write("[TCP] tcp_queue_segment: setting seg->retries\n");
    seg->retries = 0;
    
    serial_write("[TCP] tcp_queue_segment: setting seg->next\n");
    seg->next = NULL;
    
    serial_write("[TCP] tcp_queue_segment: all segment fields set\n");
    
    serial_write("[TCP] tcp_queue_segment: checking queue state\n");
    
    // Validate socket structure integrity before queue manipulation
    if ((uintptr_t)s < 0x1000 || (uintptr_t)s > 0x7FFFFFFF) {
        serial_write("[TCP] ERROR: Invalid socket pointer\n");
        kfree(seg->data);
        kfree(seg);
        return -1;
    }
    
    serial_write("[TCP] tcp_queue_segment: socket pointer valid\n");
    
    // Check tx_head pointer validity
    serial_write_hex("[TCP] tx_head address: ", (uintptr_t)&s->tx_head);
    serial_write_hex("[TCP] tx_head value: ", (uintptr_t)s->tx_head);
    
    // Check tx_tail pointer validity  
    serial_write_hex("[TCP] tx_tail address: ", (uintptr_t)&s->tx_tail);
    serial_write_hex("[TCP] tx_tail value: ", (uintptr_t)s->tx_tail);
    
    serial_write("[TCP] tcp_queue_segment: about to check tx_head == NULL\n");
    
    // This is where the crash likely occurs - let's be very careful
    volatile tx_seg_t *volatile_tx_head = s->tx_head;
    serial_write("[TCP] tcp_queue_segment: loaded tx_head into volatile\n");
    
    if (volatile_tx_head == NULL) {
        serial_write("[TCP] tcp_queue_segment: tx_head is NULL, setting both pointers\n");
        s->tx_head = seg;
        serial_write("[TCP] tcp_queue_segment: set tx_head\n");
        s->tx_tail = seg;
        serial_write("[TCP] tcp_queue_segment: set tx_tail\n");
        serial_write("[TCP] tcp_queue_segment: added to empty queue\n");
    } else {
        serial_write("[TCP] tcp_queue_segment: tx_head is not NULL\n");
        
        // Check tx_tail validity before using it
        volatile tx_seg_t *volatile_tx_tail = s->tx_tail;
        serial_write("[TCP] tcp_queue_segment: loaded tx_tail into volatile\n");
        
        if (volatile_tx_tail == NULL) {
            serial_write("[TCP] ERROR: Inconsistent queue state\n");
            kfree(seg->data);
            kfree(seg);
            return -1;
        }
        
        serial_write("[TCP] tcp_queue_segment: about to set tx_tail->next\n");
        s->tx_tail->next = seg;
        serial_write("[TCP] tcp_queue_segment: set tx_tail->next\n");
        s->tx_tail = seg;
        serial_write("[TCP] tcp_queue_segment: updated tx_tail\n");
        serial_write("[TCP] tcp_queue_segment: added to queue tail\n");
    }
    
    if (plen > 0) {
        serial_write("[TCP] tcp_queue_segment: adding RTT sample\n");
        tcp_add_rtt_sample(s, seq);
    }
    
    serial_write("[TCP] tcp_queue_segment: completed successfully\n");
    return 0;
}

static void tcp_send_queued(tcp_socket_t *s) {
    uint32_t in_flight = 0;
    tx_seg_t *t = s->tx_head;
    
    /* Calculate bytes in flight */
    while (t) {
        if (t->ts_sent > 0) {
            in_flight += t->len - sizeof(struct tcp_header);
        }
        t = t->next;
    }
    
    /* Send new segments within congestion window */
    t = s->tx_head;
    while (t && in_flight < s->cwnd && in_flight < s->snd_wnd) {
        if (t->ts_sent == 0) {
            net_ipv4_send(s->raddr, 6, t->data, (int)t->len);
            t->ts_sent = kernel_uptime_ms();
            in_flight += t->len - sizeof(struct tcp_header);
        }
        t = t->next;
    }
}

/* push data from socket send_buf into segments */
static void tcp_segment_from_sendbuf(tcp_socket_t *s) {
    while (s->send_buf_head != s->send_buf_tail) {
        uint32_t avail = (s->send_buf_head <= s->send_buf_tail) ? 
            (SEND_BUF_SIZE - s->send_buf_tail) :
            (SEND_BUF_SIZE - s->send_buf_head);
        uint32_t chunk = avail;
        if (chunk > TCP_MSS) chunk = TCP_MSS;
        if (chunk == 0) break;

        uint8_t* tmp_payload = kmalloc(chunk);
        if (!tmp_payload) {
            break; /* out of memory */
        }
        memcpy(tmp_payload, s->send_buf + s->send_buf_head, chunk);

        int ret = tcp_queue_segment(s, s->snd_nxt, TCP_PSH | TCP_ACK, tmp_payload, chunk);
        kfree(tmp_payload);

        if (ret == 0) {
            s->send_buf_head = (s->send_buf_head + chunk) % SEND_BUF_SIZE;
            s->snd_nxt += chunk;
        } else {
            break; /* failed to queue, will retry later */
        }
    }
    tcp_send_queued(s);
}

static void tcp_acknowledge(tcp_socket_t *s, uint32_t ack) {
    uint32_t old_una = s->snd_una;
    int is_duplicate = (ack == old_una);
    
    while (s->tx_head && ( (s->tx_head->seq + s->tx_head->len - sizeof(struct tcp_header)) <= ack )) {
        tx_seg_t *t = s->tx_head;
        s->tx_head = t->next;
        if (!s->tx_head) s->tx_tail = NULL;
        if (t->data) kfree(t->data);
        kfree(t);
    }
    
    if (ack > s->snd_una) {
        s->snd_una = ack;
        is_duplicate = 0;
    }
    
    tcp_process_ack_congestion_control(s, ack, is_duplicate);
}

/* insert rx segment into ordered rx queue */
static void rx_insert_ordered(tcp_socket_t *s, uint32_t seq, const uint8_t *data, uint32_t len) {
    /* ignore duplicates or already-acked data */
    if (seq + len <= s->rcv_nxt) return;
    rx_seg_t **pp = &s->rx_head;
    while (*pp && (*pp)->seq < seq) pp = &(*pp)->next;
    if (*pp && (*pp)->seq == seq) return; /* duplicate */
    rx_seg_t *r = kmalloc(sizeof(rx_seg_t));
    r->seq = seq;
    r->len = len;
    r->data = kmalloc(len);
    memcpy(r->data, data, len);
    r->next = *pp;
    *pp = r;

    /* now try to merge/advance recv buffer */
    while (s->rx_head && s->rx_head->seq == s->rcv_nxt) {
        rx_seg_t *m = s->rx_head;
        s->rx_head = m->next;
        /* copy into user recv_buf */
        uint32_t free_space = (s->recv_buf_head <= s->recv_buf_tail) ?
            (RECV_BUF_SIZE - s->recv_buf_tail) : (RECV_BUF_SIZE - s->recv_buf_head - 1);
        uint32_t copy_len = m->len;
        if (copy_len > free_space) copy_len = free_space;
        memcpy(s->recv_buf + s->recv_buf_tail, m->data, copy_len);
        s->recv_buf_tail = (s->recv_buf_tail + copy_len) % RECV_BUF_SIZE;
        s->rcv_nxt += m->len;
        kfree(m->data);
        kfree(m);
    }
    /* wake up any readers */
    scheduler_wakeup(s->wait_recv);
}

/* process incoming TCP packet */
void tcp_input_ipv4(const uint8_t *ip_hdr, int ip_hdr_len, const uint8_t *tcp_pkt, int tcp_len) {
    (void)ip_hdr_len;
    if (tcp_len < (int)sizeof(struct tcp_header)) return;
    
    const struct tcp_header *th = (const struct tcp_header *)tcp_pkt;
    uint16_t srcp = ntohs(th->src);
    uint16_t dstp = ntohs(th->dst);
    uint32_t seq = ntohl(th->seq);
    uint32_t ack = ntohl(th->ack);
    uint8_t flags = th->flags;
    uint16_t window = ntohs(th->window);
    const uint8_t *payload = tcp_pkt + sizeof(*th);
    int payload_len = tcp_len - sizeof(*th);

    serial_write("[TCP] tcp_input_ipv4: received packet from port ");
    serial_write_dec("", srcp);
    serial_write_dec(" to port ", dstp);
    serial_write_hex(" flags=", flags);
    serial_write("\n");

    /* find socket matching 4-tuple */
    tcp_socket_t *s = NULL;
    for (tcp_socket_t *c = conn_list; c; c = c->next) {
        if (!c->used) continue;
        if (c->lport == dstp) {
            /* Check for listening socket or established connection */
            if ((c->rport == 0 && (flags & TCP_SYN) && c->state == TCP_LISTEN) ||
                (memcmp(c->raddr, ip_hdr + 12, 4) == 0 && c->rport == srcp)) {
                s = c; 
                serial_write("[TCP] Found matching socket in state ");
                serial_write_dec("", s->state);
                serial_write("\n");
                break;
            }
        }
    }

    if (!s) {
        serial_write("[TCP] No matching socket found, sending RST\n");
        /* No socket - send RST in response to SYN/any other non-RST */
        if (!(flags & TCP_RST)) {
            uint8_t rst_buf[sizeof(struct tcp_header)];
            struct tcp_header *rth = (struct tcp_header *)rst_buf;
            memset(rst_buf, 0, sizeof(rst_buf));
            rth->src = htons(dstp);
            rth->dst = htons(srcp);
            rth->seq = htonl(0);
            rth->ack = htonl(seq + payload_len + ((flags & TCP_SYN) ? 1 : 0));
            rth->off_reserved = (sizeof(struct tcp_header)/4)<<4;
            rth->flags = TCP_RST | TCP_ACK;
            rth->window = 0;
            rth->checksum = 0;
            net_ipv4_send((uint8_t *)(ip_hdr+12), 6, rst_buf, sizeof(rst_buf));
        }
        return;
    }

    s->snd_wnd = window;
    if (window > s->max_window) s->max_window = window;

    /* Handle based on state */
    if (s->state == TCP_LISTEN && (flags & TCP_SYN)) {
        serial_write("[TCP] Handling SYN in LISTEN state\n");
        /* passive open: create new child socket for connection handshake */
        tcp_socket_t *child = alloc_socket();
        if (!child) { 
            serial_write("[TCP] ERROR: cannot allocate child for incoming connection\n"); 
            return; 
        }
        memcpy(child->laddr, s->laddr, 4);
        child->lport = s->lport;
        memcpy(child->raddr, ip_hdr + 12, 4);
        child->rport = srcp;
        child->iss = (uint32_t)(kernel_uptime_ms() & 0xffff);
        child->snd_una = child->iss;
        child->snd_nxt = child->iss + 1;
        child->rcv_nxt = seq + 1;
        child->state = TCP_SYN_RECEIVED;
        child->snd_wnd = window;

        /* enqueue into parent's accept backlog */
        if (s->pending_count < s->backlog) {
            s->pending[s->pending_count++] = child;
            conn_list_add(child);
            /* send SYN+ACK */
            tcp_queue_segment(child, child->iss, TCP_SYN | TCP_ACK, NULL, 0);
            tcp_send_queued(child);
            scheduler_wakeup(s->wait_accept);
            serial_write("[TCP] Sent SYN+ACK response\n");
        } else {
            /* backlog full: drop / ignore */
            free_socket_struct(child);
            serial_write("[TCP] Accept backlog full, dropping connection\n");
        }
        return;
    }

    /* Handle SYN-ACK response for outgoing connections */
    if (s->state == TCP_SYN_SENT && (flags & (TCP_SYN | TCP_ACK)) == (TCP_SYN | TCP_ACK)) {
        serial_write("[TCP] Received SYN-ACK in SYN_SENT state\n");
        if (ack == s->snd_nxt) {
            s->rcv_nxt = seq + 1;
            s->snd_una = ack;
            s->state = TCP_ESTABLISHED;
            s->snd_wnd = window;
            
            /* Send ACK to complete handshake */
            tcp_queue_segment(s, s->snd_nxt, TCP_ACK, NULL, 0);
            tcp_send_queued(s);
            
            scheduler_wakeup(s->wait_send);
            serial_write("[TCP] Connection established, sent final ACK\n");
        } else {
            serial_write("[TCP] Invalid ACK number in SYN-ACK\n");
        }
        return;
    }

    /* For other states, handle ACK/FIN/PSH */
    if (flags & TCP_ACK) {
        serial_write("[TCP] Processing ACK in state ");
        serial_write_dec("", s->state);
        serial_write("\n");
        
        tcp_acknowledge(s, ack);
        
        /* advance connection state */
        if (s->state == TCP_SYN_RECEIVED && ack >= s->snd_nxt) {
            s->state = TCP_ESTABLISHED;
            scheduler_wakeup(s->wait_accept);
            serial_write("[TCP] Connection established from SYN_RECEIVED\n");
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

    /* Handle data payload */
    if (payload_len > 0 && (s->state == TCP_ESTABLISHED || s->state == TCP_FIN_WAIT_1 || s->state == TCP_FIN_WAIT_2)) {
        serial_write("[TCP] Received ");
        serial_write_dec("", payload_len);
        serial_write(" bytes of data\n");
        
        rx_insert_ordered(s, seq, payload, payload_len);

        /* Send ACK for received data */
        tcp_queue_segment(s, s->snd_nxt, TCP_ACK, NULL, 0);
        tcp_send_queued(s);
    }

    /* Handle FIN */
    if (flags & TCP_FIN) {
        serial_write("[TCP] Received FIN\n");
        s->rcv_nxt++;
        if (s->state == TCP_ESTABLISHED) {
            s->state = TCP_CLOSE_WAIT;
            /* Send ACK for FIN */
            tcp_queue_segment(s, s->snd_nxt, TCP_ACK, NULL, 0);
            tcp_send_queued(s);
        }
    }
}

/* send new data if any */
static void tcp_send_new_data(tcp_socket_t *s) {
    if (s->state == TCP_ESTABLISHED) {
        tcp_segment_from_sendbuf(s);
    }
}

/* TIME_WAIT expiry handling */
static void tcp_handle_timewait_expiry(tcp_socket_t *s) {
    if (s->state == TCP_TIME_WAIT && kernel_uptime_ms() >= s->timewait_expires) {
        s->state = TCP_CLOSED;
        conn_list_remove(s);
        free_socket_struct(s);
    }
}

/* socket API: map to simple fd (index) */
int sock_socket(void) {
    serial_write("[TCP] sock_socket: entered\n");
    tcp_socket_t *s = alloc_socket();
    if (!s) {
        serial_write("[TCP] sock_socket: alloc_socket failed.\n");
        return -1;
    }
    serial_write("[TCP] sock_socket: alloc_socket OK.\n");
    /* default local wildcard; caller must bind */
    s->state = TCP_CLOSED;
    conn_list_add(s);
    serial_write("[TCP] sock_socket: returning socket descriptor.\n");
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
    if (!ip) {
        return -1;
    }
    memcpy(s->raddr, ip, 4);
    s->rport = port;
    if (s->lport == 0) s->lport = (uint16_t)(1024 + (kernel_uptime_ms() % 40000));
    s->iss = (uint32_t)(kernel_uptime_ms() & 0xffff);
    serial_write("[TCP] sock_connect: iss OK, ");
    s->snd_una = s->iss;
    serial_write("snd_una OK, ");
    s->snd_nxt = s->iss + 1;
    serial_write("snd_nxt OK, ");
    s->rcv_nxt = 0;
    serial_write("rcv_nxt OK");
    s->state = TCP_SYN_SENT;
    /* send SYN */
    serial_write("[TCP] sock_connect: sending SYN\n");
    tcp_queue_segment(s, s->iss, TCP_SYN, NULL, 0);
    serial_write("[TCP] sock_connect: queued SYN\n");
    tcp_send_queued(s);
    serial_write("[TCP] sock_connect: sent SYN\n");
    
    uint64_t start = kernel_uptime_ms();
    uint64_t last_debug = start;
    while (s->state != TCP_ESTABLISHED) {
        uint64_t now = kernel_uptime_ms();
        
        // Debug connection state every 1000ms
        if (now - last_debug >= 1000) {
            serial_write("[TCP] sock_connect: waiting for connection, state=");
            serial_write_hex("", s->state);
            serial_write(", elapsed=");
            serial_write_hex("", now - start);
            serial_write("ms\n");
            last_debug = now;
        }
        
        // Shorter timeout for testing - 3 seconds instead of 5
        if (now - start > 3000) {
            serial_write("[TCP] sock_connect: connection timeout\n");
            return -1;
        }
        
        scheduler_sleep(s->wait_send);
    }
    
    serial_write("[TCP] sock_connect: connection established successfully\n");
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
            (SEND_BUF_SIZE - s->send_buf_head);
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

/* periodic timer function, called every 100ms */
static void tcp_timer_fn(void) {
    tcp_socket_t *s = conn_list;
    while (s) {
        tcp_socket_t *next_s = s->next;
        int closed = 0;

        if (s->used) {
            /* Handle retransmissions */
            tx_seg_t *t = s->tx_head;
            if (t && t->ts_sent > 0 && (kernel_uptime_ms() - t->ts_sent) > t->rto_ms) {
                if (t->retries < TCP_RETRANSMIT_MAX) {
                    kernel_log("tcp: retransmitting seg seq=%u (rto=%u)\n", t->seq, t->rto_ms);
                    net_ipv4_send(s->raddr, 6, t->data, (int)t->len);
                    t->ts_sent = kernel_uptime_ms();
                    t->retries++;
                    s->rto *= 2; /* exponential backoff */
                    if (s->rto > TCP_RTO_MAX) s->rto = TCP_RTO_MAX;
                    t->rto_ms = s->rto;
                } else {
                    kernel_log("tcp: connection timeout, max retries reached\n");
                    s->state = TCP_CLOSED;
                    conn_list_remove(s);
                    free_socket_struct(s);
                    closed = 1;
                }
            }

            /* Handle TIME_WAIT expiry */
            if (!closed && s->state == TCP_TIME_WAIT) {
                tcp_handle_timewait_expiry(s);
            }

            /* Try to send new data from buffer */
            if (!closed) {
                tcp_send_new_data(s);
            }
        }
        s = next_s;
    }
}

void tcp_dump_pcbs(void) {
    kernel_log("Active TCP connections:\n");
    for (tcp_socket_t *s = conn_list; s; s = s->next) {
        if (!s->used) continue;
        kernel_log("lport=%u rport=%u state=%d cwnd=%u ssthresh=%u rto=%u\n",
            s->lport, s->rport, s->state, s->cwnd, s->ssthresh, s->rto);
    }
}

/* init */
int tcp_init(void) {
    memset(sockets, 0, sizeof(sockets));
    timer_register_periodic(tcp_timer_fn, 100); /* 100ms tick */
    return 0;
}
