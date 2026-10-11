/* The itelx calls of the XVC interpreter, on POSIX (see itx.h here).
 * This file does not include the itx.h shim: it uses the PC's own calls
 * and translates flags, error codes and structures. */
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/* from sdk/abi/itx_abi.h (not included: its names collide with the PC's) */
#define ITX_O_WRONLY 0x0001
#define ITX_O_RDWR   0x0002
#define ITX_O_CREATE 0x0040
#define ITX_O_EXCL   0x0080
#define ITX_O_TRUNC  0x0200
#define ITX_O_APPEND 0x0400
#define VT_FILE 1
#define VT_DIR  2
#define TTY_MODE_RAW 1
#define SPAWN_FG 1
#define KEY_UP     0x110001
#define KEY_DOWN   0x110002
#define KEY_LEFT   0x110003
#define KEY_RIGHT  0x110004
#define KEY_HOME   0x110005
#define KEY_END    0x110006
#define KEY_DELETE 0x110007
#define KEY_PGUP   0x110008
#define KEY_PGDN   0x110009
#define KEY_ESC    0x11000A

struct itx_stat {
    uint64_t size, ino, mtime, ctime;
    uint32_t type, flags, links, reserved;
    uint64_t phys;
};
struct itx_dirent {
    uint64_t ino, size;
    uint32_t type, name_len;
    char name[256];
};
struct itx_key { uint32_t code, mods; };

int utf8_decode(const char *str, size_t len, uint32_t *cp);
static int err(void) { return -errno; }

/* ---------------------------------------------------------------- output */

void flush(void) { fflush(stdout); }

ssize_t hx_write(int fd, const void *buf, size_t len) {
    fflush(stdout);
    if (fd != 1) return write(fd, buf, len);
    /* itelx's "clear the screen" also puts the cursor at the top */
    const char *p = buf, *end = p + len, *s = p;
    for (; p + 3 < end; p++) {
        if (!memcmp(p, "\x1b[2J", 4)) {
            if (write(1, s, (size_t)(p + 4 - s)) < 0 || write(1, "\x1b[H", 3) < 0) return err();
            s = p + 4;
            p += 3;
        }
    }
    if (s < end && write(1, s, (size_t)(end - s)) < 0) return err();
    return (ssize_t)len;
}

/* standard input gives one line per read, as itelx's terminal does */
ssize_t hx_read(int fd, void *buf, size_t len) {
    if (fd != 0) return read(fd, buf, len);
    char *b = buf;
    size_t n = 0;
    while (n < len) {
        ssize_t r = read(0, b + n, 1);
        if (r < 0) return err();
        if (r == 0) break;
        if (b[n++] == '\n') break;
    }
    return (ssize_t)n;
}

/* ----------------------------------------------------------------- files */

#define MAXDIRS 16
static DIR *dirs[MAXDIRS];
#define DIRFD0 1000

int hx_open(const char *path, int flags) {
    struct stat st;
    if (!stat(path, &st) && S_ISDIR(st.st_mode)) {
        for (int i = 0; i < MAXDIRS; i++)
            if (!dirs[i]) {
                dirs[i] = opendir(path);
                return dirs[i] ? DIRFD0 + i : err();
            }
        return -EMFILE;
    }
    int f = (flags & ITX_O_RDWR) ? O_RDWR : (flags & ITX_O_WRONLY) ? O_WRONLY : O_RDONLY;
    if (flags & ITX_O_CREATE) f |= O_CREAT;
    if (flags & ITX_O_EXCL) f |= O_EXCL;
    if (flags & ITX_O_TRUNC) f |= O_TRUNC;
    if (flags & ITX_O_APPEND) f |= O_APPEND;
    int fd = open(path, f, 0644);
    return fd < 0 ? err() : fd;
}

int hx_close(int fd) {
    if (fd >= DIRFD0 && fd < DIRFD0 + MAXDIRS) {
        if (dirs[fd - DIRFD0]) closedir(dirs[fd - DIRFD0]);
        dirs[fd - DIRFD0] = NULL;
        return 0;
    }
    return close(fd) ? err() : 0;
}

int hx_readdir(int fd, struct itx_dirent *de) {
    if (fd < DIRFD0 || fd >= DIRFD0 + MAXDIRS || !dirs[fd - DIRFD0]) return -EBADF;
    struct dirent *e;
    do {
        errno = 0;
        e = readdir(dirs[fd - DIRFD0]);
        if (!e) return errno ? err() : 0;
    } while (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."));
    memset(de, 0, sizeof *de);
    de->ino = e->d_ino;
    de->type = e->d_type == DT_DIR ? VT_DIR : VT_FILE;
    de->name_len = (uint32_t)strlen(e->d_name);
    snprintf(de->name, sizeof de->name, "%s", e->d_name);
    return 1;
}

int hx_stat(const char *path, struct itx_stat *st) {
    struct stat s;
    if (stat(path, &s)) return err();
    memset(st, 0, sizeof *st);
    st->size = (uint64_t)s.st_size;
    st->ino = (uint64_t)s.st_ino;
    st->mtime = (uint64_t)s.st_mtime;
    st->ctime = (uint64_t)s.st_ctime;
    st->type = S_ISDIR(s.st_mode) ? VT_DIR : VT_FILE;
    st->links = (uint32_t)s.st_nlink;
    return 0;
}

int hx_mkdir(const char *path) { return mkdir(path, 0755) ? err() : 0; }
int hx_remove(const char *path) { return remove(path) ? err() : 0; }

char *read_file(const char *path, size_t *len, size_t max) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return NULL;
    struct stat st;
    if (fstat(fd, &st) || S_ISDIR(st.st_mode) || (size_t)st.st_size > max) { close(fd); return NULL; }
    char *buf = malloc((size_t)st.st_size + 1);
    if (!buf) { close(fd); return NULL; }
    size_t done = 0;
    while (done < (size_t)st.st_size) {
        ssize_t n = read(fd, buf + done, (size_t)st.st_size - done);
        if (n <= 0) break;
        done += (size_t)n;
    }
    close(fd);
    buf[done] = 0;
    if (len) *len = done;
    return buf;
}

