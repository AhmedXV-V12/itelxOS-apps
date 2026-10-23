/* Realtek RTL8139 network cards (10/100): a receive ring of 8 KiB and four
 * send buffers, through I/O ports, polled by the net thread. */
#include "lib.h"
#include "arch.h"
#include "mm.h"
#include "errno.h"
#include "pci.h"
#include "time.h"
#include "net.h"

#define IDR0    0x00
#define MAR0    0x08
#define TSD0    0x10
#define TSAD0   0x20
#define RBSTART 0x30
#define CR      0x37
#define CAPR    0x38
#define IMR     0x3C
#define ISR     0x3E
#define TCR     0x40
#define RCR     0x44
#define CONFIG1 0x52
#define MSR     0x58

#define RX_LEN  8192
#define RX_ALLOC (RX_LEN + 16 + 1536)       /* WRAP: a packet may run past the end */

struct rtl {
    struct netif nif;
    u16 io;
    u8 *rx;
    u32 rx_off;
    u8 *tx[4];
    paddr_t tx_phys[4];
    int tx_cur;
};

static int r_send(struct netif *n, const void *frame, u32 len) {
    struct rtl *r = n->priv;
    u16 tsd = (u16)(r->io + TSD0 + 4 * r->tx_cur);
    /* the buffer is free when the card owns nothing in it (OWN set after DMA) */
    for (int i = 0; i < 2000; i++) {
        u32 st = inl(tsd);
        if ((st & (1u << 13)) || !(st & 0x1FFF)) break;
        udelay(5);
    }
    if (len > 1792) return -E2BIG;
    memcpy(r->tx[r->tx_cur], frame, len);
    outl((u16)(r->io + TSAD0 + 4 * r->tx_cur), (u32)r->tx_phys[r->tx_cur]);
    outl(tsd, len);                                /* starts it (OWN cleared) */
    r->tx_cur = (r->tx_cur + 1) % 4;
    return 0;
}

static int r_poll(struct netif *n, void *buf, u32 max) {
    struct rtl *r = n->priv;
    while (!(inb((u16)(r->io + CR)) & 1)) {       /* the buffer is not empty */
        u8 *h = r->rx + r->rx_off;
        u16 status = (u16)(h[0] | h[1] << 8), len = (u16)(h[2] | h[3] << 8);
        if (!(status & 1) || len < 64 || len > 1792) {
            /* a broken header: start the receiver again */
            outb((u16)(r->io + CR), 0x04);
            outb((u16)(r->io + CR), 0x0C);
            r->rx_off = 0;
            outw((u16)(r->io + CAPR), (u16)(0 - 16));
            return 0;
        }
        u32 flen = len - 4;                        /* without the CRC */
        int got = 0;
        if (flen <= max) { memcpy(buf, h + 4, flen); got = (int)flen; }
        else n->rx_dropped++;
        r->rx_off = (r->rx_off + len + 4 + 3) & ~3u;
        if (r->rx_off >= RX_LEN) r->rx_off -= RX_LEN;
        outw((u16)(r->io + CAPR), (u16)(r->rx_off - 16));
        outw((u16)(r->io + ISR), 0x05);            /* receive ok, receive error acknowledged */
        if (got) return got;
    }
    return 0;
}

static bool r_link(struct netif *n) {
    struct rtl *r = n->priv;
    u8 msr = inb((u16)(r->io + MSR));
    n->speed_mbps = (msr & 4) ? 0 : (msr & 8) ? 10 : 100;
    return !(msr & 4);                            /* LINKB: 1 when the link is down */
}

static void setup(struct pci_dev *p) {
    bool io;
    u64 base = pci_bar(p, 0, &io, NULL);
    if (!io || !base) return;
    struct rtl *r = kzalloc(sizeof *r);
    if (!r) return;
    r->io = (u16)base;
    pci_enable(p, false, true, true);
    outb((u16)(r->io + CONFIG1), 0);              /* power on */
    outb((u16)(r->io + CR), 0x10);                /* reset */
    for (int i = 0; i < 1000 && (inb((u16)(r->io + CR)) & 0x10); i++) udelay(10);
    for (int i = 0; i < 6; i++) r->nif.mac[i] = inb((u16)(r->io + IDR0 + i));
    paddr_t rx = pmm_alloc_contig((RX_ALLOC + PAGE_SIZE - 1) / PAGE_SIZE, 1ULL << 32);
    paddr_t tx = pmm_alloc_contig(2, 1ULL << 32);
    if (!rx || !tx) { klog("rtl8139: out of memory"); return; }
    r->rx = phys_to_virt(rx);
    for (int i = 0; i < 4; i++) {
        r->tx_phys[i] = tx + (paddr_t)i * 2048;
        r->tx[i] = phys_to_virt(r->tx_phys[i]);
    }
    outl((u16)(r->io + RBSTART), (u32)rx);
    outw((u16)(r->io + IMR), 0);                  /* polled */
    outl((u16)(r->io + MAR0), 0xFFFFFFFF);
    outl((u16)(r->io + MAR0 + 4), 0xFFFFFFFF);
    /* receive: our address, broadcast, multicast; WRAP; 8 KiB; DMA bursts unlimited */
    outl((u16)(r->io + RCR), 0x02 | 0x04 | 0x08 | 0x80 | (7u << 8));
    outl((u16)(r->io + TCR), 6u << 8);            /* DMA bursts of 1024 */
    outb((u16)(r->io + CR), 0x0C);                /* receive and send on */
    outw((u16)(r->io + CAPR), (u16)(0 - 16));
    r->nif.driver = "rtl8139";
    strlcpy(r->nif.model, "Realtek RTL8139 Fast Ethernet", sizeof r->nif.model);
    r->nif.pci = p;
    r->nif.priv = r;
    r->nif.send = r_send;
    r->nif.poll = r_poll;
    r->nif.link = r_link;
    pci_claim(p, "rtl8139");
    net_register(&r->nif);
}

void rtl8139_probe(void) {
    for (int i = 0; i < g_npci; i++) {
        struct pci_dev *p = &g_pci[i];
        if ((p->vendor == 0x10EC && p->device == 0x8139) || (p->vendor == 0x1186 && p->device == 0x1300)) setup(p);
    }
}
