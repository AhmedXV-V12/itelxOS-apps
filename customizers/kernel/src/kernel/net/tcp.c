/* TCP (RFC 793, 1122): connections for programs, both ways.
 *
 * Kept simple and correct rather than fast: segments that arrive out of
 * order are dropped (the sender sends them again), every segment with data
 * is acknowledged at once, and a lost segment brings back everything after
 * it (go-back-N) after a retransmission timeout that doubles up to 8
 * seconds; eight timeouts in a row end the connection. Windows are 64 KiB,
 * the segment size is what the peer says (1460 on Ethernet). */
#include "lib.h"
#include "arch.h"
#include "mm.h"
#include "errno.h"
#include "proc.h"
#include "net.h"

#define F_FIN 0x01
#define F_SYN 0x02
#define F_RST 0x04
#define F_PSH 0x08
#define F_ACK 0x10

#define RTO_MIN   400
#define RTO_MAX   8000
#define MAX_TRIES 8

/* sequence numbers wrap: compare them as differences */
#define SEQ_LT(a, b) ((i32)((a) - (b)) < 0)
#define SEQ_LE(a, b) ((i32)((a) - (b)) <= 0)

static u16 tcp_sum(u32 src, u32 dst, const u8 *seg, u32 len) {
    u8 ph[12];
    wr32(ph, src);
    wr32(ph + 4, dst);
    ph[8] = 0;
    ph[9] = 6;
    wr16(ph + 10, (u16)len);
    u32 s = 0;
    for (int i = 0; i < 12; i += 2) s += (u32)(ph[i] << 8 | ph[i + 1]);
    return ip_checksum(seg, len, s);
}

static u32 window_of(struct sock *s) {
    u32 room = s->rbuf ? SOCK_BUF - s->rlen : 0;
    return room > 65535 ? 65535 : room;
}

static int output(u32 src, u32 dst, u16 sport, u16 dport, u32 seq, u32 ack, u8 flags, u16 wnd, const u8 *data, u32 len, bool mss_opt) {
    static u8 seg[NET_MTU];
    u32 hl = mss_opt ? 24 : 20;
    if (hl + len + 20 > NET_MTU) return -E2BIG;
    wr16(seg, sport);
    wr16(seg + 2, dport);
    wr32(seg + 4, seq);
    wr32(seg + 8, ack);
    seg[12] = (u8)(hl / 4) << 4;
    seg[13] = flags;
    wr16(seg + 14, wnd);
    wr16(seg + 16, 0);
    wr16(seg + 18, 0);
    if (mss_opt) { seg[20] = 2; seg[21] = 4; wr16(seg + 22, NET_MTU - 40); }
    if (len) memcpy(seg + hl, data, len);
    wr16(seg + 16, tcp_sum(src, dst, seg, hl + len));
    return ip_send(src, dst, 6, seg, hl + len, 0);
}

static void send_flags(struct sock *s, u8 flags) {
    bool syn = flags & F_SYN;
    output(s->lip, s->rip, s->lport, s->rport, syn ? s->iss : s->snd_nxt, s->rcv_nxt, flags, (u16)window_of(s), NULL, 0, syn);
}

/* the SYN of a connect(), with our segment size */
void tcp_send_syn(struct sock *s) { send_flags(s, F_SYN); }

/* the window opened after a program read: the peer may send again */
void tcp_window_update(struct sock *s) { send_flags(s, F_ACK); }

static void arm(struct sock *s) {
    if (!s->rto_ms) s->rto_ms = 500;
    s->rto_at = net_ms() + s->rto_ms;
}

