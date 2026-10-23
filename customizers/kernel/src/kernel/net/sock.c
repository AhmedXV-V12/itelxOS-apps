/* Sockets and the network system calls (abi/itx_net.h).
 *
 * A socket belongs to the process that made it (or accepted it) and is
 * closed when that process ends. Calls that wait take a timeout in
 * milliseconds; 0 does not wait. Everything a program passes is checked
 * like every other system call: pointers against its page tables, lengths
 * against the buffers. */
#include "lib.h"
#include "arch.h"
#include "mm.h"
#include "errno.h"
#include "proc.h"
#include "time.h"
#include "net.h"

struct sock g_socks[MAX_SOCKS];
static u16 next_port = 49152;

struct sock *sock_alloc(int type, int owner) {
    for (int i = 0; i < MAX_SOCKS; i++) {
        struct sock *s = &g_socks[i];
        if (s->used) continue;
        memset(s, 0, sizeof *s);
        s->type = type;
        s->owner = owner;
        wq_init(&s->wq);
        if (type == SOCK_UDP) {
            s->dq = kmalloc(sizeof(struct dgram) * UDP_QUEUE);
            if (!s->dq) return NULL;
            s->state = SOCKS_UDP;
        } else {
            s->rbuf = kmalloc(SOCK_BUF);
            s->sbuf = kmalloc(SOCK_BUF);
            if (!s->rbuf || !s->sbuf) { kfree(s->rbuf); kfree(s->sbuf); return NULL; }
            s->mss = NET_MTU - 40;
            s->state = SOCKS_CLOSED;
        }
        s->used = true;
        return s;
    }
    return NULL;
}

void sock_free(struct sock *s) {
    if (!s->used) return;
    wake_up_all(&s->wq);
    kfree(s->dq);
    kfree(s->rbuf);
    kfree(s->sbuf);
    s->dq = NULL;
    s->rbuf = s->sbuf = NULL;
    s->used = false;
}

static void close_one(struct sock *s) {
    if (s->type == SOCK_TCP) tcp_close_sock(s);
    else sock_free(s);
}

void net_proc_exit(int pid) {
    for (int i = 0; i < MAX_SOCKS; i++)
        if (g_socks[i].used && g_socks[i].owner == pid) close_one(&g_socks[i]);
}

static struct sock *get(u64 h) {
    if (h >= MAX_SOCKS) return NULL;
    struct sock *s = &g_socks[h];
    return s->used && s->owner == current->pid ? s : NULL;
}

static bool port_free(int type, u16 port) {
    for (int i = 0; i < MAX_SOCKS; i++)
        if (g_socks[i].used && g_socks[i].type == type && g_socks[i].lport == port && !g_socks[i].parent) return false;
    return true;
}

static u16 any_port(int type) {
    for (int i = 0; i < 16384; i++) {
        u16 p = next_port++;
        if (next_port < 49152) next_port = 49152;
        if (port_free(type, p)) return p;
    }
    return 0;
}

/* waits on the socket until done() or the time is up; false on time out */
static int wait_for(struct sock *s, u64 ms, bool (*done)(struct sock *)) {
    u64 end = net_ms() + ms;
    while (!done(s)) {
        if (!ms || net_ms() >= end) return -EAGAIN;
        if (current->killed) return -EINTR;
        u64 f = irq_save();
        u64 left = end - net_ms();
        current->wake_tick = g_ticks + (left * TIMER_HZ + 999) / 1000 + 1;
        sleep_on(&s->wq);
        irq_restore(f);
        if (!s->used) return -ECONNRESET;
    }
    return 0;
}

static bool connected(struct sock *s) { return s->state != SOCKS_SYN_SENT; }
static bool readable(struct sock *s) { return s->rlen || s->fin_rcvd || s->err || s->state == SOCKS_CLOSED; }
static bool writable(struct sock *s) { return s->slen < SOCK_BUF || s->err || s->state == SOCKS_CLOSED; }
static bool has_dgram(struct sock *s) { return s->dq_n > 0; }
static bool has_conn(struct sock *s) {
    for (int i = 0; i < MAX_SOCKS; i++) {
        struct sock *c = &g_socks[i];
        if (c->used && c->parent == s && c->state != SOCKS_SYN_RCVD) return true;
    }
    return false;
}

