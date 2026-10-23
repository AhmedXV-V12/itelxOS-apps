/* The network (customizer kernels): network cards, Ethernet, ARP, IPv4,
 * ICMP, UDP, TCP, DHCP, and sockets for programs.
 *
 * The cards are polled, like every itelx driver: the "net" kernel thread
 * takes the frames the cards received, a few hundred times a second, and
 * runs the timers (ARP, TCP retransmission, DHCP). The kernel is not
 * preempted, so the stack needs no locks: a system call that works on a
 * socket runs to its end or sleeps on the socket's wait queue, and the net
 * thread wakes it when data or a connection comes. */
#ifndef ITX_NETK_H
#define ITX_NETK_H
#include "types.h"
#include "proc.h"
#include "../../abi/itx_net.h"

#define NET_MTU 1500
#define ETH_HDR 14
#define FRAME_MAX 1518

struct pci_dev;

struct netif {
    char name[8];
    const char *driver;
    char model[48];
    u8 mac[6];
    struct pci_dev *pci;
    void *priv;
    int  (*send)(struct netif *n, const void *frame, u32 len);   /* 0 or < 0 */
    int  (*poll)(struct netif *n, void *buf, u32 max);           /* a frame's length, 0 none */
    bool (*link)(struct netif *n);
    u32 speed_mbps;
    /* the address */
    bool up, dhcp;
    u32 ip, mask, gw, dns;
    u64 lease_end_ms, renew_ms;
    /* counters */
    u64 rx_packets, tx_packets, rx_bytes, tx_bytes, rx_dropped, tx_errors;
};

#define NET_MAX_IF 4
extern struct netif *g_netifs[NET_MAX_IF];
extern int g_nnetif;

/* netif.c */
void net_init(void);                              /* finds the cards, starts the thread */
void net_register(struct netif *n);
struct netif *net_route(u32 dst, u32 *next_hop);  /* the interface and the next hop for dst */
int  net_send_eth(struct netif *n, const u8 *dst_mac, u16 type, const void *payload, u32 len);
u64  net_ms(void);
void net_kick(void);                              /* the thread polls now (after a send) */

/* drivers (kernel/drivers) */
void e1000_probe(void);
void rtl8139_probe(void);
void virtio_net_probe(void);

/* arp.c */
void arp_input(struct netif *n, const u8 *p, u32 len);
bool arp_lookup(u32 ip, u8 mac[6]);
void arp_request(struct netif *n, u32 ip);
/* the MAC of ip, asked for and waited for up to ms (process context) */
bool arp_resolve(struct netif *n, u32 ip, u8 mac[6], u32 ms);
void arp_add(u32 ip, const u8 mac[6]);

/* ip.c */
u16  ip_checksum(const void *data, u32 len, u32 start);
void ip_input(struct netif *n, const u8 *p, u32 len);
/* sends an IPv4 packet; wait_ms > 0 lets it wait for ARP (process context) */
int  ip_send(u32 src, u32 dst, u8 proto, const void *payload, u32 len, u32 wait_ms);
int  ip_send_if(struct netif *n, u32 src, u32 dst, u8 proto, const void *payload, u32 len, u32 wait_ms);
int  icmp_ping(u32 ip, u32 timeout_ms, u16 seq);    /* microseconds, or < 0 */

/* udp.c */
void udp_input(struct netif *n, u32 src, u32 dst, const u8 *p, u32 len);
int  udp_send(struct netif *n, u32 src, u16 sport, u32 dst, u16 dport, const void *data, u32 len, u32 wait_ms);

/* tcp.c */
void tcp_input(struct netif *n, u32 src, u32 dst, const u8 *p, u32 len);
void tcp_timer(void);

/* dhcp.c */
void dhcp_start(struct netif *n);
void dhcp_timer(void);
bool dhcp_input(struct netif *n, const u8 *p, u32 len);  /* true when it was for DHCP (port 68) */

/* sock.c: the sockets and their system calls */
#define MAX_SOCKS 64
#define SOCK_BUF  (64 * 1024)
#define UDP_QUEUE 32

struct dgram { u32 from_ip; u16 from_port; u16 len; u8 data[NET_MTU]; };

struct sock {
    bool used;
    int type;                       /* SOCK_UDP, SOCK_TCP */
    int owner;                      /* pid */
    int state;                      /* SOCKS_* */
    u32 lip, rip;
    u16 lport, rport;
    struct wait_queue wq;           /* waiting for data, a connection, room */
    int err;                        /* the error a waiting call gets (reset, refused) */
    /* UDP */
    struct dgram *dq;
    int dq_head, dq_n;
    /* TCP */
    u8 *rbuf, *sbuf;                /* rings of SOCK_BUF bytes */
    u32 rhead, rlen;                /* received, not read yet */
    u32 shead, slen;                /* to send: from snd_una */
    u32 snd_una, snd_nxt, snd_wnd, iss;
    u32 rcv_nxt, irs;
    bool fin_rcvd, fin_sent, fin_acked, close_req;
    u64 rto_ms, rto_at, tw_until;
    int retries;
    u16 mss;
    struct sock *parent;            /* a connection a listener has not handed out yet */
    int backlog, nready;
};
extern struct sock g_socks[MAX_SOCKS];
struct sock *sock_alloc(int type, int owner);
void sock_free(struct sock *s);
void net_proc_exit(int pid);
i64  sys_net(u64 n, u64 a, u64 b, u64 c, u64 d);    /* the network system calls */
void tcp_close_sock(struct sock *s);
void tcp_reset_all(struct netif *n);
int  tcp_send_more(struct sock *s);
void tcp_send_syn(struct sock *s);
void tcp_window_update(struct sock *s);

/* byte order */
static inline u16 htons(u16 v) { return (u16)(v << 8 | v >> 8); }
static inline u32 htonl(u32 v) { return __builtin_bswap32(v); }
#define ntohs htons
#define ntohl htonl
static inline u16 rd16(const u8 *p) { return (u16)(p[0] << 8 | p[1]); }
static inline u32 rd32(const u8 *p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }
static inline void wr16(u8 *p, u16 v) { p[0] = (u8)(v >> 8); p[1] = (u8)v; }
static inline void wr32(u8 *p, u32 v) { p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v; }

#endif
