/* virtio network cards of virtual machines (QEMU/KVM, Proxmox, ...): the
 * legacy PCI interface (I/O ports), a receive and a send queue, polled by
 * the net thread. */
#include "lib.h"
#include "arch.h"
#include "mm.h"
#include "errno.h"
#include "pci.h"
#include "time.h"
#include "net.h"

#define V_FEATURES   0x00
#define V_GUEST_FEAT 0x04
#define V_QADDR      0x08
#define V_QSIZE      0x0C
#define V_QSEL       0x0E
#define V_QNOTIFY    0x10
#define V_STATUS     0x12
#define V_ISR        0x13
#define V_CONFIG     0x14            /* the MAC, then the link status */

#define F_MAC    (1u << 5)
#define F_STATUS (1u << 16)

#define HDR 10                       /* struct virtio_net_hdr, without mergeable buffers */
#define BUF 2048

struct vdesc { u64 addr; u32 len; u16 flags; u16 next; } PACKED;
struct vq {
    u16 size;
    struct vdesc *desc;
    volatile u16 *avail;             /* flags, idx, ring[size] */
    volatile u8 *used;               /* flags, idx, ring[size] of { u32 id, u32 len } */
    u16 last_used;
    u8 *buf;
    paddr_t buf_phys;
};

struct vnet {
    struct netif nif;
    u16 io;
    u32 features;
    struct vq q[2];                  /* 0 receive, 1 send */
    u16 tx_next;
};

static inline u16 used_idx(struct vq *q) { return *(volatile u16 *)(q->used + 2); }
static inline u32 used_id(struct vq *q, u16 i) { return *(volatile u32 *)(q->used + 4 + 8 * (i % q->size)); }
static inline u32 used_len(struct vq *q, u16 i) { return *(volatile u32 *)(q->used + 8 + 8 * (i % q->size)); }

static bool setup_queue(struct vnet *v, int n) {
    struct vq *q = &v->q[n];
    outw((u16)(v->io + V_QSEL), (u16)n);
    q->size = inw((u16)(v->io + V_QSIZE));
    if (!q->size || q->size > 1024) return false;
    u64 desc = 16ULL * q->size, avail = 6 + 2ULL * q->size;
    u64 used_off = ALIGN_UP(desc + avail, PAGE_SIZE), total = used_off + ALIGN_UP(6 + 8ULL * q->size, PAGE_SIZE);
    paddr_t ph = pmm_alloc_contig(total / PAGE_SIZE, 1ULL << 44);
    paddr_t bp = pmm_alloc_contig((u64)q->size * BUF / PAGE_SIZE, 1ULL << 44);
    if (!ph || !bp) return false;
    u8 *base = phys_to_virt(ph);
    memset(base, 0, total);
    q->desc = (struct vdesc *)base;
    q->avail = (volatile u16 *)(base + desc);
    q->used = base + used_off;
    q->buf = phys_to_virt(bp);
    q->buf_phys = bp;
    for (u16 i = 0; i < q->size; i++) {
        q->desc[i].addr = bp + (u64)i * BUF;
        q->desc[i].len = BUF;
        q->desc[i].flags = n == 0 ? 2 : 0;       /* the device writes the receive buffers */
    }
    outl((u16)(v->io + V_QADDR), (u32)(ph >> 12));
    return true;
}

static int v_send(struct netif *n, const void *frame, u32 len) {
    struct vnet *v = n->priv;
    struct vq *q = &v->q[1];
    /* buffers the device is done with come back first */
    u16 inflight = (u16)(q->avail[1] - used_idx(q));
    for (int i = 0; inflight >= q->size - 1; i++) {
        if (i > 1000) return -EBUSY;
        udelay(10);
        inflight = (u16)(q->avail[1] - used_idx(q));
    }
    if (len + HDR > BUF) return -E2BIG;
    u16 d = v->tx_next;
    v->tx_next = (u16)((v->tx_next + 1) % q->size);
    u8 *b = q->buf + (u64)d * BUF;
    memset(b, 0, HDR);
    memcpy(b + HDR, frame, len);
    q->desc[d].len = len + HDR;
    q->desc[d].flags = 0;
    u16 idx = q->avail[1];
    q->avail[2 + idx % q->size] = d;
    mfence();
    q->avail[1] = (u16)(idx + 1);
    mfence();
    outw((u16)(v->io + V_QNOTIFY), 1);
    return 0;
}