/* ---------------------------------------------------------- the calls */

static i64 net_info(void *u, u64 max) {
    if (max > NET_MAX_IF) max = NET_MAX_IF;
    for (u64 i = 0; i < max && i < (u64)g_nnetif; i++) {
        struct netif *n = g_netifs[i];
        struct itx_netinfo k;
        memset(&k, 0, sizeof k);
        strlcpy(k.name, n->name, sizeof k.name);
        strlcpy(k.driver, n->driver, sizeof k.driver);
        strlcpy(k.model, n->model, sizeof k.model);
        memcpy(k.mac, n->mac, 6);
        k.flags = (n->up ? NETF_UP : 0) | ((!n->link || n->link(n)) ? NETF_LINK : 0) | (n->dhcp ? NETF_DHCP : 0);
        k.ip = n->ip;
        k.mask = n->mask;
        k.gateway = n->gw;
        k.dns = n->dns;
        u64 now = net_ms();
        k.lease_s = n->dhcp && n->lease_end_ms > now ? (u32)((n->lease_end_ms - now) / 1000) : 0;
        k.speed_mbps = n->speed_mbps;
        k.rx_packets = n->rx_packets;
        k.tx_packets = n->tx_packets;
        k.rx_bytes = n->rx_bytes;
        k.tx_bytes = n->tx_bytes;
        k.rx_dropped = n->rx_dropped;
        k.tx_errors = n->tx_errors;
        if (copy_to_user((struct itx_netinfo *)u + i, &k, sizeof k)) return -EFAULT;
    }
    return g_nnetif;
}

static i64 net_config(u64 idx, const void *u) {
    if (idx >= (u64)g_nnetif) return -ENODEV;
    struct itx_netconf c;
    if (copy_from_user(&c, u, sizeof c)) return -EFAULT;
    struct netif *n = g_netifs[idx];
    if (c.dhcp) {
        n->up = false;
        n->ip = 0;
        dhcp_start(n);
        return 0;
    }
    if (!c.ip || !c.mask) return -EINVAL;
    n->dhcp = false;
    n->ip = c.ip;
    n->mask = c.mask;
    n->gw = c.gateway;
    n->dns = c.dns;
    n->up = true;
    n->lease_end_ms = 0;
    klog("net: %s: %u.%u.%u.%u set by hand", n->name, c.ip >> 24, c.ip >> 16 & 255, c.ip >> 8 & 255, c.ip & 255);
    return 0;
}

static i64 do_connect(struct sock *s, const void *ua, u64 ms) {
    struct itx_sockaddr a;
    if (copy_from_user(&a, ua, sizeof a)) return -EFAULT;
    if (s->type != SOCK_TCP) {                          /* UDP: only remembers where datagrams go */
        s->rip = a.ip;
        s->rport = a.port;
        if (!s->lport) s->lport = any_port(SOCK_UDP);
        return 0;
    }
    if (s->state != SOCKS_CLOSED) return -EINVAL;
    u32 hop;
    struct netif *n = net_route(a.ip, &hop);
    if (!n) return g_nnetif ? -ENETUNREACH : -ENETDOWN;
    u8 mac[6];
    if (!arp_resolve(n, hop, mac, ms < 3000 ? (u32)ms : 3000)) return -EHOSTUNREACH;
    s->lip = n->ip;
    s->rip = a.ip;
    s->rport = a.port;
    if (!s->lport) s->lport = any_port(SOCK_TCP);
    s->iss = (u32)rdtsc();
    s->snd_una = s->iss;
    s->snd_nxt = s->iss + 1;
    s->state = SOCKS_SYN_SENT;
    s->rto_ms = 1000;
    s->rto_at = net_ms() + 1000;
    tcp_send_syn(s);
    int r = wait_for(s, ms, connected);
    if (r == -EAGAIN) { s->state = SOCKS_CLOSED; s->rto_at = 0; return -ETIMEDOUT; }
    if (r) return r;
    if (s->state != SOCKS_ESTABLISHED) { int e = s->err ? s->err : -ECONNREFUSED; s->state = SOCKS_CLOSED; s->err = 0; return e; }
    return 0;
}

