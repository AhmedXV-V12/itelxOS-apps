/* itx.h for a PC: lets the sources of the XVC interpreter
 * (sdk/system/xvc) build and run on Linux, WSL or macOS, so XVC programs
 * can be tried before they go to itelx. The itelx calls the interpreter
 * makes are mapped onto POSIX in host.c ("make host-xvc").
 *
 * Differences from itelx: paths are the PC's (there is no /dex or
 * /media), run()/spawn() of an .xvr program starts a PC program of that
 * name, and colours and keys go through the PC's terminal.
 */
#ifndef HOST_ITX_H
#define HOST_ITX_H

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <sys/types.h>
#include "../../sdk/abi/itx_abi.h"

#define write    hx_write
#define read     hx_read
#define open     hx_open
#define close    hx_close
#define stat     hx_stat
#define readdir  hx_readdir
#define mkdir    hx_mkdir
#define remove   hx_remove
#define spawn    hx_spawn
#define waitpid  hx_waitpid
#define kill     hx_kill
#define getpid   hx_getpid
#define strerror hx_strerror
#define strtol   hx_strtol           /* itelx: decimal or 0x hex, never octal */

ssize_t hx_write(int fd, const void *buf, size_t len);
ssize_t hx_read(int fd, void *buf, size_t len);
int hx_open(const char *path, int flags);
int hx_close(int fd);
int hx_stat(const char *path, struct itx_stat *st);
int hx_readdir(int fd, struct itx_dirent *de);
int hx_mkdir(const char *path);
int hx_remove(const char *path);
int hx_spawn(const char *path, const char *const *argv, int argc, int flags);
int hx_waitpid(int pid, int *status, int flags);
int hx_kill(int pid);
int hx_getpid(void);
const char *hx_strerror(int err);
long hx_strtol(const char *s, char **end, int base);

void flush(void);
int sleep_ms(long ms);
long uptime_ms(void);
long unix_time(void);
int tty_mode(int mode);
int tty_setfg(int pid);
int tty_key(struct itx_key *k);
bool ends_with(const char *s, const char *suffix);
int utf8_decode(const char *s, size_t len, uint32_t *cp);
int utf8_encode(uint32_t cp, char out[4]);
char *read_file(const char *path, size_t *len, size_t max);
int write_file(const char *path, const void *data, size_t len, bool append);

#endif
