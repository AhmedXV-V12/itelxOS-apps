/* libitx: system call wrappers and a small C library for itelx programs. */
#include "include/itx.h"

/* ------------------------------------------------------------ syscalls */

long itx_syscall(long n, long a, long b, long c, long d, long e, long f) {
    long r;
    register long r10 __asm__("r10") = d;
    register long r8 __asm__("r8") = e;
    register long r9 __asm__("r9") = f;
    __asm__ volatile("int $0x80"
                     : "=a"(r)
                     : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9)
                     : "memory", "rcx", "r11");
    return r;
}

#define S0(n) itx_syscall(n, 0, 0, 0, 0, 0, 0)
#define S1(n, a) itx_syscall(n, (long)(a), 0, 0, 0, 0, 0)
#define S2(n, a, b) itx_syscall(n, (long)(a), (long)(b), 0, 0, 0, 0)
#define S3(n, a, b, c) itx_syscall(n, (long)(a), (long)(b), (long)(c), 0, 0, 0)
#define S4(n, a, b, c, d) itx_syscall(n, (long)(a), (long)(b), (long)(c), (long)(d), 0, 0)

void exit(int code) {
    flush();
    S1(SYS_EXIT, code);
    for (;;) {}
}
ssize_t write(int fd, const void *buf, size_t len) { return S3(SYS_WRITE, fd, buf, len); }
ssize_t read(int fd, void *buf, size_t len) { return S3(SYS_READ, fd, buf, len); }
int open(const char *path, int flags) { return (int)S2(SYS_OPEN, path, flags); }
int close(int fd) { return (int)S1(SYS_CLOSE, fd); }
long lseek(int fd, long off, int whence) { return S3(SYS_SEEK, fd, off, whence); }
int stat(const char *path, struct itx_stat *st) { return (int)S2(SYS_STAT, path, st); }
int fstat(int fd, struct itx_stat *st) { return (int)S2(SYS_FSTAT, fd, st); }
int readdir(int fd, struct itx_dirent *de) { return (int)S2(SYS_READDIR, fd, de); }
int mkdir(const char *path) { return (int)S1(SYS_MKDIR, path); }
int remove(const char *path) { return (int)S1(SYS_REMOVE, path); }
int chdir(const char *path) { return (int)S1(SYS_CHDIR, path); }
int getcwd(char *buf, size_t len) { return (int)S2(SYS_GETCWD, buf, len); }
int spawn(const char *path, const char *const *argv, int argc, int flags) { return (int)S4(SYS_SPAWN, path, argv, argc, flags); }
int waitpid(int pid, int *status, int flags) { return (int)S3(SYS_WAIT, pid, status, flags); }
int getpid(void) { return (int)S0(SYS_GETPID); }
int getppid(void) { return (int)S0(SYS_GETPPID); }
int sleep_ms(long ms) { return (int)S1(SYS_SLEEP, ms); }
long uptime_ms(void) { return S0(SYS_UPTIME); }
void *sbrk_to(void *end) { return (void *)S1(SYS_BRK, end); }
int kill(int pid) { return (int)S1(SYS_KILL, pid); }
int proclist(struct itx_procinfo *out, int max) { return (int)S2(SYS_PROCLIST, out, max); }
int sysinfo(struct itx_sysinfo *si) { return (int)S1(SYS_SYSINFO, si); }
int tty_mode(int mode) { return (int)S1(SYS_TTY_MODE, mode); }
int tty_setfg(int pid) { return (int)S1(SYS_TTY_SETFG, pid); }
long unix_time(void) { return S0(SYS_TIME); }
int reboot(void) { flush(); return (int)S0(SYS_REBOOT); }
int poweroff(void) { flush(); return (int)S0(SYS_POWEROFF); }
void yield(void) { S0(SYS_YIELD); }
int rename(const char *from, const char *to) { return (int)S2(SYS_RENAME, from, to); }
long klog_read(char *buf, size_t len) { return S2(SYS_KLOG, buf, len); }
long mounts(char *buf, size_t len) { return S2(SYS_MOUNTS, buf, len); }
int truncate(const char *path, long size) { return (int)S2(SYS_TRUNCATE, path, size); }
int sync(void) { return (int)S0(SYS_SYNC); }
int tty_info(struct itx_ttyinfo *ti) { return (int)S1(SYS_TTY_INFO, ti); }
int last_error(char *buf, size_t len) { return (int)S2(SYS_LASTERR, buf, len); }
int tty_key(struct itx_key *k) { return (int)S1(SYS_TTY_KEY, k); }
int devlist(struct itx_devinfo *out, int max) { return (int)S2(SYS_DEVLIST, out, max); }
int eject(const char *path) { return (int)S1(SYS_EJECT, path); }
int xvr_check(const char *path, char *why, size_t len) { return (int)S3(SYS_XVRCHECK, path, why, len); }