static i64 do_accept(struct sock *s, void *upeer, u64 ms) {
    if (s->state != SOCKS_LISTEN) return -EINVAL;
    int r = wait_for(s, ms, has_conn);
    if (r) return r;
    for (int i = 0; i < MAX_SOCKS; i++) {
        struct sock *c = &g_socks[i];
        if (!c->used || c->parent != s || c->state == SOCKS_SYN_RCVD) continue;
        c->parent = NULL;
        c->owner = current->pid;
        if (s->nready) s->nready--;
        if (upeer) {
            struct itx_sockaddr a = { c->rip, c->rport, 0 };
            if (copy_to_user(upeer, &a, sizeof a)) return -EFAULT;
        }
        return i;
    }
    return -EAGAIN;
}

static i64 do_send(struct sock *s, const u8 *ubuf, u64 len, u64 ms) {
    if (s->type != SOCK_TCP) return -EINVAL;
    if (!user_range_ok(current_pml4(), (u64)ubuf, len, false)) return -EFAULT;
    u64 done = 0;
    while (done < len) {
        if (s->err) return done ? (i64)done : s->err;
        if (s->state != SOCKS_ESTABLISHED && s->state != SOCKS_CLOSE_WAIT) return done ? (i64)done : -ENOTCONN;
        int r = wait_for(s, ms, writable);
        if (r) return done ? (i64)done : r;
        if (s->err) continue;
        u32 room = SOCK_BUF - s->slen, n = (u32)MIN(len - done, (u64)room);
        u32 at = (s->shead + s->slen) % SOCK_BUF;
        u32 first = SOCK_BUF - at < n ? SOCK_BUF - at : n;
        if (copy_from_user(s->sbuf + at, ubuf + done, first)) return -EFAULT;
        if (first < n && copy_from_user(s->sbuf, ubuf + done + first, n - first)) return -EFAULT;
        s->slen += n;
        done += n;
        tcp_send_more(s);
        net_kick();
    }
    return (i64)done;
}

static i64 do_recv(struct sock *s, u8 *ubuf, u64 len, u64 ms) {
    if (s->type != SOCK_TCP) return -EINVAL;
    if (!user_range_ok(current_pml4(), (u64)ubuf, len, true)) return -EFAULT;
    if (s->state == SOCKS_LISTEN || s->state == SOCKS_SYN_SENT) return -ENOTCONN;
    int r = wait_for(s, ms, readable);
    if (r) return r;
    if (!s->rlen) {
        if (s->err) return s->err;
        return 0;                                       /* the peer closed it */
    }
    u32 before = SOCK_BUF - s->rlen;
    u32 n = (u32)MIN(len, (u64)s->rlen);
    u32 first = SOCK_BUF - s->rhead < n ? SOCK_BUF - s->rhead : n;
    if (copy_to_user(ubuf, s->rbuf + s->rhead, first)) return -EFAULT;
    if (first < n && copy_to_user(ubuf + first, s->rbuf, n - first)) return -EFAULT;
    s->rhead = (s->rhead + n) % SOCK_BUF;
    s->rlen -= n;
    /* the window opened a lot: tell the peer */
    if (before < 2u * s->mss && SOCK_BUF - s->rlen >= 2u * s->mss) tcp_window_update(s);
    return n;
}

