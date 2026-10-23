/* DHCP: an address, the network mask, the router and a DNS server from the
 * network (RFC 2131): DISCOVER, OFFER, REQUEST, ACK; renewed at half the
 * lease. */
#include "lib.h"
#include "arch.h"
#include "net.h"

enum { D_IDLE, D_SELECTING, D_REQUESTING, D_BOUND, D_RENEWING };

static struct dstate {
    int state, tries;
    u32 xid, server, offered;
    u64 next_ms;
} st[NET_MAX_IF];

static int index_of(struct netif *n) {
    for (int i = 0; i < g_nnetif; i++) if (g_netifs[i] == n) return i;
    return -1;
}

static void send_msg(struct netif *n, struct dstate *d, u8 type) {
    u8 p[300];
    memset(p, 0, sizeof p);
    p[0] = 1;                                   /* a request */
    p[1] = 1;                                   /* Ethernet */
    p[2] = 6;
    wr32(p + 4, d->xid);
    if (d->state == D_RENEWING) wr32(p + 12, n->ip);
    else wr16(p + 10, 0x8000);                  /* answer by broadcast: we have no address yet */
    memcpy(p + 28, n->mac, 6);
    wr32(p + 236, 0x63825363);                  /* the magic cookie */
    u8 *o = p + 240;
    *o++ = 53; *o++ = 1; *o++ = type;
    *o++ = 61; *o++ = 7; *o++ = 1; memcpy(o, n->mac, 6); o += 6;             /* client id */
    if (type == 3 && d->state == D_REQUESTING) {
        *o++ = 50; *o++ = 4; wr32(o, d->offered); o += 4;
        *o++ = 54; *o++ = 4; wr32(o, d->server); o += 4;
    }
    *o++ = 12; *o++ = 5; memcpy(o, "itelx", 5); o += 5;                     /* host name */
    *o++ = 55; *o++ = 4; *o++ = 1; *o++ = 3; *o++ = 6; *o++ = 51;           /* what we want to know */
    *o++ = 255;
    u32 len = (u32)(o - p);
    if (len < 300) len = 300;
    udp_send(n, d->state == D_RENEWING ? n->ip : 0, 68, 0xFFFFFFFF, 67, p, len, 0);
}

void dhcp_start(struct netif *n) {
    int i = index_of(n);
    if (i < 0) return;
    struct dstate *d = &st[i];
    d->state = D_SELECTING;
    d->tries = 0;
    d->xid = (u32)rdtsc() ^ ((u32)n->mac[5] << 24);
    d->next_ms = net_ms() + 1000;
    n->dhcp = true;
    send_msg(n, d, 1);                          /* DISCOVER */
}

static u32 opt32(const u8 *o) { return rd32(o); }

bool dhcp_input(struct netif *n, const u8 *p, u32 len) {
    int i = index_of(n);
    if (i < 0 || len < 240 || p[0] != 2 || rd32(p + 236) != 0x63825363) return false;
    struct dstate *d = &st[i];
    if (rd32(p + 4) != d->xid || memcmp(p + 28, n->mac, 6)) return true;
    u8 type = 0;
    u32 mask = 0, router = 0, dns = 0, lease = 3600, server = 0;
    for (u32 k = 240; k < len;) {
        u8 code = p[k];
        if (code == 255) break;
        if (code == 0) { k++; continue; }
        if (k + 1 >= len) break;
        u8 l = p[k + 1];
        const u8 *v = p + k + 2;
        if (k + 2 + l > len) break;
        if (code == 53 && l >= 1) type = v[0];
        else if (code == 1 && l >= 4) mask = opt32(v);
        else if (code == 3 && l >= 4) router = opt32(v);
        else if (code == 6 && l >= 4) dns = opt32(v);
        else if (code == 51 && l >= 4) lease = opt32(v);
        else if (code == 54 && l >= 4) server = opt32(v);
        k += 2u + l;
    }
    u32 yiaddr = rd32(p + 16);
    if (type == 2 && d->state == D_SELECTING) {             /* OFFER: ask for it */
        d->offered = yiaddr;
        d->server = server;
        d->state = D_REQUESTING;
        d->tries = 0;
        d->next_ms = net_ms() + 1000;
        send_msg(n, d, 3);
    } else if (type == 5 && (d->state == D_REQUESTING || d->state == D_RENEWING)) {   /* ACK */
        bool changed = n->ip != yiaddr;
        n->ip = yiaddr;
        n->mask = mask ? mask : 0xFFFFFF00;
        n->gw = router;
        if (dns) n->dns = dns;
        n->up = true;
        if (lease < 60) lease = 60;
        u64 now = net_ms();
        n->lease_end_ms = now + (u64)lease * 1000;
        n->renew_ms = now + (u64)lease * 500;
        d->state = D_BOUND;
        if (changed)
            klog("net: %s: %u.%u.%u.%u/%u.%u.%u.%u, router %u.%u.%u.%u, DNS %u.%u.%u.%u (DHCP, %u s)", n->name, yiaddr >> 24,
                 yiaddr >> 16 & 255, yiaddr >> 8 & 255, yiaddr & 255, n->mask >> 24, n->mask >> 16 & 255, n->mask >> 8 & 255,
                 n->mask & 255, router >> 24, router >> 16 & 255, router >> 8 & 255, router & 255, n->dns >> 24, n->dns >> 16 & 255,
                 n->dns >> 8 & 255, n->dns & 255, lease);
        arp_request(n, router ? router : yiaddr);            /* the router's MAC, before anyone needs it */
    } else if (type == 6) {                                  /* NAK: start again */
        n->up = false;
        dhcp_start(n);
    }
    return true;
}

void dhcp_timer(void) {
    u64 now = net_ms();
    for (int i = 0; i < g_nnetif; i++) {
        struct netif *n = g_netifs[i];
        struct dstate *d = &st[i];
        if (!n->dhcp) continue;
        if (d->state == D_BOUND && now >= n->renew_ms) {
            d->state = D_RENEWING;
            d->tries = 0;
            d->next_ms = now + 2000;
            send_msg(n, d, 3);
        } else if ((d->state == D_SELECTING || d->state == D_REQUESTING || d->state == D_RENEWING) && now >= d->next_ms) {
            if (d->state == D_RENEWING && now >= n->lease_end_ms) {
                klog("net: %s: the DHCP lease ran out", n->name);
                n->up = false;
                dhcp_start(n);
                continue;
            }
            if (++d->tries > 4 && d->state != D_RENEWING) {
                /* nobody answers: try again in a while */
                d->state = D_SELECTING;
                d->tries = 0;
                d->next_ms = now + 15000;
                continue;
            }
            d->next_ms = now + (1000u << (d->tries < 4 ? d->tries : 4));
            send_msg(n, d, d->state == D_SELECTING ? 1 : 3);
        }
    }
}
