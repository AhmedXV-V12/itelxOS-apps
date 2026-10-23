/* The network: the cards, Ethernet, and the thread that polls them (net.h). */
#include "lib.h"
#include "arch.h"
#include "mm.h"
#include "errno.h"
#include "proc.h"
#include "time.h"
#include "net.h"

struct netif *g_netifs[NET_MAX_IF];
int g_nnetif;

static struct wait_queue kick_wq;
static volatile bool kicked;

u64 net_ms(void) { return time_uptime_ms(); }

void net_register(struct netif *n) {
    if (g_nnetif >= NET_MAX_IF) return;
    snprintf(n->name, sizeof n->name, "eth%d", g_nnetif);
    g_netifs[g_nnetif++] = n;
    klog("net: %s: %s (%s), MAC %02x:%02x:%02x:%02x:%02x:%02x", n->name, n->model, n->driver, n->mac[0], n->mac[1],
         n->mac[2], n->mac[3], n->mac[4], n->mac[5]);
}

/* the interface for dst: the one whose network holds it, else the first
   with a gateway; next_hop is dst itself or the gateway */
struct netif *net_route(u32 dst, u32 *next_hop) {
    for (int i = 0; i < g_nnetif; i++) {
        struct netif *n = g_netifs[i];
        if (n->up && n->mask && (dst & n->mask) == (n->ip & n->mask)) { *next_hop = dst; return n; }
    }
    for (int i = 0; i < g_nnetif; i++) {
        struct netif *n = g_netifs[i];
        if (n->up && n->gw) { *next_hop = n->gw; return n; }
    }
    return NULL;
}

int net_send_eth(struct netif *n, const u8 *dst, u16 type, const void *payload, u32 len) {
    static u8 frame[FRAME_MAX];
    if (len > NET_MTU) return -E2BIG;
    memcpy(frame, dst, 6);
    memcpy(frame + 6, n->mac, 6);
    wr16(frame + 12, type);
    memcpy(frame + ETH_HDR, payload, len);
    u32 total = ETH_HDR + len;
    if (total < 60) { memset(frame + total, 0, 60 - total); total = 60; }
    int r = n->send(n, frame, total);
    if (r) { n->tx_errors++; return r; }
    n->tx_packets++;
    n->tx_bytes += total;
    return 0;
}

void net_kick(void) {
    kicked = true;
    wake_up_all(&kick_wq);
}

static void eth_input(struct netif *n, const u8 *f, u32 len) {
    if (len < ETH_HDR) return;
    /* for us, or for everyone */
    bool bcast = f[0] == 0xFF && f[1] == 0xFF && f[2] == 0xFF && f[3] == 0xFF && f[4] == 0xFF && f[5] == 0xFF;
    if (!bcast && memcmp(f, n->mac, 6) && !(f[0] & 1)) return;
    u16 type = rd16(f + 12);
    if (type == 0x0806) arp_input(n, f + ETH_HDR, len - ETH_HDR);
    else if (type == 0x0800) ip_input(n, f + ETH_HDR, len - ETH_HDR);
}

static int net_thread(void *arg) {
    static u8 buf[2048];
    bool was_link[NET_MAX_IF] = { false };
    u64 last_timer = 0;
    for (;;) {
        int got = 0;
        for (int i = 0; i < g_nnetif; i++) {
            struct netif *n = g_netifs[i];
            /* a cable plugged in: DHCP (again) */
            bool link = n->link ? n->link(n) : true;
            if (link && !was_link[i]) {
                klog("net: %s: link up%s", n->name, n->dhcp ? ", asking DHCP for an address" : "");
                if (n->dhcp) dhcp_start(n);
            } else if (!link && was_link[i]) {
                klog("net: %s: link down", n->name);
            }
            was_link[i] = link;
            for (int k = 0; k < 64; k++) {
                int len = n->poll(n, buf, sizeof buf);
                if (len <= 0) break;
                n->rx_packets++;
                n->rx_bytes += (u64)len;
                eth_input(n, buf, (u32)len);
                got++;
            }
        }
        u64 now = net_ms();
        if (now - last_timer >= 20) {
            last_timer = now;
            tcp_timer();
            dhcp_timer();
        }
        if (got) {
            schedule();                       /* busy: give the others a turn, then poll again */
            continue;
        }
        /* idle: a tick, or until a send kicks the thread */
        u64 f = irq_save();
        if (!kicked) {
            current->wake_tick = g_ticks + 1;
            sleep_on(&kick_wq);
        }
        kicked = false;
        irq_restore(f);
    }
    return 0;
}

void net_init(void) {
    wq_init(&kick_wq);
    e1000_probe();
    rtl8139_probe();
    virtio_net_probe();
    for (int i = 0; i < g_nnetif; i++) g_netifs[i]->dhcp = true;
    if (!g_nnetif) {
        klog("net: no network card this kernel has a driver for (e1000, e1000e 82574, RTL8139, virtio-net)");
        return;
    }
    kthread_create("net", net_thread, NULL);
}