static i64 do_sendto(struct sock *s, const u8 *ubuf, u64 len, const void *ua) {
    if (s->type != SOCK_UDP) return -EINVAL;
    if (len > NET_MTU - 28) return -E2BIG;
    struct itx_sockaddr a = { s->rip, s->rport, 0 };
    if (ua && copy_from_user(&a, ua, sizeof a)) return -EFAULT;
    if (!a.ip || !a.port) return -ENOTCONN;
    static u8 k[NET_MTU];
    if (copy_from_user(k, ubuf, len)) return -EFAULT;
    if (!s->lport) s->lport = any_port(SOCK_UDP);
    int r = udp_send(NULL, 0, s->lport, a.ip, a.port, k, (u32)len, 3000);
    return r ? r : (i64)len;
}

static i64 do_recvfrom(struct sock *s, void *ud, u64 ms) {
    if (s->type != SOCK_UDP) return -EINVAL;
    struct itx_dgram d;
    if (copy_from_user(&d, ud, sizeof d)) return -EFAULT;
    if (!s->lport) return -ENOTCONN;
    int r = wait_for(s, ms, has_dgram);
    if (r) return r;
    struct dgram *g = &s->dq[s->dq_head];
    u32 n = g->len < d.len ? g->len : d.len;
    if (!user_range_ok(current_pml4(), (u64)d.buf, n, true) || copy_to_user(d.buf, g->data, n)) return -EFAULT;
    d.len = n;
    d.from.ip = g->from_ip;
    d.from.port = g->from_port;
    s->dq_head = (s->dq_head + 1) % UDP_QUEUE;
    s->dq_n--;
    if (copy_to_user(ud, &d, sizeof d)) return -EFAULT;
    return n;
}

i64 sys_net(u64 nr, u64 a, u64 b, u64 c, u64 d) {
    struct sock *s;
    switch (nr) {
    case SYS_NET_INFO: return net_info((void *)a, b);
    case SYS_NET_CONFIG: return net_config(a, (const void *)b);
    case SYS_NET_PING: return icmp_ping((u32)a, b > 30000 ? 30000 : (u32)b, (u16)c);
    case SYS_SOCKET: {
        if (a != SOCK_UDP && a != SOCK_TCP) return -EINVAL;
        if (!g_nnetif) return -ENETDOWN;
        s = sock_alloc((int)a, current->pid);
        return s ? (i64)(s - g_socks) : -ENFILE;
    }
    }
    if (!(s = get(a))) return -EBADF;
    switch (nr) {
    case SYS_SOCK_BIND:
        if (b > 65535) return -EINVAL;
        if (b && !port_free(s->type, (u16)b)) return -EADDRINUSE;
        s->lport = b ? (u16)b : any_port(s->type);
        return s->lport;
    case SYS_SOCK_CONNECT: return do_connect(s, (const void *)b, c);
    case SYS_SOCK_LISTEN:
        if (s->type != SOCK_TCP || !s->lport) return -EINVAL;
        s->state = SOCKS_LISTEN;
        s->backlog = b < 1 ? 1 : b > 16 ? 16 : (int)b;
        return 0;
    case SYS_SOCK_ACCEPT: return do_accept(s, (void *)b, c);
    case SYS_SOCK_SEND: return do_send(s, (const u8 *)b, c, d);
    case SYS_SOCK_RECV: return do_recv(s, (u8 *)b, c, d);
    case SYS_SOCK_SENDTO: return do_sendto(s, (const u8 *)b, c, (const void *)d);
    case SYS_SOCK_RECVFROM: return do_recvfrom(s, (void *)b, c);
    case SYS_SOCK_CLOSE: close_one(s); return 0;
    case SYS_SOCK_INFO: {
        struct itx_sockinfo k;
        memset(&k, 0, sizeof k);
        k.type = (u32)s->type;
        k.state = (u32)s->state;
        k.local.ip = s->lip;
        k.local.port = s->lport;
        k.remote.ip = s->rip;
        k.remote.port = s->rport;
        k.rx_queued = s->type == SOCK_TCP ? s->rlen : (u32)s->dq_n;
        k.tx_queued = s->slen;
        return copy_to_user((void *)b, &k, sizeof k) ? -EFAULT : 0;
    }
    }
    return -ENOSYS;
}
