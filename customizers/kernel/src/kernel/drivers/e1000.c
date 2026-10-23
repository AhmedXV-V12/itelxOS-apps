/* Intel PRO/1000 network cards (the 8254x "e1000" family and the 82574L
 * "e1000e"): legacy descriptors, 256 to receive and 64 to send, polled by
 * the net thread. These are the cards QEMU, VirtualBox and VMware give a
 * virtual machine by default, and common server and desktop cards. */
#include "lib.h"
#include "arch.h"
#include "mm.h"
#include "errno.h"
#include "pci.h"
#include "time.h"
#include "net.h"

#define CTRL   0x0000
#define STATUS 0x0008
#define EERD   0x0014
#define ICR    0x00C0
#define IMC    0x00D8
#define RCTL   0x0100
#define TCTL   0x0400
#define TIPG   0x0410
#define RDBAL  0x2800
#define RDBAH  0x2804
#define RDLEN  0x2808
#define RDH    0x2810
#define RDT    0x2818
#define TDBAL  0x3800
#define TDBAH  0x3804
#define TDLEN  0x3808
#define TDH    0x3810
#define TDT    0x3818
#define MTA    0x5200
#define RAL0   0x5400
#define RAH0   0x5404

#define NRX 256
#define NTX 64
#define BUF 2048

struct rxd { u64 addr; u16 len; u16 csum; u8 status; u8 err; u16 special; } PACKED;
struct txd { u64 addr; u16 len; u8 cso; u8 cmd; u8 status; u8 css; u16 special; } PACKED;

struct e1000 {
    struct netif nif;
    volatile u8 *regs;
    bool e1000e;
    struct rxd *rx;
    struct txd *tx;
    u8 *rxbuf[NRX], *txbuf[NTX];
    u32 rx_cur, tx_cur;
};

static inline u32 rr(struct e1000 *e, u32 off) { return *(volatile u32 *)(e->regs + off); }
static inline void wr(struct e1000 *e, u32 off, u32 v) { *(volatile u32 *)(e->regs + off) = v; }

static const struct { u16 id; bool e1000e; const char *name; } models[] = {
    { 0x100E, false, "Intel 82540EM Gigabit" },
    { 0x100F, false, "Intel 82545EM Gigabit" },
    { 0x100C, false, "Intel 82544GC Gigabit" },
    { 0x10D3, true, "Intel 82574L Gigabit" },
};

static u16 eeprom(struct e1000 *e, u8 word) {
    u32 v;
    if (e->e1000e) {
        wr(e, EERD, ((u32)word << 2) | 1);
        for (int i = 0; i < 10000; i++) { v = rr(e, EERD); if (v & 2) return (u16)(v >> 16); udelay(5); }
    } else {
        wr(e, EERD, ((u32)word << 8) | 1);
        for (int i = 0; i < 10000; i++) { v = rr(e, EERD); if (v & 0x10) return (u16)(v >> 16); udelay(5); }
    }
    return 0;
}

static int e_send(struct netif *n, const void *frame, u32 len) {
    struct e1000 *e = n->priv;
    struct txd *d = &e->tx[e->tx_cur];
    /* the descriptor is free once the card has sent what was in it */
    for (int i = 0; d->cmd && !(d->status & 1); i++) {
        if (i > 1000) return -EBUSY;
        udelay(10);
    }
    if (len > BUF) return -E2BIG;
    memcpy(e->txbuf[e->tx_cur], frame, len);
    d->len = (u16)len;
    d->cmd = 0x01 | 0x02 | 0x08;                /* end of packet, add the CRC, report status */
    d->status = 0;
    e->tx_cur = (e->tx_cur + 1) % NTX;
    mfence();
    wr(e, TDT, e->tx_cur);
    return 0;
}

static int e_poll(struct netif *n, void *buf, u32 max) {
    struct e1000 *e = n->priv;
    for (;;) {
        struct rxd *d = &e->rx[e->rx_cur];
        if (!(d->status & 1)) return 0;
        u32 len = d->len;
        bool whole = (d->status & 2) && !d->err;
        if (whole && len <= max) memcpy(buf, e->rxbuf[e->rx_cur], len);
        else n->rx_dropped++;
        d->status = 0;
        u32 done = e->rx_cur;
        e->rx_cur = (e->rx_cur + 1) % NRX;
        mfence();
        wr(e, RDT, done);
        if (whole && len <= max) return (int)len;
    }
}

static bool e_link(struct netif *n) {
    struct e1000 *e = n->priv;
    u32 st = rr(e, STATUS);
    static const u32 speeds[] = { 10, 100, 1000, 1000 };
    n->speed_mbps = (st & 2) ? speeds[(st >> 6) & 3] : 0;
    return st & 2;
}

static u8 *dma_buf(paddr_t *phys) {
    static paddr_t page;
    static u32 used = PAGE_SIZE;
    if (used + BUF > PAGE_SIZE) {
        page = pmm_alloc_contig(1, 1ULL << 32);
        if (!page) return NULL;
        used = 0;
    }
    *phys = page + used;
    used += BUF;
    return phys_to_virt(*phys);
}

