/* libnet: the network for programs (libnet.h). */
#include "libnet.h"

#define S1(n, a) itx_syscall(n, (long)(a), 0, 0, 0, 0, 0)
#define S2(n, a, b) itx_syscall(n, (long)(a), (long)(b), 0, 0, 0, 0)
#define S3(n, a, b, c) itx_syscall(n, (long)(a), (long)(b), (long)(c), 0, 0, 0)
#define S4(n, a, b, c, d) itx_syscall(n, (long)(a), (long)(b), (long)(c), (long)(d), 0, 0)

/* ---------------------------------------------------------- addresses */

bool ip_parse(const char *s, uint32_t *ip) {
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) {
        if (!isdigit(*s)) return false;
        long n = 0;
        int d = 0;
        while (isdigit(*s) && d < 4) { n = n * 10 + (*s - '0'); s++; d++; }
        if (n > 255) return false;
        v = v << 8 | (uint32_t)n;
        if (i < 3) { if (*s != '.') return false; s++; }
    }
    if (*s) return false;
    *ip = v;
    return true;
}

char *ip_str(char *out, size_t len, uint32_t ip) {
    snprintf(out, len, "%u.%u.%u.%u", ip >> 24, ip >> 16 & 255, ip >> 8 & 255, ip & 255);
    return out;
}

const char *net_error(int e) {
    if (e < 0) e = -e;
    switch (e) {
    case EADDRINUSE: return "the port is in use";
    case ENETDOWN: return "no network (no card this kernel has a driver for)";
    case ENETUNREACH: return "no route to that network (no address yet? see: net)";
    case ECONNRESET: return "the connection was reset";
    case ENOTCONN: return "not connected";
    case ETIMEDOUT: return "no answer (timed out)";
    case ECONNREFUSED: return "the connection was refused";
    case EHOSTUNREACH: return "the computer does not answer on the network";
    case ENOSYS: return "this kernel has no network (the File saver or data_place kernel has)";
    }
    return strerror(-e);
}

/* ------------------------------------------------------- system calls */

int net_info(struct itx_netinfo *out, int max) { return (int)S2(SYS_NET_INFO, out, max); }
int net_config(int index, const struct itx_netconf *c) { return (int)S2(SYS_NET_CONFIG, index, c); }
int net_ping(uint32_t ip, long ms, int seq) { return (int)S3(SYS_NET_PING, ip, ms, seq); }
int sock_open(int type) { return (int)S1(SYS_SOCKET, type); }
int sock_bind(int s, int port) { return (int)S2(SYS_SOCK_BIND, s, port); }
int sock_connect(int s, uint32_t ip, int port, long ms) {
    struct itx_sockaddr a = { ip, (uint16_t)port, 0 };
    return (int)S3(SYS_SOCK_CONNECT, s, &a, ms);
}
int sock_listen(int s, int backlog) { return (int)S2(SYS_SOCK_LISTEN, s, backlog); }
int sock_accept(int s, struct itx_sockaddr *peer, long ms) { return (int)S3(SYS_SOCK_ACCEPT, s, peer, ms); }
long sock_send(int s, const void *buf, size_t len, long ms) { return S4(SYS_SOCK_SEND, s, buf, len, ms); }
long sock_recv(int s, void *buf, size_t len, long ms) { return S4(SYS_SOCK_RECV, s, buf, len, ms); }
long sock_sendto(int s, const void *buf, size_t len, uint32_t ip, int port) {
    struct itx_sockaddr a = { ip, (uint16_t)port, 0 };
    return S4(SYS_SOCK_SENDTO, s, buf, len, &a);
}
long sock_recvfrom(int s, void *buf, size_t len, struct itx_sockaddr *from, long ms) {
    struct itx_dgram d = { buf, (uint32_t)len, 0, { 0, 0, 0 } };
    long r = S3(SYS_SOCK_RECVFROM, s, &d, ms);
    if (r >= 0 && from) *from = d.from;
    return r;
}
int sock_close(int s) { return (int)S1(SYS_SOCK_CLOSE, s); }
int sock_info(int s, struct itx_sockinfo *si) { return (int)S2(SYS_SOCK_INFO, s, si); }

bool sock_send_all(int s, const void *buf, size_t len) {
    const char *p = buf;
    while (len) {
        long n = sock_send(s, p, len, 30000);
        if (n <= 0) return false;
        p += n;
        len -= (size_t)n;
    }
    return true;
}