/* sends what the window allows from the send ring, then the FIN */
int tcp_send_more(struct sock *s) {
    if (s->state != SOCKS_ESTABLISHED && s->state != SOCKS_CLOSE_WAIT && s->state != SOCKS_FIN_WAIT1 &&
        s->state != SOCKS_LAST_ACK && s->state != SOCKS_CLOSING)
        return 0;
    u32 flight = s->snd_nxt - s->snd_una;
    if (s->fin_sent) flight--;                         /* the FIN takes a sequence number, no byte */
    u32 wnd = s->snd_wnd ? s->snd_wnd : 1;             /* a closed window: a byte to probe it */
    if (wnd > 32u * s->mss) wnd = 32u * s->mss;
    int sent = 0;
    while (!s->fin_sent && flight < s->slen && flight < wnd) {
        u32 n = s->slen - flight;
        if (n > s->mss) n = s->mss;
        if (n > wnd - flight) n = wnd - flight;
        static u8 chunk[NET_MTU];
        u32 at = (s->shead + flight) % SOCK_BUF;
        u32 first = SOCK_BUF - at < n ? SOCK_BUF - at : n;
        memcpy(chunk, s->sbuf + at, first);
        if (first < n) memcpy(chunk + first, s->sbuf, n - first);
        if (output(s->lip, s->rip, s->lport, s->rport, s->snd_nxt, s->rcv_nxt, F_ACK | F_PSH, (u16)window_of(s), chunk, n, false) < 0)
            break;
        s->snd_nxt += n;
        flight += n;
        sent++;
        if (!s->rto_at) arm(s);
    }
    if (s->close_req && !s->fin_sent && flight == s->slen) {
        send_flags(s, F_FIN | F_ACK);
        s->snd_nxt++;
        s->fin_sent = true;
        if (s->state == SOCKS_ESTABLISHED) s->state = SOCKS_FIN_WAIT1;
        else if (s->state == SOCKS_CLOSE_WAIT) s->state = SOCKS_LAST_ACK;
        if (!s->rto_at) arm(s);
    }
    return sent;
}

static void finish(struct sock *s, int err) {
    s->err = err;
    s->state = SOCKS_CLOSED;
    s->rto_at = 0;
    wake_up_all(&s->wq);
    if (s->parent) wake_up_all(&s->parent->wq);
    if (!s->owner && !s->parent) sock_free(s);        /* nobody holds it any more */
}

/* RST for a segment nobody wants */
static void reset_for(u32 src, u32 dst, const u8 *p, u32 seglen) {
    u8 flags = p[13];
    if (flags & F_RST) return;
    u16 sport = rd16(p), dport = rd16(p + 2);
    if (flags & F_ACK) output(dst, src, dport, sport, rd32(p + 8), 0, F_RST, 0, NULL, 0, false);
    else {
        u32 ack = rd32(p + 4) + seglen + ((flags & F_SYN) ? 1 : 0) + ((flags & F_FIN) ? 1 : 0);
        output(dst, src, dport, sport, 0, ack, F_RST | F_ACK, 0, NULL, 0, false);
    }
}

