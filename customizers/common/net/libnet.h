/* libnet: the network for programs on the customizer kernels
 * (customizers/kernel; system calls in itx_net.h): addresses, sockets, DNS,
 * an HTTP client and server, and the network commands of the File saver
 * and data_place terminals (net, ping, dns, fetch). */
#ifndef LIBNET_H
#define LIBNET_H

#include <itx.h>
#include <kit.h>
#include "../../kernel/src/abi/itx_net.h"

/* addresses */
bool ip_parse(const char *s, uint32_t *ip);           /* "192.168.1.10" */
char *ip_str(char *out, size_t len, uint32_t ip);
const char *net_error(int err);                       /* strerror with the network's errors */

/* system calls */
int  net_info(struct itx_netinfo *out, int max);      /* -ENOSYS: a kernel without the network */
int  net_config(int index, const struct itx_netconf *c);
int  net_ping(uint32_t ip, long timeout_ms, int seq); /* microseconds */
int  sock_open(int type);
int  sock_bind(int s, int port);
int  sock_connect(int s, uint32_t ip, int port, long timeout_ms);
int  sock_listen(int s, int backlog);
int  sock_accept(int s, struct itx_sockaddr *peer, long timeout_ms);
long sock_send(int s, const void *buf, size_t len, long timeout_ms);
long sock_recv(int s, void *buf, size_t len, long timeout_ms);
long sock_sendto(int s, const void *buf, size_t len, uint32_t ip, int port);
long sock_recvfrom(int s, void *buf, size_t len, struct itx_sockaddr *from, long timeout_ms);
int  sock_close(int s);
int  sock_info(int s, struct itx_sockinfo *si);
bool sock_send_all(int s, const void *buf, size_t len);

/* the name of a computer to its address: a dotted address as it is, else
   DNS (the server DHCP gave, or 8.8.8.8) */
int  net_resolve(const char *name, uint32_t *ip);
bool net_ready(long wait_ms);                         /* an interface with an address (waits for DHCP) */

/* HTTP/1.0 GET of a URL (http:// only) into a file; progress on the
   terminal when show is set. Returns the bytes written, or < 0. */
long http_get(const char *url, const char *file, bool show, int *status);

/* the commands */
int  cmd_net(int argc, char **argv);                  /* net [dhcp | static IP/BITS GATEWAY DNS] */
int  cmd_ping(int argc, char **argv);                 /* ping HOST [count] */
int  cmd_dns(int argc, char **argv);                  /* dns NAME */
int  cmd_fetch(int argc, char **argv);                /* fetch URL [FILE] */
void net_help(void);

#endif