/* -------------------------------------------------------------- memory */

/* first fit over a list of blocks taken from the kernel with brk */
struct blk {
    size_t size;            /* payload bytes */
    struct blk *next;       /* next free block (free blocks only) */
    uint32_t magic;
    uint32_t free;
};
#define MAGIC 0x4D454D49u
static struct blk *free_list;
static char *heap_end;

static struct blk *grow(size_t need) {
    size_t total = (need + sizeof(struct blk) + 65535) & ~(size_t)65535;
    if (!heap_end) heap_end = sbrk_to(0);
    char *old = heap_end;
    char *now = sbrk_to(old + total);
    if ((long)now < 0 || now < old + total) return NULL;
    heap_end = now;
    struct blk *b = (struct blk *)old;
    b->size = (size_t)(now - old) - sizeof(struct blk);
    b->magic = MAGIC;
    b->free = 1;
    return b;
}

void *malloc(size_t n) {
    if (n == 0) n = 1;
    if (n > ((size_t)1 << 40)) return NULL;
    n = (n + 15) & ~(size_t)15;
    struct blk **pp = &free_list, *b;
    for (b = free_list; b; pp = &b->next, b = b->next) if (b->size >= n) break;
    if (!b) {
        b = grow(n);
        if (!b) return NULL;
    } else {
        *pp = b->next;
    }
    if (b->size >= n + sizeof(struct blk) + 32) {
        struct blk *rest = (struct blk *)((char *)(b + 1) + n);
        rest->size = b->size - n - sizeof(struct blk);
        rest->magic = MAGIC;
        rest->free = 1;
        rest->next = free_list;
        free_list = rest;
        b->size = n;
    }
    b->free = 0;
    b->next = NULL;
    return b + 1;
}

void free(void *p) {
    if (!p) return;
    struct blk *b = (struct blk *)p - 1;
    if (b->magic != MAGIC || b->free) {
        dprintf(2, "libitx: free() of a bad or freed pointer %p\n", p);
        exit(134);
    }
    b->free = 1;
    /* merge with a free block right after it, when it is at the head */
    b->next = free_list;
    free_list = b;
}

void *calloc(size_t n, size_t m) {
    if (m && n > (size_t)-1 / m) return NULL;
    void *p = malloc(n * m);
    if (p) memset(p, 0, n * m);
    return p;
}

void *realloc(void *p, size_t n) {
    if (!p) return malloc(n);
    struct blk *b = (struct blk *)p - 1;
    if (b->magic != MAGIC || b->free) { dprintf(2, "libitx: realloc() of a bad pointer\n"); exit(134); }
    if (n <= b->size) return p;
    void *q = malloc(n);
    if (!q) return NULL;
    memcpy(q, p, b->size);
    free(p);
    return q;
}

/* ------------------------------------------------------------- strings */