static int v_poll(struct netif *n, void *buf, u32 max) {
    struct vnet *v = n->priv;
    struct vq *q = &v->q[0];
    while (q->last_used != used_idx(q)) {
        u32 id = used_id(q, q->last_used), len = used_len(q, q->last_used);
        q->last_used++;
        int got = 0;
        if (id < q->size && len > HDR && len - HDR <= max) {
            memcpy(buf, q->buf + (u64)id * BUF + HDR, len - HDR);
            got = (int)(len - HDR);
        } else {
            n->rx_dropped++;
        }
        /* the buffer goes back to the device */
        u16 idx = q->avail[1];
        q->avail[2 + idx % q->size] = (u16)id;
        mfence();
        q->avail[1] = (u16)(idx + 1);
        mfence();
        outw((u16)(v->io + V_QNOTIFY), 0);
        if (got) return got;
    }
    return 0;
}

static bool v_link(struct netif *n) {
    struct vnet *v = n->priv;
    if (!(v->features & F_STATUS)) return true;
    return inw((u16)(v->io + V_CONFIG + 6)) & 1;
}

static void setup(struct pci_dev *p) {
    bool io;
    u64 base = pci_bar(p, 0, &io, NULL);
    if (!io || !base) { klog("virtio-net: %02x:%02x.%u has no legacy I/O interface", p->bus, p->dev, p->fn); return; }
    struct vnet *v = kzalloc(sizeof *v);
    if (!v) return;
    v->io = (u16)base;
    pci_enable(p, false, true, true);
    outb((u16)(v->io + V_STATUS), 0);                  /* reset */
    outb((u16)(v->io + V_STATUS), 1 | 2);              /* acknowledged, a driver is here */
    u32 f = inl((u16)(v->io + V_FEATURES));
    v->features = f & (F_MAC | F_STATUS);
    outl((u16)(v->io + V_GUEST_FEAT), v->features);
    if (!setup_queue(v, 0) || !setup_queue(v, 1)) {
        outb((u16)(v->io + V_STATUS), 0x80);           /* failed */
        klog("virtio-net: the queues could not be set up");
        return;
    }
    if (v->features & F_MAC) for (int i = 0; i < 6; i++) v->nif.mac[i] = inb((u16)(v->io + V_CONFIG + i));
    else { v->nif.mac[0] = 0x52; v->nif.mac[1] = 0x54; v->nif.mac[5] = (u8)rdtsc(); }
    outb((u16)(v->io + V_STATUS), 1 | 2 | 4);          /* driver ready */
    /* every receive buffer to the device */
    struct vq *rq = &v->q[0];
    for (u16 i = 0; i < rq->size; i++) rq->avail[2 + i] = i;
    mfence();
    rq->avail[1] = rq->size;
    mfence();
    outw((u16)(v->io + V_QNOTIFY), 0);
    v->nif.driver = "virtio-net";
    strlcpy(v->nif.model, "virtio network card", sizeof v->nif.model);
    v->nif.pci = p;
    v->nif.priv = v;
    v->nif.send = v_send;
    v->nif.poll = v_poll;
    v->nif.link = v_link;
    pci_claim(p, "virtio-net");
    net_register(&v->nif);
}

void virtio_net_probe(void) {
    for (int i = 0; i < g_npci; i++) {
        struct pci_dev *p = &g_pci[i];
        if (p->vendor == 0x1AF4 && p->device == 0x1000) setup(p);
    }
}