bool net_ready(long wait_ms) {
    long end = uptime_ms() + wait_ms;
    for (;;) {
        struct itx_netinfo ni[4];
        int n = net_info(ni, 4);
        for (int i = 0; i < n; i++) if (ni[i].flags & NETF_UP) return true;
        if (n <= 0 || uptime_ms() >= end) return false;
        sleep_ms(200);
    }
}

/* ---------------------------------------------------------------- DNS */

static uint32_t dns_server(void) {
    struct itx_netinfo ni[4];
    int n = net_info(ni, 4);
    for (int i = 0; i < n; i++) if ((ni[i].flags & NETF_UP) && ni[i].dns) return ni[i].dns;
    return 0x08080808;
}

/* a name in a DNS message (labels, maybe compressed): the offset after it */
static int skip_name(const uint8_t *m, int len, int o) {
    while (o < len) {
        uint8_t l = m[o];
        if (l == 0) return o + 1;
        if ((l & 0xC0) == 0xC0) return o + 2;
        o += l + 1;
    }
    return -1;
}

int net_resolve(const char *name, uint32_t *ip) {
    if (ip_parse(name, ip)) return 0;
    if (!strcmp(name, "localhost")) { *ip = 0x7F000001; return 0; }
    if (!net_ready(8000)) return -ENETUNREACH;
    uint8_t q[512];
    int o = 12;
    uint16_t id = (uint16_t)(uptime_ms() * 2654435761u >> 16);
    memset(q, 0, 12);
    q[0] = (uint8_t)(id >> 8);
    q[1] = (uint8_t)id;
    q[2] = 0x01;                                  /* recursion desired */
    q[5] = 1;                                     /* one question */
    for (const char *p = name; *p;) {
        const char *dot = strchr(p, '.');
        size_t l = dot ? (size_t)(dot - p) : strlen(p);
        if (!l || l > 63 || o + (int)l + 6 > (int)sizeof q) return -EINVAL;
        q[o++] = (uint8_t)l;
        memcpy(q + o, p, l);
        o += (int)l;
        p += l;
        if (*p == '.') p++;
    }
    q[o++] = 0;
    q[o++] = 0; q[o++] = 1;                       /* type A */
    q[o++] = 0; q[o++] = 1;                       /* class IN */
    int s = sock_open(SOCK_UDP);
    if (s < 0) return s;
    sock_bind(s, 0);
    uint32_t server = dns_server();
    int r = -ETIMEDOUT;
    for (int tries = 0; tries < 3 && r == -ETIMEDOUT; tries++) {
        long sent = sock_sendto(s, q, (size_t)o, server, 53);
        if (sent < 0) { r = (int)sent; break; }
        uint8_t a[1500];
        struct itx_sockaddr from;
        long n = sock_recvfrom(s, a, sizeof a, &from, 2000);
        if (n == -EAGAIN) continue;
        if (n < 12) { r = n < 0 ? (int)n : -EIO; break; }
        if (a[0] != q[0] || a[1] != q[1]) continue;
        if ((a[3] & 15) == 3) { r = -ENOENT; break; }   /* no such name */
        int qd = a[4] << 8 | a[5], an = a[6] << 8 | a[7], p = 12;
        for (int i = 0; i < qd && p >= 0; i++) { p = skip_name(a, (int)n, p); if (p >= 0) p += 4; }
        r = -ENOENT;
        for (int i = 0; i < an && p >= 0 && p < n; i++) {
            p = skip_name(a, (int)n, p);
            if (p < 0 || p + 10 > n) break;
            int type = a[p] << 8 | a[p + 1], rl = a[p + 8] << 8 | a[p + 9];
            p += 10;
            if (type == 1 && rl == 4 && p + 4 <= n) {
                *ip = (uint32_t)a[p] << 24 | (uint32_t)a[p + 1] << 16 | (uint32_t)a[p + 2] << 8 | a[p + 3];
                r = 0;
                break;
            }
            p += rl;
        }
    }
    sock_close(s);
    return r;
}

/* --------------------------------------------------------------- HTTP */