void *memcpy(void *d, const void *s, size_t n) {
    char *a = d;
    const char *b = s;
    while (n--) *a++ = *b++;
    return d;
}
void *memmove(void *d, const void *s, size_t n) {
    char *a = d;
    const char *b = s;
    if (a < b) while (n--) *a++ = *b++;
    else { a += n; b += n; while (n--) *--a = *--b; }
    return d;
}
void *memset(void *d, int c, size_t n) {
    char *a = d;
    while (n--) *a++ = (char)c;
    return d;
}
int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *x = a, *y = b;
    for (size_t i = 0; i < n; i++) if (x[i] != y[i]) return x[i] - y[i];
    return 0;
}
size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
int strcmp(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return (unsigned char)*a - (unsigned char)*b; }
int strncmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) { if (a[i] != b[i] || !a[i]) return (unsigned char)a[i] - (unsigned char)b[i]; }
    return 0;
}
char *strcpy(char *d, const char *s) { char *r = d; while ((*d++ = *s++)) {} return r; }
char *strncpy(char *d, const char *s, size_t n) {
    size_t i = 0;
    for (; i < n && s[i]; i++) d[i] = s[i];
    for (; i < n; i++) d[i] = 0;
    return d;
}
size_t strlcpy(char *d, const char *s, size_t n) {
    size_t l = strlen(s);
    if (n) { size_t c = l < n - 1 ? l : n - 1; memcpy(d, s, c); d[c] = 0; }
    return l;
}
size_t strlcat(char *d, const char *s, size_t n) {
    size_t dl = 0;
    while (dl < n && d[dl]) dl++;
    if (dl == n) return n + strlen(s);
    return dl + strlcpy(d + dl, s, n - dl);
}
char *strcat(char *d, const char *s) { strcpy(d + strlen(d), s); return d; }
char *strchr(const char *s, int c) { for (;; s++) { if (*s == (char)c) return (char *)s; if (!*s) return NULL; } }
char *strrchr(const char *s, int c) { const char *r = NULL; for (;; s++) { if (*s == (char)c) r = s; if (!*s) return (char *)r; } }
char *strstr(const char *h, const char *n) {
    size_t l = strlen(n);
    if (!l) return (char *)h;
    for (; *h; h++) if (!strncmp(h, n, l)) return (char *)h;
    return NULL;
}
char *strdup(const char *s) { size_t l = strlen(s) + 1; char *p = malloc(l); if (p) memcpy(p, s, l); return p; }
int isdigit(int c) { return c >= '0' && c <= '9'; }
int isspace(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }
int isalpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
int isalnum(int c) { return isalpha(c) || isdigit(c); }
int toupper(int c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }
int tolower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

unsigned long strtoul(const char *s, char **end, int base) {
    while (isspace(*s)) s++;
    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { s += 2; base = 16; }
    if (base == 0) base = 10;
    unsigned long v = 0;
    for (;; s++) {
        int d;
        if (*s >= '0' && *s <= '9') d = *s - '0';
        else if (*s >= 'a' && *s <= 'z') d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'Z') d = *s - 'A' + 10;
        else break;
        if (d >= base) break;
        v = v * base + d;
    }
    if (end) *end = (char *)s;
    return v;
}
long strtol(const char *s, char **end, int base) {
    while (isspace(*s)) s++;
    int neg = *s == '-';
    if (*s == '-' || *s == '+') s++;
    unsigned long v = strtoul(s, end, base);
    return neg ? -(long)v : (long)v;
}
long atol(const char *s) { return strtol(s, NULL, 10); }
int atoi(const char *s) { return (int)strtol(s, NULL, 10); }
bool ends_with(const char *s, const char *suf) {
    size_t a = strlen(s), b = strlen(suf);
    return a >= b && !strcmp(s + a - b, suf);
}

