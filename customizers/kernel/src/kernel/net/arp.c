/* ARP: the Ethernet address of an IPv4 address on the local network. */
#include "lib.h"
#include "arch.h"
#include "proc.h"
#include "time.h"
#include "net.h"

#define ARP_N 32
#define ARP_LIFE_MS (10 * 60 * 1000)

static struct { u32 ip; u8 mac[6]; u64 at; } cache[ARP_N];
static struct wait_queue arp_wq;
static bool wq_ready;

void arp_add(u32 ip, const u8 mac[6]) {
    int slot = -1;
    u64 oldest = ~0ULL;
    for (int i = 0; i < ARP_N; i++) {
        if (cache[i].ip == ip) { slot = i; break; }
        if (cache[i].at < oldest) { oldest = cache[i].at; slot = i; }
    }
    cache[slot].ip = ip;
    memcpy(cache[slot].mac, mac, 6);
    cache[slot].at = net_ms() + 1;
    if (wq_ready) wake_up_all(&arp_wq);
}

bool arp_lookup(u32 ip, u8 mac[6]) {
    if (ip == 0xFFFFFFFF) { memset(mac, 0xFF, 6); return true; }
    u64 now = net_ms();
    for (int i = 0; i < ARP_N; i++) {
        if (cache[i].ip == ip && cache[i].at && now - cache[i].at < ARP_LIFE_MS) {
            memcpy(mac, cache[i].mac, 6);
            return true;
        }
    }
    return false;
}

static void arp_send(struct netif *n, u16 op, const u8 *tmac, u32 tip, const u8 *dst) {
    u8 p[28];
    wr16(p, 1);                 /* Ethernet */
    wr16(p + 2, 0x0800);        /* IPv4 */
    p[4] = 6;
    p[5] = 4;
    wr16(p + 6, op);
    memcpy(p + 8, n->mac, 6);
    wr32(p + 14, n->ip);
    memcpy(p + 18, tmac, 6);
    wr32(p + 24, tip);
    net_send_eth(n, dst, 0x0806, p, sizeof p);
}

void arp_request(struct netif *n, u32 ip) {
    static const u8 zero[6], bcast[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    arp_send(n, 1, zero, ip, bcast);
}

void arp_input(struct netif *n, const u8 *p, u32 len) {
    if (len < 28 || rd16(p) != 1 || rd16(p + 2) != 0x0800 || p[4] != 6 || p[5] != 4) return;
    u16 op = rd16(p + 6);
    u32 sip = rd32(p + 14), tip = rd32(p + 24);
    if (sip) arp_add(sip, p + 8);
    if (op == 1 && n->up && tip == n->ip) arp_send(n, 2, p + 8, sip, p + 8);   /* who has our address: we do */
}

bool arp_resolve(struct netif *n, u32 ip, u8 mac[6], u32 ms) {
    if (arp_lookup(ip, mac)) return true;
    if (!wq_ready) { wq_init(&arp_wq); wq_ready = true; }
    u64 end = net_ms() + ms;
    while (net_ms() < end) {
        arp_request(n, ip);
        u64 until = net_ms() + 300;
        while (net_ms() < until && net_ms() < end) {
            u64 f = irq_save();
            current->wake_tick = g_ticks + 2;
            sleep_on(&arp_wq);
            irq_restore(f);
            if (arp_lookup(ip, mac)) return true;
            if (current->killed) return false;
        }
    }
    return false;
}