void tcp_input(struct netif *n, u32 src, u32 dst, const u8 *p, u32 len) {
    if (len < 20 || tcp_sum(src, dst, p, len)) { n->rx_dropped++; return; }
    u16 sport = rd16(p), dport = rd16(p + 2);
    u32 seq = rd32(p + 4), ack = rd32(p + 8), off = (u32)(p[12] >> 4) * 4;
    u8 flags = p[13];
    u32 wnd = rd16(p + 14);
    if (off < 20 || off > len) return;
    const u8 *data = p + off;
    u32 dlen = len - off;
    struct sock *s = NULL, *listener = NULL;
    for (int i = 0; i < MAX_SOCKS; i++) {
        struct sock *x = &g_socks[i];
        if (!x->used || x->type != SOCK_TCP || x->lport != dport) continue;
        if (x->state == SOCKS_LISTEN) { listener = x; continue; }
        if (x->rport == sport && x->rip == src && x->state != SOCKS_CLOSED) { s = x; break; }
    }
    if (!s && listener && (flags & F_SYN) && !(flags & (F_ACK | F_RST))) {
        /* a new connection for a listener with room in its queue */
        int pending = 0;
        for (int i = 0; i < MAX_SOCKS; i++) if (g_socks[i].used && g_socks[i].parent == listener) pending++;
        if (pending >= listener->backlog) return;
        struct sock *c = sock_alloc(SOCK_TCP, listener->owner);
        if (!c) return;
        c->parent = listener;
        c->owner = 0;
        c->lip = dst;
        c->rip = src;
        c->lport = dport;
        c->rport = sport;
        c->irs = seq;
        c->rcv_nxt = seq + 1;
        c->iss = (u32)rdtsc();
        c->snd_una = c->iss;
        c->snd_nxt = c->iss + 1;
        c->snd_wnd = wnd;
        c->mss = 536;
        for (u32 k = 20; k + 1 < off;) {               /* the MSS option */
            u8 kind = p[k];
            if (kind == 0) break;
            if (kind == 1) { k++; continue; }
            u8 l = p[k + 1];
            if (l < 2 || k + l > off) break;
            if (kind == 2 && l == 4) c->mss = rd16(p + k + 2);
            k += l;
        }
        if (c->mss > NET_MTU - 40) c->mss = NET_MTU - 40;
        c->state = SOCKS_SYN_RCVD;
        send_flags(c, F_SYN | F_ACK);
        arm(c);
        return;
    }
    if (!s) { reset_for(src, dst, p, dlen); return; }

    if (s->state == SOCKS_SYN_SENT) {
        if ((flags & F_ACK) && ack != s->iss + 1) { if (!(flags & F_RST)) reset_for(src, dst, p, dlen); return; }
        if (flags & F_RST) { if (flags & F_ACK) finish(s, -ECONNREFUSED); return; }
        if (!(flags & F_SYN)) return;
        s->irs = seq;
        s->rcv_nxt = seq + 1;
        for (u32 k = 20; k + 1 < off;) {
            u8 kind = p[k];
            if (kind == 0) break;
            if (kind == 1) { k++; continue; }
            u8 l = p[k + 1];
            if (l < 2 || k + l > off) break;
            if (kind == 2 && l == 4) s->mss = rd16(p + k + 2);
            k += l;
        }
        if (s->mss > NET_MTU - 40 || !s->mss) s->mss = NET_MTU - 40;
        if (flags & F_ACK) {
            s->snd_una = ack;
            s->snd_wnd = wnd;
            s->state = SOCKS_ESTABLISHED;
            s->rto_at = 0;
            s->retries = 0;
            send_flags(s, F_ACK);
            wake_up_all(&s->wq);
        }
        return;
    }

    if (flags & F_RST) {
        if (SEQ_LE(s->rcv_nxt, seq) && SEQ_LT(seq, s->rcv_nxt + window_of(s) + 1)) finish(s, -ECONNRESET);
        return;
    }
    if (flags & F_SYN) {                               /* a SYN again: our ACK was lost */
        if (s->state == SOCKS_SYN_RCVD) send_flags(s, F_SYN | F_ACK);
        else send_flags(s, F_ACK);
        return;
    }
    if (!(flags & F_ACK)) return;

    /* what our peer acknowledges */
    if (s->state == SOCKS_SYN_RCVD) {
        if (ack != s->iss + 1) { reset_for(src, dst, p, dlen); return; }
        s->snd_una = ack;
        s->snd_wnd = wnd;
        s->state = SOCKS_ESTABLISHED;
        s->rto_at = 0;
        s->retries = 0;
        if (s->parent) { s->parent->nready++; wake_up_all(&s->parent->wq); }
    } else if (SEQ_LT(s->snd_una, ack) && SEQ_LE(ack, s->snd_nxt)) {
        u32 acked = ack - s->snd_una;
        if (s->fin_sent && ack == s->snd_nxt) { s->fin_acked = true; acked--; }
        if (acked > s->slen) acked = s->slen;
        s->shead = (s->shead + acked) % SOCK_BUF;
        s->slen -= acked;
        s->snd_una = ack;
        s->retries = 0;
        s->rto_ms = RTO_MIN;
        s->rto_at = s->snd_una == s->snd_nxt ? 0 : net_ms() + s->rto_ms;
        wake_up_all(&s->wq);
        if (s->fin_acked) {
            if (s->state == SOCKS_FIN_WAIT1) s->state = SOCKS_FIN_WAIT2;
            else if (s->state == SOCKS_CLOSING) { s->state = SOCKS_TIME_WAIT; s->tw_until = net_ms() + 2000; }
            else if (s->state == SOCKS_LAST_ACK) { finish(s, 0); return; }
        }
    }
    s->snd_wnd = wnd;

    /* data, in order only */
    bool ack_now = false;
    if (dlen) {
        if (seq == s->rcv_nxt && !s->fin_rcvd && s->rbuf) {
            u32 room = SOCK_BUF - s->rlen, take = dlen < room ? dlen : room;
            u32 at = (s->rhead + s->rlen) % SOCK_BUF;
            u32 first = SOCK_BUF - at < take ? SOCK_BUF - at : take;
            memcpy(s->rbuf + at, data, first);
            if (first < take) memcpy(s->rbuf, data + first, take - first);
            s->rlen += take;
            s->rcv_nxt += take;
            wake_up_all(&s->wq);
            if (s->parent) wake_up_all(&s->parent->wq);
        }
        ack_now = true;                                /* in order or not: say where we are */
    }
    if ((flags & F_FIN) && seq + dlen == s->rcv_nxt && !s->fin_rcvd) {
        s->rcv_nxt++;
        s->fin_rcvd = true;
        ack_now = true;
        if (s->state == SOCKS_ESTABLISHED || s->state == SOCKS_SYN_RCVD) s->state = SOCKS_CLOSE_WAIT;
        else if (s->state == SOCKS_FIN_WAIT1) {
            if (s->fin_acked) { s->state = SOCKS_TIME_WAIT; s->tw_until = net_ms() + 2000; }
            else s->state = SOCKS_CLOSING;
        } else if (s->state == SOCKS_FIN_WAIT2) { s->state = SOCKS_TIME_WAIT; s->tw_until = net_ms() + 2000; }
        wake_up_all(&s->wq);
        if (s->parent) wake_up_all(&s->parent->wq);
    }
    if (ack_now) send_flags(s, F_ACK);
    tcp_send_more(s);
}