const char *strerror(int e) {
    if (e < 0) e = -e;
    switch (e) {
    case 0: return "success";
    case EPERM: return "operation not permitted";
    case ENOENT: return "no such file or directory";
    case ESRCH: return "no such process";
    case EINTR: return "interrupted";
    case EIO: return "input/output error";
    case E2BIG: return "argument list too long";
    case ENOEXEC: return "not a valid XVR executable";
    case EBADF: return "bad file handle";
    case ECHILD: return "no child process";
    case EAGAIN: return "try again";
    case ENOMEM: return "out of memory";
    case EACCES: return "access denied";
    case EFAULT: return "bad address";
    case EBUSY: return "in use";
    case EEXIST: return "already exists";
    case EXDEV: return "not on the same filesystem";
    case ENODEV: return "no such device";
    case ENOTDIR: return "not a directory";
    case EISDIR: return "is a directory";
    case EINVAL: return "invalid argument";
    case EMFILE: return "too many open files";
    case EFBIG: return "file too large";
    case ENOSPC: return "no space left on the disk";
    case ESPIPE: return "cannot seek";
    case EROFS: return "read-only filesystem";
    case ERANGE: return "out of range";
    case ENAMETOOLONG: return "name too long";
    case ENOSYS: return "not implemented";
    case ENOTEMPTY: return "directory not empty";
    case ECORRUPT: return "the filesystem is damaged";
    case ECANCELED: return "cancelled";
    }
    return "unknown error";
}

/* --------------------------------------------------------------- UTF-8 */

int utf8_decode(const char *str, size_t len, uint32_t *cp) {
    const unsigned char *s = (const unsigned char *)str;
    if (!len) { *cp = 0xFFFD; return 1; }
    if (s[0] < 0x80) { *cp = s[0]; return 1; }
    int n;
    uint32_t v, min;
    if ((s[0] & 0xE0) == 0xC0) { n = 2; v = s[0] & 0x1F; min = 0x80; }
    else if ((s[0] & 0xF0) == 0xE0) { n = 3; v = s[0] & 0x0F; min = 0x800; }
    else if ((s[0] & 0xF8) == 0xF0) { n = 4; v = s[0] & 0x07; min = 0x10000; }
    else { *cp = 0xFFFD; return 1; }
    if ((size_t)n > len) { *cp = 0xFFFD; return 1; }
    for (int i = 1; i < n; i++) {
        if ((s[i] & 0xC0) != 0x80) { *cp = 0xFFFD; return i; }
        v = (v << 6) | (s[i] & 0x3F);
    }
    if (v < min || v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF)) v = 0xFFFD;
    *cp = v;
    return n;
}

int utf8_encode(uint32_t cp, char out[4]) {
    if (cp < 0x80) { out[0] = (char)cp; return 1; }
    if (cp < 0x800) { out[0] = (char)(0xC0 | (cp >> 6)); out[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12)); out[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18)); out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

size_t utf8_len(const char *s) {
    size_t n = 0, len = strlen(s);
    for (size_t i = 0; i < len; n++) {
        uint32_t cp;
        i += utf8_decode(s + i, len - i, &cp);
    }
    return n;
}

/* -------------------------------------------------------------- output */

struct out { char *buf; size_t size, pos; };

static void put(struct out *o, char c) {
    if (o->pos + 1 < o->size) o->buf[o->pos] = c;
    o->pos++;
}