static void setup(struct pci_dev *p, bool e1000e, const char *name) {
    struct e1000 *e = kzalloc(sizeof *e);
    if (!e) return;
    bool io;
    u64 size;
    u64 bar = pci_bar(p, 0, &io, &size);
    if (io || !bar) { kfree(e); return; }
    pci_enable(p, true, false, true);
    e->regs = vmm_map_device(bar, size ? size : 0x20000, MAP_MMIO_UC);
    e->e1000e = e1000e;
    /* reset, interrupts off (polled) */
    wr(e, IMC, 0xFFFFFFFF);
    wr(e, CTRL, rr(e, CTRL) | (1u << 26));
    mdelay(10);
    for (int i = 0; i < 1000 && (rr(e, CTRL) & (1u << 26)); i++) udelay(10);
    wr(e, IMC, 0xFFFFFFFF);
    (void)rr(e, ICR);
    /* link up, speed by itself; no reset of the link or the PHY */
    u32 c = rr(e, CTRL);
    c |= (1u << 6) | (1u << 5);
    c &= ~((1u << 3) | (1u << 31) | (1u << 7));
    wr(e, CTRL, c);
    /* the MAC address: from the receive address register, else the EEPROM */
    u32 ral = rr(e, RAL0), rah = rr(e, RAH0);
    if ((rah & (1u << 31)) && (ral || (rah & 0xFFFF))) {
        for (int i = 0; i < 4; i++) e->nif.mac[i] = (u8)(ral >> (8 * i));
        e->nif.mac[4] = (u8)rah;
        e->nif.mac[5] = (u8)(rah >> 8);
    } else {
        for (int i = 0; i < 3; i++) {
            u16 w = eeprom(e, (u8)i);
            e->nif.mac[2 * i] = (u8)w;
            e->nif.mac[2 * i + 1] = (u8)(w >> 8);
        }
        wr(e, RAL0, (u32)e->nif.mac[0] | (u32)e->nif.mac[1] << 8 | (u32)e->nif.mac[2] << 16 | (u32)e->nif.mac[3] << 24);
        wr(e, RAH0, (u32)e->nif.mac[4] | (u32)e->nif.mac[5] << 8 | (1u << 31));
    }
    for (int i = 0; i < 128; i++) wr(e, MTA + 4 * i, 0);
    /* the rings */
    paddr_t rxp = pmm_alloc_contig(1, 1ULL << 32), txp = pmm_alloc_contig(1, 1ULL << 32);
    if (!rxp || !txp) { klog("e1000: out of memory"); return; }
    e->rx = phys_to_virt(rxp);
    e->tx = phys_to_virt(txp);
    memset(e->rx, 0, PAGE_SIZE);
    memset(e->tx, 0, PAGE_SIZE);
    for (int i = 0; i < NRX; i++) {
        paddr_t ph;
        e->rxbuf[i] = dma_buf(&ph);
        if (!e->rxbuf[i]) { klog("e1000: out of memory"); return; }
        e->rx[i].addr = ph;
    }
    for (int i = 0; i < NTX; i++) {
        paddr_t ph;
        e->txbuf[i] = dma_buf(&ph);
        if (!e->txbuf[i]) { klog("e1000: out of memory"); return; }
        e->tx[i].addr = ph;
    }
    wr(e, RDBAL, (u32)rxp);
    wr(e, RDBAH, (u32)(rxp >> 32));
    wr(e, RDLEN, NRX * sizeof(struct rxd));
    wr(e, RDH, 0);
    wr(e, RDT, NRX - 1);
    /* receive: on, broadcast, 2048-byte buffers, the CRC stripped */
    wr(e, RCTL, (1u << 1) | (1u << 15) | (1u << 26));
    wr(e, TDBAL, (u32)txp);
    wr(e, TDBAH, (u32)(txp >> 32));
    wr(e, TDLEN, NTX * sizeof(struct txd));
    wr(e, TDH, 0);
    wr(e, TDT, 0);
    /* send: on, pad short packets, collision settings for full duplex */
    wr(e, TCTL, (1u << 1) | (1u << 3) | (0x0Fu << 4) | (0x40u << 12));
    wr(e, TIPG, 0x0060200A);
    e->nif.driver = e1000e ? "e1000e" : "e1000";
    strlcpy(e->nif.model, name, sizeof e->nif.model);
    e->nif.pci = p;
    e->nif.priv = e;
    e->nif.send = e_send;
    e->nif.poll = e_poll;
    e->nif.link = e_link;
    pci_claim(p, e->nif.driver);
    net_register(&e->nif);
}

void e1000_probe(void) {
    for (int i = 0; i < g_npci; i++) {
        struct pci_dev *p = &g_pci[i];
        if (p->vendor != 0x8086 || p->class_code != 0x02) continue;
        for (u32 k = 0; k < ARRAY_LEN(models); k++)
            if (p->device == models[k].id) setup(p, models[k].e1000e, models[k].name);
    }
}