static bool split_url(const char *url, char *host, size_t hl, int *port, char *path, size_t pl) {
    if (strncmp(url, "http://", 7)) return false;
    const char *h = url + 7, *slash = strchr(h, '/'), *colon = strchr(h, ':');
    size_t n = slash ? (size_t)(slash - h) : strlen(h);
    *port = 80;
    if (colon && (!slash || colon < slash)) {
        *port = atoi(colon + 1);
        n = (size_t)(colon - h);
    }
    if (!n || n >= hl || *port <= 0 || *port > 65535) return false;
    memcpy(host, h, n);
    host[n] = 0;
    strlcpy(path, slash ? slash : "/", pl);
    return true;
}

long http_get(const char *url0, const char *file, bool show, int *status) {
    char url[1024];
    strlcpy(url, url0, sizeof url);
    for (int hop = 0; hop < 4; hop++) {
        char host[256], path[1024], ipb[20];
        int port;
        if (!strncmp(url, "https://", 8)) {
            if (show) printf("https is not possible here (no TLS on itelx); use an http:// address\n");
            return -EINVAL;
        }
        if (!split_url(url, host, sizeof host, &port, path, sizeof path)) return -EINVAL;
        uint32_t ip;
        int r = net_resolve(host, &ip);
        if (r) return r;
        if (show) { printf("connecting to %s (%s) port %d ...\n", host, ip_str(ipb, sizeof ipb, ip), port); flush(); }
        int s = sock_open(SOCK_TCP);
        if (s < 0) return s;
        r = sock_connect(s, ip, port, 10000);
        if (r) { sock_close(s); return r; }
        char req[1600];
        int n = snprintf(req, sizeof req, "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: itelx-fetch/1.0\r\nAccept: */*\r\nConnection: close\r\n\r\n", path, host);
        if (!sock_send_all(s, req, (size_t)n)) { sock_close(s); return -ECONNRESET; }
        /* the header */
        static char buf[65536];
        size_t have = 0;
        char *end = NULL;
        while (!end && have < sizeof buf - 1) {
            long k = sock_recv(s, buf + have, sizeof buf - 1 - have, 30000);
            if (k <= 0) { sock_close(s); return k < 0 ? (int)k : -ECONNRESET; }
            have += (size_t)k;
            buf[have] = 0;
            end = strstr(buf, "\r\n\r\n");
        }
        if (!end) { sock_close(s); return -E2BIG; }
        int code = 0;
        const char *sp = strchr(buf, ' ');
        if (sp) code = atoi(sp + 1);
        if (status) *status = code;
        long clen = -1;
        char location[1024] = "";
        for (char *l = strstr(buf, "\r\n"); l && l < end; l = strstr(l + 2, "\r\n")) {
            char *v = l + 2;
            if (!strncasecmp(v, "content-length:", 15)) clen = atol(v + 15 + strspn(v + 15, " "));
            if (!strncasecmp(v, "location:", 9)) {
                v += 9 + strspn(v + 9, " ");
                size_t ll = strcspn(v, "\r");
                if (ll >= sizeof location) ll = sizeof location - 1;
                memcpy(location, v, ll);
                location[ll] = 0;
            }
        }
        if ((code == 301 || code == 302 || code == 303 || code == 307 || code == 308) && location[0]) {
            sock_close(s);
            if (location[0] == '/') snprintf(url, sizeof url, "http://%s:%d%s", host, port, location);
            else strlcpy(url, location, sizeof url);
            if (show) printf("moved to %s\n", url);
            continue;
        }
        if (show && code != 200) printf("the server answered %d\n", code);
        int fd = open(file, O_WRONLY | O_CREATE | O_TRUNC);
        if (fd < 0) { sock_close(s); return fd; }
        size_t body = have - (size_t)(end + 4 - buf);
        long total = 0;
        if (body && write(fd, end + 4, body) != (ssize_t)body) { close(fd); sock_close(s); return -ENOSPC; }
        total += (long)body;
        long t0 = uptime_ms(), last = 0;
        for (;;) {
            long k = sock_recv(s, buf, sizeof buf, 30000);
            if (k == 0) break;
            if (k < 0) { close(fd); sock_close(s); return (int)k; }
            if (write(fd, buf, (size_t)k) != k) { close(fd); sock_close(s); return -ENOSPC; }
            total += k;
            if (show && uptime_ms() - last > 500) {
                last = uptime_ms();
                long ms = last - t0 ? last - t0 : 1;
                if (clen > 0) printf("\r  %ld of %ld KiB (%ld%%), %ld KiB/s   ", total / 1024, clen / 1024, total * 100 / clen, total * 1000 / 1024 / ms);
                else printf("\r  %ld KiB, %ld KiB/s   ", total / 1024, total * 1000 / 1024 / ms);
                flush();
            }
            if (clen >= 0 && total >= clen) break;
        }
        close(fd);
        sock_close(s);
        sync();
        if (show) printf("\r                                                  \r");
        return total;
    }
    return -ELOOP;
}