static void put_num(struct out *o, unsigned long v, int base, int upper, int neg, int width, int zero, int left) {
    char tmp[24];
    const char *dg = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int n = 0;
    do { tmp[n++] = dg[v % base]; v /= base; } while (v);
    int len = n + neg;
    if (!left && !zero) for (int i = len; i < width; i++) put(o, ' ');
    if (neg) put(o, '-');
    if (!left && zero) for (int i = len; i < width; i++) put(o, '0');
    while (n) put(o, tmp[--n]);
    if (left) for (int i = len; i < width; i++) put(o, ' ');
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap) {
    struct out o = { buf, size, 0 };
    for (; *fmt; fmt++) {
        if (*fmt != '%') { put(&o, *fmt); continue; }
        fmt++;
        int left = 0, zero = 0, width = 0, prec = -1, lng = 0;
        for (;; fmt++) {
            if (*fmt == '-') left = 1;
            else if (*fmt == '0') zero = 1;
            else break;
        }
        if (*fmt == '*') { width = va_arg(ap, int); fmt++; }
        while (isdigit(*fmt)) width = width * 10 + (*fmt++ - '0');
        if (*fmt == '.') {
            fmt++;
            prec = 0;
            if (*fmt == '*') { prec = va_arg(ap, int); fmt++; }
            while (isdigit(*fmt)) prec = prec * 10 + (*fmt++ - '0');
        }
        while (*fmt == 'l') { lng++; fmt++; }
        if (*fmt == 'z') { lng = 1; fmt++; }
        switch (*fmt) {
        case 'd': case 'i': {
            long v = lng ? va_arg(ap, long) : va_arg(ap, int);
            int neg = v < 0;
            put_num(&o, neg ? -(unsigned long)v : (unsigned long)v, 10, 0, neg, width, zero, left);
            break;
        }
        case 'u': case 'x': case 'X': case 'o': {
            unsigned long v = lng ? va_arg(ap, unsigned long) : va_arg(ap, unsigned int);
            put_num(&o, v, *fmt == 'u' ? 10 : *fmt == 'o' ? 8 : 16, *fmt == 'X', 0, width, zero, left);
            break;
        }
        case 'p':
            put(&o, '0'); put(&o, 'x');
            put_num(&o, (unsigned long)va_arg(ap, void *), 16, 0, 0, 0, 0, 0);
            break;
        case 'c': put(&o, (char)va_arg(ap, int)); break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            size_t l = strlen(s);
            if (prec >= 0 && (size_t)prec < l) l = prec;
            /* width counts characters, not bytes, so Arabic columns line up */
            size_t chars = 0;
            for (size_t i = 0; i < l; i++) if (((unsigned char)s[i] & 0xC0) != 0x80) chars++;
            if (!left) for (int i = (int)chars; i < width; i++) put(&o, ' ');
            for (size_t i = 0; i < l; i++) put(&o, s[i]);
            if (left) for (int i = (int)chars; i < width; i++) put(&o, ' ');
            break;
        }
        case '%': put(&o, '%'); break;
        case 0: fmt--; break;
        default: put(&o, '%'); put(&o, *fmt);
        }
    }
    if (size) buf[o.pos < size ? o.pos : size - 1] = 0;
    return (int)o.pos;
}

int snprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

/* stdout is buffered per line */
static char obuf[4096];
static size_t olen;

void flush(void) {
    if (olen) { write(1, obuf, olen); olen = 0; }
}

static void out_bytes(const char *s, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (olen == sizeof obuf) flush();
        obuf[olen++] = s[i];
        if (s[i] == '\n') flush();
    }
}

int printf(const char *fmt, ...) {
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    out_bytes(buf, n < (int)sizeof buf ? (size_t)n : sizeof buf - 1);
    return n;
}

int dprintf(int fd, const char *fmt, ...) {
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (fd == 1) { out_bytes(buf, n < (int)sizeof buf ? (size_t)n : sizeof buf - 1); return n; }
    flush();
    write(fd, buf, n < (int)sizeof buf ? (size_t)n : sizeof buf - 1);
    return n;
}

int puts(const char *s) { out_bytes(s, strlen(s)); out_bytes("\n", 1); return 0; }
int putchar(int c) { char ch = (char)c; out_bytes(&ch, 1); return c; }

/* ---------------------------------------------------------------- files */

char *read_file(const char *path, size_t *len, size_t max) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return NULL;
    struct itx_stat st;
    if (fstat(fd, &st) < 0 || st.size > max) { close(fd); return NULL; }
    char *buf = malloc(st.size + 1);
    if (!buf) { close(fd); return NULL; }
    size_t done = 0;
    while (done < st.size) {
        ssize_t n = read(fd, buf + done, st.size - done);
        if (n <= 0) break;
        done += n;
    }
    close(fd);
    buf[done] = 0;
    if (len) *len = done;
    return buf;
}

int write_file(const char *path, const void *data, size_t len, bool append) {
    int fd = open(path, O_WRONLY | O_CREATE | (append ? O_APPEND : O_TRUNC));
    if (fd < 0) return fd;
    size_t done = 0;
    while (done < len) {
        ssize_t n = write(fd, (const char *)data + done, len - done);
        if (n <= 0) { close(fd); return n < 0 ? (int)n : -EIO; }
        done += n;
    }
    return close(fd);
}

void __libitx_init(void) {
    heap_end = sbrk_to(0);
}
