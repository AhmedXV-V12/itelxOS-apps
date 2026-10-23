/* IPv4 and ICMP (echo). No fragments: itelx sends whole packets (DF) and
 * drops fragments it receives. */
#include "lib.h"
#include "arch.h"
#include "errno.h"
#include "proc.h"
#include "time.h"
#include "net.h"

static u16 ip_id;

u16 ip_checksum(const void *data, u32 len, u32 sum) {
    const u8 *p = data;
    while (len > 1) { sum += (u32)(p[0] << 8 | p[1]); p += 2; len -= 2; }
    if (len) sum += (u32)(p[0] << 8);
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (u16)~sum;
}

int ip_send_if(struct netif *n, u32 src, u32 dst, u8 proto, const void *payload, u32 len, u32 wait_ms) {
    static u8 pkt[NET_MTU];
    if (len + 20 > NET_MTU) return -E2BIG;
    u32 hop = dst;
    u8 mac[6];
    if (dst == 0xFFFFFFFF || (n->mask && dst == (n->ip | ~n->mask))) {
        memset(mac, 0xFF, 6);
    } else {
        if (n->mask && (dst & n->mask) != (n->ip & n->mask)) hop = n->gw;
        if (!hop) return -ENETUNREACH;
        if (!arp_lookup(hop, mac)) {
            if (!wait_ms) { arp_request(n, hop); return -EHOSTUNREACH; }
            if (!arp_resolve(n, hop, mac, wait_ms)) return -EHOSTUNREACH;
        }
    }
    pkt[0] = 0x45;
    pkt[1] = 0;
    wr16(pkt + 2, (u16)(len + 20));
    wr16(pkt + 4, ++ip_id);
    wr16(pkt + 6, 0x4000);                   /* don't fragment */
    pkt[8] = 64;
    pkt[9] = proto;
    wr16(pkt + 10, 0);
    wr32(pkt + 12, src);
    wr32(pkt + 16, dst);
    wr16(pkt + 10, ip_checksum(pkt, 20, 0));
    memcpy(pkt + 20, payload, len);
    int r = net_send_eth(n, mac, 0x0800, pkt, len + 20);
    net_kick();
    return r;
}

int ip_send(u32 src, u32 dst, u8 proto, const void *payload, u32 len, u32 wait_ms) {
    u32 hop;
    struct netif *n = net_route(dst, &hop);
    if (!n) return g_nnetif ? -ENETUNREACH : -ENETDOWN;
    return ip_send_if(n, src ? src : n->ip, dst, proto, payload, len, wait_ms);
}

/* ------------------------------------------------------------- ICMP */

static struct wait_queue ping_wq;
static bool ping_ready;
static volatile u16 ping_got_seq;
static volatile u32 ping_got_from;
static volatile u64 ping_got_at;

static void icmp_input(struct netif *n, u32 src, u32 dst, const u8 *p, u32 len) {
    if (len < 8 || ip_checksum(p, len, 0)) return;
    if (p[0] == 8) {                          /* echo request: the reply carries the same data */
        static u8 r[NET_MTU];
        memcpy(r, p, len);
        r[0] = 0;
        wr16(r + 2, 0);
        wr16(r + 2, ip_checksum(r, len, 0));
        ip_send_if(n, n->ip, src, 1, r, len, 0);
    } else if (p[0] == 0 && rd16(p + 4) == 0x1E7E) {   /* a reply to our ping */
        ping_got_seq = rd16(p + 6);
        ping_got_from = src;
        ping_got_at = rdtsc();
        if (ping_ready) wake_up_all(&ping_wq);
    }
}

extern u64 g_tsc_per_us;

int icmp_ping(u32 ip, u32 timeout_ms, u16 seq) {
    if (!ping_ready) { wq_init(&ping_wq); ping_ready = true; }
    u8 p[64];
    memset(p, 0, sizeof p);
    p[0] = 8;
    wr16(p + 4, 0x1E7E);
    wr16(p + 6, seq);
    for (int i = 8; i < 64; i++) p[i] = (u8)i;
    wr16(p + 2, ip_checksum(p, sizeof p, 0));
    ping_got_seq = 0;
    u64 t0 = rdtsc(), ms0 = net_ms();
    int r = ip_send(0, ip, 1, p, sizeof p, timeout_ms);
    if (r) return r;
    while (net_ms() - ms0 < timeout_ms) {
        if (ping_got_seq == seq && ping_got_from == ip) {
            return (int)((ping_got_at - t0) / (g_tsc_per_us ? g_tsc_per_us : 1000));
        }
        u64 f = irq_save();
        current->wake_tick = g_ticks + 1;
        sleep_on(&ping_wq);
        irq_restore(f);
        if (current->killed) return -EINTR;
    }
    return -ETIMEDOUT;
}

/* ---------------------------------------------------------- receive */

void ip_input(struct netif *n, const u8 *p, u32 len) {
    if (len < 20 || (p[0] >> 4) != 4) return;
    u32 hl = (u32)(p[0] & 15) * 4, total = rd16(p + 2);
    if (hl < 20 || total < hl || total > len || ip_checksum(p, hl, 0)) { n->rx_dropped++; return; }
    if ((rd16(p + 6) & 0x3FFF) != 0) { n->rx_dropped++; return; }   /* a fragment */
    u32 src = rd32(p + 12), dst = rd32(p + 16);
    bool mine = n->up && dst == n->ip;
    bool bcast = dst == 0xFFFFFFFF || (n->mask && dst == (n->ip | ~n->mask));
    const u8 *pl = p + hl;
    u32 plen = total - hl;
    switch (p[9]) {
    case 1: if (mine) icmp_input(n, src, dst, pl, plen); break;
    case 17: if (mine || bcast || !n->up) udp_input(n, src, dst, pl, plen); break;
    case 6: if (mine) tcp_input(n, src, dst, pl, plen); break;
    }
}