/* ----------------------------------------------------------- commands */

static void show_if(const struct itx_netinfo *n) {
    char a[20], b[20], c[20], d[20];
    printf("\x1b[96m%s\x1b[0m  %s (%s)  MAC %02x:%02x:%02x:%02x:%02x:%02x\n", n->name, n->model, n->driver, n->mac[0], n->mac[1],
           n->mac[2], n->mac[3], n->mac[4], n->mac[5]);
    if (!(n->flags & NETF_LINK)) printf("  link: \x1b[91mno cable / no link\x1b[0m\n");
    else if (n->speed_mbps) printf("  link: up, %u Mbit/s\n", n->speed_mbps);
    else printf("  link: up\n");
    if (n->flags & NETF_UP) {
        int bits = 0;
        for (uint32_t m = n->mask; m & 0x80000000u; m <<= 1) bits++;
        printf("  address: %s/%d   router %s   DNS %s   %s", ip_str(a, sizeof a, n->ip), bits, ip_str(b, sizeof b, n->gateway),
               ip_str(c, sizeof c, n->dns), n->flags & NETF_DHCP ? "from DHCP" : "set by hand");
        if ((n->flags & NETF_DHCP) && n->lease_s) printf(" (lease %u min)", n->lease_s / 60);
        printf("\n");
    } else {
        printf("  address: %s\n", n->flags & NETF_DHCP ? "none yet (asking DHCP)" : "none");
    }
    printf("  received %lu packets (%lu KiB), sent %lu (%lu KiB)%s\n", (unsigned long)n->rx_packets, (unsigned long)(n->rx_bytes / 1024),
           (unsigned long)n->tx_packets, (unsigned long)(n->tx_bytes / 1024), n->rx_dropped || n->tx_errors ? "" : "");
    (void)d;
}

static void no_card(void) {
    printf("No network card this kernel has a driver for.\n"
           "Drivers: Intel e1000 (82540EM, 82544GC, 82545EM), e1000e (82574L), Realtek RTL8139, virtio-net.\n");
    struct itx_pciinfo p[64];
    int n = pcilist(p, 64);
    bool any = false;
    for (int i = 0; i < n; i++) {
        if (p[i].cls != 0x02) continue;
        if (!any) printf("Network hardware in this computer:\n");
        any = true;
        printf("  %04x:%04x  %s%s\n", p[i].vendor, p[i].device, p[i].sub == 0x80 ? "wireless or other network controller" : "Ethernet controller",
               p[i].driver[0] ? "" : "  (no driver)");
    }
    if (!any) printf("(no network controller on the PCI bus)\n");
}

int cmd_net(int argc, char **argv) {
    struct itx_netinfo ni[4];
    int n = net_info(ni, 4);
    if (n == -ENOSYS) { printf("%s\n", net_error(n)); return 1; }
    if (n <= 0) { no_card(); return 1; }
    if (argc < 2) {
        for (int i = 0; i < n; i++) show_if(&ni[i]);
        return 0;
    }
    int idx = 0;
    if (!strcmp(argv[1], "dhcp")) {
        struct itx_netconf c = { 1, 0, 0, 0, 0 };
        int r = net_config(idx, &c);
        if (r) { printf("net dhcp: %s\n", net_error(r)); return 1; }
        printf("asking DHCP for an address ...\n");
        flush();
        sleep_ms(300);
        if (net_ready(10000)) { net_info(ni, 4); show_if(&ni[0]); return 0; }
        printf("no DHCP answer yet (is the cable in?); it keeps asking\n");
        return 1;
    }
    if (!strcmp(argv[1], "static") && argc >= 3) {
        /* net static 192.168.1.50/24 192.168.1.1 8.8.8.8 */
        char addr[32];
        strlcpy(addr, argv[2], sizeof addr);
        int bits = 24;
        char *sl = strchr(addr, '/');
        if (sl) { *sl = 0; bits = atoi(sl + 1); }
        struct itx_netconf c = { 0, 0, 0, 0, 0 };
        if (!ip_parse(addr, &c.ip) || bits < 1 || bits > 30) { printf("use: net static IP/BITS [ROUTER] [DNS]   e.g. net static 192.168.1.50/24 192.168.1.1 8.8.8.8\n"); return 1; }
        c.mask = bits == 32 ? 0xFFFFFFFFu : ~((1u << (32 - bits)) - 1);
        if (argc > 3 && !ip_parse(argv[3], &c.gateway)) { printf("not an address: %s\n", argv[3]); return 1; }
        if (argc > 4 && !ip_parse(argv[4], &c.dns)) { printf("not an address: %s\n", argv[4]); return 1; }
        int r = net_config(idx, &c);
        if (r) { printf("net static: %s\n", net_error(r)); return 1; }
        net_info(ni, 4);
        show_if(&ni[0]);
        return 0;
    }
    printf("use: net | net dhcp | net static IP/BITS [ROUTER] [DNS]\n");
    return 1;
}

