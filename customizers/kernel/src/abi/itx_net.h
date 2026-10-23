/* The network interface of the customizer kernels ("itelx 1.4 net" and
 * "itelx 1.4 data", customizers/kernel): system call numbers, error codes
 * and structures, shared by the kernel and the programs (libnet).
 *
 * The numbers start at 200, away from itelx's own (0-46 in 1.4), so a
 * later itelx can add calls without meeting these. On a kernel without a
 * network the calls return -ENOSYS.
 *
 * Addresses and ports are in host order (192.168.1.10 = 0xC0A8010A).
 */
#ifndef ITX_NET_H
#define ITX_NET_H

#include <stdint.h>

#define SYS_NET_INFO      200   /* struct itx_netinfo*, max -> count of interfaces */
#define SYS_NET_CONFIG    201   /* index, const struct itx_netconf* */
#define SYS_NET_PING      202   /* ip, timeout ms, seq -> microseconds, or < 0 */
#define SYS_SOCKET        203   /* SOCK_UDP or SOCK_TCP -> a socket number */
#define SYS_SOCK_BIND     204   /* sock, port (0: any) */
#define SYS_SOCK_CONNECT  205   /* sock, const struct itx_sockaddr*, timeout ms */
#define SYS_SOCK_LISTEN   206   /* sock, backlog */
#define SYS_SOCK_ACCEPT   207   /* sock, struct itx_sockaddr* (peer, may be 0), timeout ms -> a new socket */
#define SYS_SOCK_SEND     208   /* sock, buf, len, timeout ms -> bytes (TCP) */
#define SYS_SOCK_RECV     209   /* sock, buf, len, timeout ms -> bytes, 0 when the peer closed */
#define SYS_SOCK_SENDTO   210   /* sock, buf, len, const struct itx_sockaddr* (UDP) */
#define SYS_SOCK_RECVFROM 211   /* sock, struct itx_dgram* (UDP; buffer, length, sender), timeout ms */
#define SYS_SOCK_CLOSE    212   /* sock */
#define SYS_SOCK_INFO     213   /* sock, struct itx_sockinfo* */

#define SOCK_UDP 1
#define SOCK_TCP 2

/* errors of the network (the others are in itx_abi.h) */
#define EADDRINUSE    98
#define ENETDOWN     100
#define ENETUNREACH  101
#define ECONNRESET   104
#define ENOTCONN     107
#define ETIMEDOUT    110
#define ECONNREFUSED 111
#define EHOSTUNREACH 113

#define NETF_UP    1          /* the interface is set up (an address) */
#define NETF_LINK  2          /* the cable is in (link up) */
#define NETF_DHCP  4          /* the address comes from DHCP */

struct itx_netinfo {
    char name[8];             /* eth0, eth1 ... */
    char driver[16];          /* e1000, rtl8139, virtio-net */
    char model[48];           /* what the card is */
    uint8_t mac[6];
    uint16_t pad;
    uint32_t flags;           /* NETF_* */
    uint32_t ip, mask, gateway, dns;
    uint32_t lease_s;         /* DHCP: seconds of the lease left, 0 static */
    uint32_t speed_mbps;      /* 0 unknown */
    uint64_t rx_packets, tx_packets, rx_bytes, tx_bytes, rx_dropped, tx_errors;
};

struct itx_netconf {
    uint32_t dhcp;            /* 1: ask DHCP (again); 0: the addresses below */
    uint32_t ip, mask, gateway, dns;
};

struct itx_sockaddr {
    uint32_t ip;
    uint16_t port;
    uint16_t pad;
};

struct itx_dgram {
    void *buf;                /* where the datagram goes */
    uint32_t len;             /* its room; the datagram's length on return */
    uint32_t pad;
    struct itx_sockaddr from;
};

#define SOCKS_CLOSED      0
#define SOCKS_LISTEN      1
#define SOCKS_SYN_SENT    2
#define SOCKS_SYN_RCVD    3
#define SOCKS_ESTABLISHED 4
#define SOCKS_FIN_WAIT1   5
#define SOCKS_FIN_WAIT2   6
#define SOCKS_CLOSE_WAIT  7
#define SOCKS_CLOSING     8
#define SOCKS_LAST_ACK    9
#define SOCKS_TIME_WAIT   10
#define SOCKS_UDP         11

struct itx_sockinfo {
    uint32_t type, state;
    struct itx_sockaddr local, remote;
    uint32_t rx_queued, tx_queued;   /* bytes waiting */
};

#endif
