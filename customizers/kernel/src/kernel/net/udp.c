/* UDP: datagrams to and from the sockets of programs, and DHCP. */
#include "lib.h"
#include "errno.h"
#include "proc.h"
#include "net.h"

static u16 udp_sum(u32 src, u32 dst, const u8 *seg, u32 len) {
    u8 ph[12];
    wr32(ph, src);
    wr32(ph + 4, dst);
    ph[8] = 0;
    ph[9] = 17;
    wr16(ph + 10, (u16)len);
    u32 s = 0;
    for (int i = 0; i < 12; i += 2) s += (u32)(ph[i] << 8 | ph[i + 1]);
    u16 c = ip_checksum(seg, len, s);
    return c ? c : 0xFFFF;
}

int udp_send(struct netif *n, u32 src, u16 sport, u32 dst, u16 dport, const void *data, u32 len, u32 wait_ms) {
    static u8 seg[NET_MTU];
    if (len + 28 > NET_MTU) return -E2BIG;
    wr16(seg, sport);
    wr16(seg + 2, dport);
    wr16(seg + 4, (u16)(len + 8));
    wr16(seg + 6, 0);
    memcpy(seg + 8, data, len);
    wr16(seg + 6, udp_sum(src, dst, seg, len + 8));
    if (n) return ip_send_if(n, src, dst, 17, seg, len + 8, wait_ms);
    return ip_send(src, dst, 17, seg, len + 8, wait_ms);
}

void udp_input(struct netif *n, u32 src, u32 dst, const u8 *p, u32 len) {
    if (len < 8) return;
    u32 ulen = rd16(p + 4);
    if (ulen < 8 || ulen > len) return;
    if (rd16(p + 6)) {
        /* a checksum is given: with the pseudo header it has to add up */
        u8 ph[12];
        wr32(ph, src);
        wr32(ph + 4, dst);
        ph[8] = 0;
        ph[9] = 17;
        wr16(ph + 10, (u16)ulen);
        u32 s = 0;
        for (int i = 0; i < 12; i += 2) s += (u32)(ph[i] << 8 | ph[i + 1]);
        if (ip_checksum(p, ulen, s)) { n->rx_dropped++; return; }
    }
    u16 sport = rd16(p), dport = rd16(p + 2);
    if (dport == 68 && dhcp_input(n, p + 8, ulen - 8)) return;
    for (int i = 0; i < MAX_SOCKS; i++) {
        struct sock *s = &g_socks[i];
        if (!s->used || s->type != SOCK_UDP || s->lport != dport) continue;
        if (s->dq_n >= UDP_QUEUE) { n->rx_dropped++; return; }
        struct dgram *d = &s->dq[(s->dq_head + s->dq_n) % UDP_QUEUE];
        d->from_ip = src;
        d->from_port = sport;
        d->len = (u16)(ulen - 8);
        memcpy(d->data, p + 8, ulen - 8);
        s->dq_n++;
        wake_up_all(&s->wq);
        return;
    }
}
