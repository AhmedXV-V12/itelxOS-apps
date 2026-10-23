/* libitx: the userspace library of itelx. System calls (abi/itx_abi.h) and
 * the small C library every itelx program is built with. Calls return a
 * negative error code on failure (there is no errno). */
#ifndef LIBITX_H
#define LIBITX_H

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdbool.h>
#include "../../../abi/itx_abi.h"

typedef long ssize_t;

/* ---- system calls */
long  itx_syscall(long n, long a, long b, long c, long d, long e, long f);
void  exit(int code) __attribute__((noreturn));
ssize_t write(int fd, const void *buf, size_t len);
ssize_t read(int fd, void *buf, size_t len);
int   open(const char *path, int flags);
int   close(int fd);
long  lseek(int fd, long off, int whence);
int   stat(const char *path, struct itx_stat *st);
int   fstat(int fd, struct itx_stat *st);
int   readdir(int fd, struct itx_dirent *de);          /* 1 entry, 0 end, < 0 error */
int   mkdir(const char *path);
int   remove(const char *path);
int   chdir(const char *path);
int   getcwd(char *buf, size_t len);
int   spawn(const char *path, const char *const *argv, int argc, int flags);
int   waitpid(int pid, int *status, int flags);         /* WAIT_NOHANG: 0 when none is done */
int   getpid(void);
int   getppid(void);
int   sleep_ms(long ms);
long  uptime_ms(void);
void *sbrk_to(void *end);
int   kill(int pid);
int   proclist(struct itx_procinfo *out, int max);
int   sysinfo(struct itx_sysinfo *si);
int   tty_mode(int mode);
int   tty_setfg(int pid);
long  unix_time(void);
int   reboot(void);
int   poweroff(void);
void  yield(void);
int   rename(const char *from, const char *to);
long  klog_read(char *buf, size_t len);
long  mounts(char *buf, size_t len);
int   truncate(const char *path, long size);
int   sync(void);
int   tty_info(struct itx_ttyinfo *ti);
int   last_error(char *buf, size_t len);
int   tty_key(struct itx_key *k);
int   devlist(struct itx_devinfo *out, int max);       /* USB devices -> count */
int   eject(const char *path);                         /* a volume in /media */
int   xvr_check(const char *path, char *why, size_t len);   /* 0: a valid XVR executable */
/* itelx 1.4; on an older kernel they return -ENOSYS */
int   cpustat(struct itx_cpustat *s);
int   hwinfo(struct itx_hwinfo *h);
int   pcilist(struct itx_pciinfo *out, int max);       /* -> count */
int   disklist(struct itx_diskinfo *out, int max);     /* -> count */
int   fsinfo(const char *path, struct itx_fsinfo *fi);
int   tty_keywait(struct itx_key *k, long ms);         /* raw mode; -EAGAIN: no key within ms */

#define WAIT_NOHANG 1

/* ---- memory */
void *malloc(size_t n);
void *calloc(size_t n, size_t m);
void *realloc(void *p, size_t n);
void  free(void *p);

/* ---- strings */
void  *memcpy(void *d, const void *s, size_t n);
void  *memmove(void *d, const void *s, size_t n);
void  *memset(void *d, int c, size_t n);
int    memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
int    strcmp(const char *a, const char *b);
int    strncmp(const char *a, const char *b, size_t n);
char  *strcpy(char *d, const char *s);
char  *strncpy(char *d, const char *s, size_t n);
size_t strlcpy(char *d, const char *s, size_t n);
size_t strlcat(char *d, const char *s, size_t n);
char  *strcat(char *d, const char *s);
char  *strchr(const char *s, int c);
char  *strrchr(const char *s, int c);
char  *strstr(const char *h, const char *n);
char  *strdup(const char *s);
int    isdigit(int c);
int    isspace(int c);
int    isalpha(int c);
int    isalnum(int c);
int    toupper(int c);
int    tolower(int c);
long   strtol(const char *s, char **end, int base);
unsigned long strtoul(const char *s, char **end, int base);
long   atol(const char *s);
int    atoi(const char *s);
bool   ends_with(const char *s, const char *suffix);
const char *strerror(int err);         /* takes a negative or positive code */

/* UTF-8 */
int utf8_decode(const char *s, size_t len, uint32_t *cp);
int utf8_encode(uint32_t cp, char out[4]);
size_t utf8_len(const char *s);        /* code points */

/* ---- output */
int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int snprintf(char *buf, size_t size, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
int printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int dprintf(int fd, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
int puts(const char *s);
int putchar(int c);
void flush(void);

/* reads a whole file into a new buffer (NUL terminated); size in *len */
char *read_file(const char *path, size_t *len, size_t max);
int write_file(const char *path, const void *data, size_t len, bool append);

#endif