int cmd_ping(int argc, char **argv) {
    if (argc < 2) { printf("use: ping HOST [count]\n"); return 1; }
    uint32_t ip;
    int r = net_resolve(argv[1], &ip);
    if (r) { printf("%s: %s\n", argv[1], net_error(r)); return 1; }
    int count = argc > 2 ? atoi(argv[2]) : 4, got = 0;
    if (count < 1) count = 1;
    char b[20];
    long best = -1, worst = 0, sum = 0;
    for (int i = 1; i <= count; i++) {
        long t0 = uptime_ms();
        int us = net_ping(ip, 2000, i);
        if (us >= 0) {
            got++;
            sum += us;
            if (best < 0 || us < best) best = us;
            if (us > worst) worst = us;
            printf("reply from %s: seq %d, %d.%03d ms\n", ip_str(b, sizeof b, ip), i, us / 1000, us % 1000);
        } else {
            printf("seq %d: %s\n", i, net_error(us));
        }
        flush();
        long left = 1000 - (uptime_ms() - t0);
        if (i < count && left > 0) sleep_ms(left);
    }
    printf("%d sent, %d answered (%d%% lost)", count, got, (count - got) * 100 / count);
    if (got) printf(", %ld.%03ld / %ld.%03ld / %ld.%03ld ms (best / average / worst)", best / 1000, best % 1000, sum / got / 1000,
                    sum / got % 1000, worst / 1000, worst % 1000);
    printf("\n");
    return got ? 0 : 1;
}

int cmd_dns(int argc, char **argv) {
    if (argc < 2) { printf("use: dns NAME\n"); return 1; }
    uint32_t ip;
    int r = net_resolve(argv[1], &ip);
    char b[20];
    if (r) { printf("%s: %s\n", argv[1], r == -ENOENT ? "no such name" : net_error(r)); return 1; }
    printf("%s is %s\n", argv[1], ip_str(b, sizeof b, ip));
    return 0;
}

int cmd_fetch(int argc, char **argv) {
    if (argc < 2) { printf("use: fetch http://... [FILE]\n"); return 1; }
    char file[ITX_PATH_MAX];
    if (argc > 2) strlcpy(file, argv[2], sizeof file);
    else {
        const char *slash = strrchr(argv[1], '/');
        strlcpy(file, slash && slash[1] && strncmp(argv[1] + strlen(argv[1]) - 2, "//", 2) ? slash + 1 : "index.html", sizeof file);
        char *q = strchr(file, '?');
        if (q) *q = 0;
        if (!file[0]) strlcpy(file, "index.html", sizeof file);
    }
    int status = 0;
    long t0 = uptime_ms();
    long n = http_get(argv[1], file, true, &status);
    if (n < 0) { printf("fetch: %s\n", net_error((int)n)); return 1; }
    long ms = uptime_ms() - t0;
    printf("%s: %ld bytes in %ld.%01ld s (HTTP %d)\n", file, n, ms / 1000, ms % 1000 / 100, status);
    return status == 200 ? 0 : 1;
}

void net_help(void) {
    printf("Network:\n"
           "  net                      the network cards, the address, the router, DNS\n"
           "  net dhcp                 ask DHCP for an address again\n"
           "  net static IP/BITS R D   set the address by hand (router R, DNS D)\n"
           "  ping HOST [n]            does a computer answer, how fast\n"
           "  dns NAME                 the address of a name\n"
           "  fetch URL [FILE]         download a file (http://)\n");
}