int write_file(const char *path, const void *data, size_t len, bool append) {
    int fd = open(path, O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC), 0644);
    if (fd < 0) return err();
    size_t done = 0;
    while (done < len) {
        ssize_t n = write(fd, (const char *)data + done, len - done);
        if (n <= 0) { close(fd); return n < 0 ? err() : -EIO; }
        done += (size_t)n;
    }
    return close(fd) ? err() : 0;
}

/* ------------------------------------------------------------- programs */

int hx_spawn(const char *path, const char *const *argv, int argc, int flags) {
    char self[4096];
    const char *prog = path;
    if (!strcmp(path, "/system/xvc.xvr")) {
        ssize_t n = readlink("/proc/self/exe", self, sizeof self - 1);
        if (n > 0) { self[n] = 0; prog = self; }
    }
    char **av = calloc((size_t)argc + 1, sizeof *av);
    if (!av) return -ENOMEM;
    for (int i = 0; i < argc; i++) av[i] = (char *)argv[i];
    fflush(stdout);
    pid_t pid = fork();
    if (pid < 0) { free(av); return err(); }
    if (!pid) {
        execvp(prog, av);
        fprintf(stderr, "%s: %s\n", prog, strerror(errno));
        _exit(127);
    }
    free(av);
    return (int)pid;
}

int hx_waitpid(int pid, int *status, int flags) {
    int st;
    pid_t r = waitpid(pid, &st, 0);
    if (r < 0) return err();
    if (status) *status = WIFEXITED(st) ? WEXITSTATUS(st) : -WTERMSIG(st);
    return (int)r;
}

int hx_kill(int pid) { return kill(pid, SIGTERM) ? err() : 0; }
int hx_getpid(void) { return (int)getpid(); }
int tty_setfg(int pid) { return 0; }

int sleep_ms(long ms) {
    struct timespec t = { ms / 1000, (ms % 1000) * 1000000L };
    while (nanosleep(&t, &t) && errno == EINTR) {}
    return 0;
}

long uptime_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

long unix_time(void) { return (long)time(NULL); }

/* ------------------------------------------------------------- terminal */

static struct termios saved;
static bool raw_on;

int tty_mode(int mode) {
    if (!isatty(0)) return 0;
    if (mode == TTY_MODE_RAW && !raw_on) {
        tcgetattr(0, &saved);
        struct termios t = saved;
        t.c_lflag &= ~(tcflag_t)(ICANON | ECHO);
        t.c_cc[VMIN] = 1;
        t.c_cc[VTIME] = 0;
        tcsetattr(0, TCSANOW, &t);
        raw_on = true;
    } else if (mode != TTY_MODE_RAW && raw_on) {
        tcsetattr(0, TCSANOW, &saved);
        raw_on = false;
    }
    return 0;
}

static int getbyte(void) {
    unsigned char c;
    return read(0, &c, 1) == 1 ? c : -1;
}

int tty_key(struct itx_key *k) {
    int c = getbyte();
    if (c < 0) return -EIO;
    k->mods = 0;
    if (c == 0x1B) {
        int a = getbyte();
        if (a != '[' && a != 'O') { k->code = KEY_ESC; return 0; }
        int b = getbyte(), n = 0;
        while (b >= '0' && b <= '9') { n = n * 10 + (b - '0'); b = getbyte(); }
        static const uint32_t arrows[] = { KEY_UP, KEY_DOWN, KEY_RIGHT, KEY_LEFT };
        if (b >= 'A' && b <= 'D') k->code = arrows[b - 'A'];
        else if (b == 'H') k->code = KEY_HOME;
        else if (b == 'F') k->code = KEY_END;
        else if (b == '~' && n == 3) k->code = KEY_DELETE;
        else if (b == '~' && n == 5) k->code = KEY_PGUP;
        else if (b == '~' && n == 6) k->code = KEY_PGDN;
        else k->code = KEY_ESC;
        return 0;
    }
    if (c == '\r') c = '\n';
    if (c == 0x7F) c = '\b';
    if (c >= 0x80) {
        char b[4] = { (char)c };
        int need = (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : 4;
        for (int i = 1; i < need; i++) { int x = getbyte(); if (x < 0) break; b[i] = (char)x; }
        utf8_decode(b, (size_t)need, &k->code);
        return 0;
    }
    k->code = (uint32_t)c;
    return 0;
}

/* ---------------------------------------------------- the small library */

bool ends_with(const char *s, const char *suf) {
    size_t a = strlen(s), b = strlen(suf);
    return a >= b && !strcmp(s + a - b, suf);
}

const char *hx_strerror(int e) { return strerror(e < 0 ? -e : e); }

/* libitx's strtol: base 0 means decimal, or hex after 0x (no octal) */
long hx_strtol(const char *s, char **end, int base) {
    while (*s == ' ' || (*s >= '\t' && *s <= '\r')) s++;
    int neg = *s == '-';
    if (*s == '-' || *s == '+') s++;
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
        v = v * (unsigned long)base + (unsigned long)d;
    }
    if (end) *end = (char *)s;
    return neg ? -(long)v : (long)v;
}

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