void tcp_timer(void) {
    u64 now = net_ms();
    for (int i = 0; i < MAX_SOCKS; i++) {
        struct sock *s = &g_socks[i];
        if (!s->used || s->type != SOCK_TCP) continue;
        if (s->state == SOCKS_TIME_WAIT) {
            if (now >= s->tw_until) finish(s, 0);
            continue;
        }
        /* a connection its program let go of gets a minute to close */
        if (!s->owner && !s->parent && s->tw_until && now >= s->tw_until) {
            send_flags(s, F_RST | F_ACK);
            finish(s, 0);
            continue;
        }
        if (!s->rto_at || now < s->rto_at) continue;
        if (++s->retries > MAX_TRIES) {
            if (s->state != SOCKS_SYN_SENT && s->state != SOCKS_SYN_RCVD) send_flags(s, F_RST | F_ACK);
            if (s->state == SOCKS_SYN_RCVD && s->parent) { sock_free(s); continue; }
            finish(s, -ETIMEDOUT);
            continue;
        }
        s->rto_ms = s->rto_ms * 2 > RTO_MAX ? RTO_MAX : s->rto_ms * 2;
        s->rto_at = now + s->rto_ms;
        if (s->state == SOCKS_SYN_SENT) { send_flags(s, F_SYN); continue; }
        if (s->state == SOCKS_SYN_RCVD) { send_flags(s, F_SYN | F_ACK); continue; }
        /* go back to the first byte not acknowledged */
        s->snd_nxt = s->snd_una;
        if (s->fin_sent && !s->fin_acked) {
            s->fin_sent = false;
            if (s->state == SOCKS_FIN_WAIT1) s->state = SOCKS_ESTABLISHED;
            else if (s->state == SOCKS_LAST_ACK) s->state = SOCKS_CLOSE_WAIT;
            else if (s->state == SOCKS_CLOSING) { s->state = SOCKS_CLOSE_WAIT; }
        }
        tcp_send_more(s);
        if (!s->rto_at) arm(s);
    }
}

/* a program lets go of a connection: what is left to send still goes, then
   the FIN; a listener's waiting connections are reset */
void tcp_close_sock(struct sock *s) {
    switch (s->state) {
    case SOCKS_LISTEN:
        for (int i = 0; i < MAX_SOCKS; i++) {
            struct sock *c = &g_socks[i];
            if (c->used && c->parent == s) { send_flags(c, F_RST | F_ACK); sock_free(c); }
        }
        sock_free(s);
        return;
    case SOCKS_CLOSED:
    case SOCKS_SYN_SENT:
    case SOCKS_TIME_WAIT:
        sock_free(s);
        return;
    case SOCKS_SYN_RCVD:
    case SOCKS_ESTABLISHED:
    case SOCKS_CLOSE_WAIT:
        s->close_req = true;
        s->owner = 0;
        s->tw_until = net_ms() + 60000;
        tcp_send_more(s);
        return;
    default:                                           /* closing already: the stack finishes it */
        s->owner = 0;
        s->tw_until = net_ms() + 60000;
        return;
    }
}
