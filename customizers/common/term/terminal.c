/* The itelx terminal: the command interpreter users work in.
 *
 * Commands (English; file names may be Arabic or any UTF-8):
 *   ls [path]                 list a directory
 *   cd= <path>                change directory
 *   del [-r] <path>           delete a file or a directory (-r: with everything in it)
 *   add -r <dir>              create a directory
 *   add -f <file> [text]      create a file (with optional text)
 *   copy [-r] <from> <to>     copy a file (-r: a directory), between any disks
 *   move <from> <to>          rename or move, between any disks
 *   run <file.xvc|file.xvr> [args] [&]    run an XVC program or an XVR executable
 *   <path to a .xvr file> [args] [&]      e.g. ./app.xvr or /dex/hello.xvr
 *   install <file.xvr|file.xvc> [name]    put a program (from a USB stick, say)
 *                                         into /dex, to run it by its name
 *   usb                       the USB devices (they are also in /media)
 *   eject <volume>            write a volume out and unmount it, before unplugging
 *   sys                       the system (sys.c): version, start, disks, memory, owner
 *   sys.info                  processor, memory and graphics use over one second
 *   sys.info.device           the hardware, from the processor to the graphics
 *   sys.all                   all three
 *   sys.top [s]               every process and what it uses, live (/system/top.xvr)
 * and: cat, write, append, hex, info, pwd, ps, kill, mem, log, mounts, clear,
 * echo, sleep, jobs, lang, sync, reboot, poweroff, help, exit
 *
 * A trailing "&" runs the program in the background; the terminal goes on
 * at once, and reports when it ends. Ctrl+C stops a program in the
 * foreground. Alt+Shift switches the keyboard between English and Arabic.
 *
 * This copy is the itelx 1.4 terminal (system/terminal/terminal.c, MPL-2.0)
 * with hooks for the terminals of the customizer apps (term.h): their own
 * start, banner, prompt, commands and help; and the helpers below exported
 * to them as term_*. Everything else is unchanged.
 */
#include <itx.h>
#include "sys.h"
#include "term.h"

#define MAXARGS 32
#define LINE 4096

static char cwd[ITX_PATH_MAX] = "/";
static int jobs[32];
static int njobs;

static void err(const char *what, int code) {
    printf("\x1b[91m%s: %s\x1b[0m\n", what, strerror(code));
}

/* ---------------------------------------------------------------- parse */

static int split(char *line, char **argv) {
    int n = 0;
    char *p = line;
    while (*p && n < MAXARGS) {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
        if (!*p) break;
        char *out = p, *start = p;
        if (*p == '"') {
            p++;
            start = out = p;
            while (*p && *p != '"') *out++ = *p++;
            if (*p == '"') p++;
        } else {
            while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') p++;
            out = p;
        }
        char *next = *p ? p + 1 : p;
        *out = 0;
        argv[n++] = start;
        p = next;
    }
    return n;
}

/* --------------------------------------------------------------- helpers */

static const char *type_mark(uint32_t t) {
    return t == VT_DIR ? "dir " : t == VT_RAM ? "ram " : t == VT_DEV ? "dev " : "file";
}

/* "a/b" + "c" -> "a/b/c" */
static void join(char *out, size_t len, const char *dir, const char *name) {
    size_t l = strlen(dir);
    snprintf(out, len, "%s%s%s", dir, l && dir[l - 1] == '/' ? "" : "/", name);
}

static const char *base_name(const char *p) {
    static char b[ITX_NAME_MAX + 1];
    size_t l = strlen(p);
    while (l > 1 && p[l - 1] == '/') l--;
    size_t s = l;
    while (s > 0 && p[s - 1] != '/') s--;
    size_t n = l - s < ITX_NAME_MAX ? l - s : ITX_NAME_MAX;
    memcpy(b, p + s, n);
    b[n] = 0;
    return b;
}

/* an absolute path with . and .. worked out (as the kernel does) */
static void absolute(const char *path, char *out, size_t len) {
    char tmp[ITX_PATH_MAX * 2];
    if (path[0] == '/') strlcpy(tmp, path, sizeof tmp);
    else snprintf(tmp, sizeof tmp, "%s/%s", cwd, path);
    size_t o = 0;
    out[o++] = '/';
    char *p = tmp;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        char *s = p;
        while (*p && *p != '/') p++;
        size_t n = (size_t)(p - s);
        if (n == 1 && s[0] == '.') continue;
        if (n == 2 && s[0] == '.' && s[1] == '.') {
            while (o > 1 && out[o - 1] != '/') o--;
            if (o > 1) o--;
            continue;
        }
        if (o > 1 && o + 1 < len) out[o++] = '/';
        if (o + n >= len) break;
        memcpy(out + o, s, n);
        o += n;
    }
    out[o] = 0;
}

/* is b the same as a, or inside it? */
static bool within(const char *a, const char *b) {
    char x[ITX_PATH_MAX], y[ITX_PATH_MAX];
    absolute(a, x, sizeof x);
    absolute(b, y, sizeof y);
    size_t l = strlen(x);
    return !strncmp(x, y, l) && (y[l] == 0 || y[l] == '/' || l == 1);
}

static const char *speed_name(int s) {
    switch (s) {
    case 1: return "full";
    case 2: return "low";
    case 3: return "high";
    case 4: return "super";
    case 5: return "super+";
    }
    return "?";
}

static const char *kind_name(uint32_t k) {
    static const char *n[] = { "device", "keyboard", "mouse", "hid", "hub", "storage" };
    return k < 6 ? n[k] : "device";
}

static void human(uint64_t n, char *out, size_t len) {
    if (n < 1024) snprintf(out, len, "%lu B", (unsigned long)n);
    else if (n < 1024 * 1024) snprintf(out, len, "%lu KiB", (unsigned long)(n / 1024));
    else if (n < 1024ULL * 1024 * 1024) snprintf(out, len, "%lu MiB", (unsigned long)(n >> 20));
    else snprintf(out, len, "%lu GiB", (unsigned long)(n >> 30));
}

struct entry { char name[ITX_NAME_MAX + 1]; uint32_t type; uint64_t size; };

static int cmp_entry(const struct entry *a, const struct entry *b) {
    if ((a->type == VT_DIR) != (b->type == VT_DIR)) return a->type == VT_DIR ? -1 : 1;
    return strcmp(a->name, b->name);
}

static void cmd_ls(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) { err(path, fd); return; }
    struct itx_stat st;
    fstat(fd, &st);
    if (st.type != VT_DIR) {
        close(fd);
        char h[32];
        human(st.size, h, sizeof h);
        printf("%s  %10s  %s\n", type_mark(st.type), h, path);
        return;
    }
    int cap = 64, n = 0;
    struct entry *e = malloc(sizeof *e * cap);
    struct itx_dirent de;
    int r;
    while ((r = readdir(fd, &de)) == 1) {
        if (n == cap) { cap *= 2; e = realloc(e, sizeof *e * cap); if (!e) break; }
        strlcpy(e[n].name, de.name, sizeof e[n].name);
        e[n].type = de.type;
        e[n].size = de.size;
        n++;
    }
    close(fd);
    if (r < 0) err(path, r);
    for (int i = 1; i < n; i++) {                        /* insertion sort */
        struct entry k = e[i];
        int j = i - 1;
        while (j >= 0 && cmp_entry(&e[j], &k) > 0) { e[j + 1] = e[j]; j--; }
        e[j + 1] = k;
    }
    for (int i = 0; i < n; i++) {
        char h[32] = "";
        if (e[i].type != VT_DIR) human(e[i].size, h, sizeof h);
        if (e[i].type == VT_DIR) printf("\x1b[96m%s  %10s  %s/\x1b[0m\n", type_mark(e[i].type), h, e[i].name);
        else printf("%s  %10s  %s\n", type_mark(e[i].type), h, e[i].name);
    }
    if (!n) printf("(empty)\n");
    free(e);
}

static void cmd_cat(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) { err(path, fd); return; }
    struct itx_stat st;
    fstat(fd, &st);
    if (st.type == VT_RAM) { close(fd); printf("%s: RAM is binary; use: hex %s [offset] [count]\n", path, path); return; }
    char buf[4096];
    ssize_t n;
    bool last_nl = true;
    while ((n = read(fd, buf, sizeof buf)) > 0) { flush(); write(1, buf, n); last_nl = buf[n - 1] == '\n'; }
    if (n < 0) err(path, (int)n);
    if (!last_nl) putchar('\n');
    close(fd);
}

/* reads lines until a line with only "." and writes them to the file */
static void cmd_write(const char *path, bool append) {
    printf("Type the text; a line with only . ends it.\n");
    flush();
    size_t cap = 4096, len = 0;
    char *text = malloc(cap);
    char line[LINE];
    for (;;) {
        ssize_t n = read(0, line, sizeof line - 1);
        if (n <= 0) break;
        line[n] = 0;
        if (!strcmp(line, ".\n") || !strcmp(line, ".")) break;
        if (len + n + 1 > cap) { cap = (len + n + 1) * 2; text = realloc(text, cap); if (!text) return; }
        memcpy(text + len, line, n);
        len += n;
    }
    int r = write_file(path, text, len, append);
    free(text);
    if (r < 0) err(path, r); else printf("%lu bytes %s %s\n", (unsigned long)len, append ? "added to" : "written to", path);
}

static void cmd_hex(const char *path, const char *offs, const char *cnts) {
    unsigned long off = offs ? strtoul(offs, NULL, 0) : 0;
    unsigned long cnt = cnts ? strtoul(cnts, NULL, 0) : 256;
    if (cnt > 65536) cnt = 65536;
    int fd = open(path, O_RDONLY);
    if (fd < 0) { err(path, fd); return; }
    struct itx_stat st;
    fstat(fd, &st);
    if (lseek(fd, (long)off, SEEK_SET) < 0) { err(path, -ESPIPE); close(fd); return; }
    unsigned char *buf = malloc(cnt ? cnt : 1);
    ssize_t n = read(fd, buf, cnt);
    close(fd);
    if (n < 0) { err(path, (int)n); free(buf); return; }
    uint64_t base = st.type == VT_RAM ? st.phys + off : off;
    if (st.type == VT_RAM) printf("physical RAM at 0x%lx:\n", (unsigned long)base);
    for (ssize_t i = 0; i < n; i += 16) {
        printf("%012lx  ", (unsigned long)(base + i));
        for (int j = 0; j < 16; j++) {
            if (i + j < n) printf("%02x ", buf[i + j]);
            else printf("   ");
        }
        printf(" ");
        for (int j = 0; j < 16 && i + j < n; j++) putchar(buf[i + j] >= 0x20 && buf[i + j] < 0x7F ? buf[i + j] : '.');
        putchar('\n');
    }
    if (n == 0) printf("(nothing at that offset)\n");
    free(buf);
}

/* copies one file; an error is reported with the file it happened on */
static int copy_file(const char *a, const char *dst, uint64_t *total) {
    int in = open(a, O_RDONLY);
    if (in < 0) { err(a, in); return in; }
    int out = open(dst, O_WRONLY | O_CREATE | O_TRUNC);
    if (out < 0) { close(in); err(dst, out); return out; }
    char *buf = malloc(65536);
    int r = buf ? 0 : -ENOMEM;
    while (!r) {
        ssize_t n = read(in, buf, 65536);
        if (n == 0) break;
        if (n < 0) { r = (int)n; err(a, r); break; }
        ssize_t w = write(out, buf, n);
        if (w != n) { r = w < 0 ? (int)w : -ENOSPC; err(dst, r); break; }
        *total += n;
    }
    free(buf);
    close(in);
    close(out);
    return r;
}

/* the names in a directory, read before anything in it changes */
static char **list_dir(const char *path, int *count, int *err_out) {
    *count = 0;
    int fd = open(path, O_RDONLY);
    if (fd < 0) { *err_out = fd; return NULL; }
    int cap = 32, n = 0, r = 0;
    char **names = malloc(sizeof(char *) * cap);
    struct itx_dirent de;
    while (names && (r = readdir(fd, &de)) == 1) {
        if (n == cap) {
            cap *= 2;
            char **more = realloc(names, sizeof(char *) * cap);
            if (!more) { r = -ENOMEM; break; }
            names = more;
        }
        names[n++] = strdup(de.name);
    }
    close(fd);
    *count = n;
    *err_out = !names ? -ENOMEM : r < 0 ? r : 0;
    return names;
}

static void free_names(char **names, int n) {
    if (!names) return;
    for (int i = 0; i < n; i++) free(names[i]);
    free(names);
}

/* copies a file or a whole directory; the first error stops it */
static int copy_tree(const char *a, const char *b, uint64_t *total, int *files) {
    struct itx_stat st;
    int r = stat(a, &st);
    if (r) { err(a, r); return r; }
    if (st.type == VT_DEV) return 0;                  /* devices are not copied */
    if (st.type != VT_DIR) {
        r = copy_file(a, b, total);
        if (!r) (*files)++;
        return r;
    }
    r = mkdir(b);
    if (r && r != -EEXIST) { err(b, r); return r; }
    int n;
    char **names = list_dir(a, &n, &r);
    if (r) { err(a, r); free_names(names, n); return r; }
    char *x = malloc(ITX_PATH_MAX), *y = malloc(ITX_PATH_MAX);
    for (int i = 0; i < n && !r; i++) {
        join(x, ITX_PATH_MAX, a, names[i]);
        join(y, ITX_PATH_MAX, b, names[i]);
        r = copy_tree(x, y, total, files);
    }
    free(x);
    free(y);
    free_names(names, n);
    return r;
}

static int del_tree(const char *p) {
    struct itx_stat st;
    int r = stat(p, &st);
    if (r) return r;
    if (st.type == VT_DIR) {
        int n;
        char **names = list_dir(p, &n, &r);
        char *x = malloc(ITX_PATH_MAX);
        for (int i = 0; i < n && !r; i++) {
            join(x, ITX_PATH_MAX, p, names[i]);
            r = del_tree(x);
            if (r) err(x, r);
        }
        free(x);
        free_names(names, n);
        if (r) return r;
    }
    return remove(p);
}

/* where a copy or move to b lands: into b when b is a directory */
static void target(const char *a, const char *b, char *dst, size_t len) {
    struct itx_stat dt;
    if (stat(b, &dt) == 0 && dt.type == VT_DIR) join(dst, len, b, base_name(a));
    else strlcpy(dst, b, len);
}

static void cmd_copy(const char *a, const char *b, bool rec) {
    struct itx_stat st;
    int r = stat(a, &st);
    if (r) { err(a, r); return; }
    if (st.type == VT_DIR && !rec) { printf("%s is a directory (use: copy -r %s %s)\n", a, a, b); return; }
    if (st.type == VT_DEV) { printf("%s is a device; there is nothing in it to copy\n", a); return; }
    char dst[ITX_PATH_MAX];
    target(a, b, dst, sizeof dst);
    if (st.type == VT_DIR && within(a, dst)) { printf("cannot copy %s into itself\n", a); return; }
    uint64_t total = 0;
    int files = 0;
    if (st.type == VT_DIR) {
        r = copy_tree(a, dst, &total, &files);
        printf("%d file(s), %lu bytes copied to %s%s\n", files, (unsigned long)total, dst, r ? " (stopped by an error)" : "");
    } else {
        r = copy_file(a, dst, &total);
        if (!r) printf("%lu bytes copied to %s\n", (unsigned long)total, dst);
    }
}

static void cmd_move(const char *a, const char *b) {
    char dst[ITX_PATH_MAX];
    target(a, b, dst, sizeof dst);
    int r = rename(a, dst);
    if (r == -EXDEV) {
        /* to another disk (a USB stick, say): copied, then deleted here */
        struct itx_stat st;
        if ((r = stat(a, &st))) { err(a, r); return; }
        if (st.type == VT_DEV) { printf("%s is a device and cannot be moved\n", a); return; }
        if (st.type == VT_DIR && (within(a, dst) || within(a, cwd))) { printf("cannot move %s: it is in use\n", a); return; }
        uint64_t total = 0;
        int files = 0;
        r = copy_tree(a, dst, &total, &files);
        if (r) { printf("\x1b[91mnot moved: the copy failed; %s is unchanged\x1b[0m\n", a); return; }
        r = del_tree(a);
        if (r) { err(a, r); printf("copied to %s, but %s could not be deleted\n", dst, a); return; }
    } else if (r) {
        err(a, r);
        return;
    }
    printf("moved %s to %s\n", a, dst);
}

static void cmd_install(const char *src, const char *as) {
    struct itx_stat st;
    int r = stat(src, &st);
    if (r) { err(src, r); return; }
    if (st.type != VT_FILE) { printf("%s: install takes a file\n", src); return; }
    bool xvr = ends_with(src, ".xvr"), xvc = ends_with(src, ".xvc");
    if (!xvr && !xvc) { printf("install takes .xvr executables and .xvc programs\n"); return; }
    char name[ITX_NAME_MAX + 1];
    strlcpy(name, as ? as : base_name(src), sizeof name - 4);
    if (strchr(name, '/') || !name[0]) { printf("%s: not a name for a program\n", name); return; }
    const char *ext = xvr ? ".xvr" : ".xvc";
    if (!ends_with(name, ext)) strlcat(name, ext, sizeof name);
    if (xvr) {
        char why[96] = "";
        r = xvr_check(src, why, sizeof why);
        if (r) { printf("\x1b[91m%s: not installed: %s\x1b[0m\n", src, why[0] ? why : strerror(r)); return; }
    } else {
        size_t len;
        char *text = read_file(src, &len, 8u << 20);
        if (!text) { printf("\x1b[91m%s: not installed: cannot read it (larger than 8 MiB?)\x1b[0m\n", src); return; }
        bool bin = false;
        for (size_t i = 0; i < len; i++) if (!text[i]) bin = true;
        free(text);
        if (bin) { printf("\x1b[91m%s: not installed: not a text file\x1b[0m\n", src); return; }
    }
    char dst[ITX_PATH_MAX];
    join(dst, sizeof dst, "/dex", name);
    struct itx_stat old;
    bool existed = stat(dst, &old) == 0;
    uint64_t total = 0;
    r = copy_file(src, dst, &total);
    if (r) return;
    char cmd[ITX_NAME_MAX + 1];
    strlcpy(cmd, name, sizeof cmd);
    cmd[strlen(cmd) - 4] = 0;
    printf("installed %s (%lu bytes)%s - run it with: %s\n", dst, (unsigned long)total,
           existed ? ", the old one replaced" : "", cmd);
}

static void cmd_usb(void) {
    struct itx_devinfo d[32];
    int n = devlist(d, 32);
    if (n < 0) { err("usb", n); return; }
    if (!n) { printf("no USB devices\n"); return; }
    printf("  ID  KIND      SPEED   PORT      VID:PID    NAME\n");
    for (int i = 0; i < n; i++) {
        printf("%4u  %-8s  %-6s  %-8s  %04x:%04x  %s\n", d[i].id, kind_name(d[i].kind), speed_name(d[i].speed),
               d[i].port, d[i].vendor, d[i].product, d[i].name[0] ? d[i].name : "-");
        if (d[i].media[0]) printf("%38s/media: %s\n", "", d[i].media);
    }
}

static void cmd_eject(const char *path) {
    char abs[ITX_PATH_MAX];
    absolute(path, abs, sizeof abs);
    if (strncmp(abs, "/media/", 7) || strchr(abs + 7, '/')) {
        printf("eject takes a volume in /media, e.g.  eject /media/STICK\n");
        return;
    }
    if (within(abs, cwd)) { chdir("/"); getcwd(cwd, sizeof cwd); }
    int r = eject(abs);
    if (r == -EBUSY) printf("\x1b[91m%s: files on it are still open\x1b[0m\n", abs);
    else if (r == -ENOTDIR) printf("%s is a device, not a volume\n", abs);
    else if (r) err(abs, r);
    else printf("%s is written out: it can be unplugged\n", abs);
}

static void cmd_info(const char *path) {
    struct itx_stat st;
    int r = stat(path, &st);
    if (r) { err(path, r); return; }
    if (st.type == VT_DEV) {
        struct itx_devinfo d[32];
        int n = devlist(d, 32);
        for (int i = 0; i < n; i++) {
            if (d[i].id != st.ino) continue;
            printf("%s\n  type: USB device (%s)\n  id: %u\n  port: %s, %s speed\n  vendor:product: %04x:%04x\n"
                   "  name: %s\n  in /media: %s\n", path, kind_name(d[i].kind), d[i].id, d[i].port,
                   speed_name(d[i].speed), d[i].vendor, d[i].product, d[i].name[0] ? d[i].name : "-", d[i].media);
            return;
        }
        printf("%s\n  type: USB device\n  id: %lu (no longer plugged in)\n", path, (unsigned long)st.ino);
        return;
    }
    printf("%s\n  type: %s\n  size: %lu bytes\n", path, st.type == VT_DIR ? "directory" : st.type == VT_RAM ? "RAM range" : "file",
           (unsigned long)st.size);
    if (st.type == VT_RAM) printf("  physical: 0x%lx - 0x%lx\n", (unsigned long)st.phys, (unsigned long)(st.phys + st.size - 1));
    if (st.mtime) printf("  modified: %lu (seconds since 1970)\n", (unsigned long)st.mtime);
    printf("  inode: %lu%s\n", (unsigned long)st.ino, st.flags & ST_RDONLY ? "  (read-only)" : "");
}

static void cmd_ps(void) {
    struct itx_procinfo p[80];
    int n = proclist(p, 80);
    static const char *states[] = { "-", "ready", "running", "waiting", "ended" };
    printf("  PID  PARENT  STATE     CPU ms   MEMORY  NAME\n");
    for (int i = 0; i < n; i++) {
        char mem[24] = "-";
        if (!p[i].kernel) snprintf(mem, sizeof mem, "%lu KiB", (unsigned long)p[i].mem_kib);
        printf("%5d  %6d  %-8s %7lu %8s  %s%s\n", p[i].pid, p[i].ppid, p[i].state >= 0 && p[i].state <= 4 ? states[p[i].state] : "?",
               (unsigned long)p[i].cpu_ms, mem, p[i].name, p[i].kernel ? "  [kernel]" : "");
    }
}

static void cmd_mem(void) {
    struct itx_sysinfo si;
    sysinfo(&si);
    printf("%s, %s boot, screen %ux%u\n", si.version, si.firmware == 2 ? "UEFI" : "BIOS", si.fb_width, si.fb_height);
    printf("cpu: %s\n", si.cpu);
    printf("memory: %lu MiB, %lu MiB free, kernel heap %lu KiB\n", (unsigned long)(si.total_kib / 1024),
           (unsigned long)(si.free_kib / 1024), (unsigned long)si.heap_kib);
    printf("processes: %u, up %lu s, root on %s\n", si.nprocs, (unsigned long)(si.uptime_ms / 1000), si.root);
}

static void cmd_log(int lines) {
    char *buf = malloc(65536);
    long n = klog_read(buf, 65536);
    if (n < 0) { free(buf); return; }
    int seen = 0;
    long start = n;
    while (start > 0) {
        if (buf[start - 1] == '\n' && ++seen > lines) break;
        start--;
    }
    flush();
    write(1, buf + start, n - start);
    free(buf);
}

/* ------------------------------------------------------------ programs */

static void reap(bool quiet) {
    for (;;) {
        int status;
        int pid = waitpid(-1, &status, WAIT_NOHANG);
        if (pid <= 0) break;
        for (int i = 0; i < njobs; i++) {
            if (jobs[i] == pid) {
                jobs[i] = jobs[--njobs];
                if (!quiet) printf("[%d] done (exit %d)\n", pid, status);
                break;
            }
        }
    }
}

static void report(int status) {
    if (status == -9) printf("\x1b[93m[stopped]\x1b[0m\n");
    else if (status == -11) printf("\x1b[91m[the program crashed]\x1b[0m\n");
    else if (status) printf("\x1b[93m[exit %d]\x1b[0m\n", status);
}

static void exec_program(const char *path, int argc, char **argv, bool bg) {
    const char *args[MAXARGS + 2];
    int n = 0;
    const char *prog = path;
    if (ends_with(path, ".xvc")) {
        /* XVC programs run in the XVC interpreter */
        prog = "/system/xvc.xvr";
        args[n++] = "xvc";
        args[n++] = path;
        for (int i = 1; i < argc; i++) args[n++] = argv[i];
    } else {
        args[n++] = path;
        for (int i = 1; i < argc; i++) args[n++] = argv[i];
    }
    flush();
    int pid = spawn(prog, args, n, bg ? 0 : SPAWN_FG);
    if (pid < 0) {
        char why[96] = "";
        last_error(why, sizeof why);
        if (pid == -ENOEXEC && why[0]) printf("\x1b[91m%s: not a valid XVR executable: %s\x1b[0m\n", path, why);
        else err(path, pid);
        return;
    }
    if (bg) {
        if (njobs < 32) jobs[njobs++] = pid;
        printf("[%d] running in the background\n", pid);
        return;
    }
    int status = 0;
    int r = waitpid(pid, &status, 0);
    tty_setfg(0);
    if (r < 0) err("wait", r);
    else report(status);
}

/* --------------------------------------------------------------- help */

static void help(void) {
    printf("itelx terminal commands:\n"
           "  ls [path]                list a directory (ls /ram lists the physical RAM)\n"
           "  cd= <path>               change directory, e.g.  cd= /system\n"
           "  del <path>               delete a file or an empty directory\n"
           "  del -r <dir>             delete a directory with everything in it\n"
           "  add -r <dir>             create a directory\n"
           "  add -f <file> [text]     create a file\n"
           "  run <file.xvc> [args]    run an XVC program\n"
           "  run <file.xvr> [args]    run an executable (also: ./app.xvr, /dex/app.xvr)\n"
           "  ... &                    run in the background\n"
           "  cat <file>               show a file\n"
           "  write|append <file>      type text into a file (end with a line \".\")\n"
           "  copy <from> <to>         copy a file (to and from USB sticks too)\n"
           "  copy -r <dir> <to>       copy a directory with everything in it\n"
           "  move <from> <to>         rename or move (also to another disk)\n"
           "  install <app.xvr|.xvc>   put a program into /dex; then run it by its name\n"
           "  usb                      the USB devices; ls /media shows them and the volumes\n"
           "  eject /media/<volume>    write a USB stick out before unplugging it\n"
           "  hex <file> [off] [n]     bytes of a file (e.g. hex /ram/<range> 0x1000 64)\n"
           "  info <path>              details of a file\n"
           "  ps | kill <pid> | jobs   processes\n"
           "  sys                      the system: version, start, disks, memory, owner\n"
           "  sys.info                 processor, memory and graphics use (one second)\n"
           "  sys.info.device          the hardware: processor, memory, board, firmware, graphics\n"
           "  sys.all                  all of sys, sys.info and sys.info.device\n"
           "  sys.top [seconds]        every process and what it uses, live (q quits)\n"
           "  mem | log [n] | mounts   system information\n"
           "  pwd | echo | sleep <ms> | clear | lang | sync | reboot | poweroff | exit\n"
           "Alt+Shift switches the keyboard between English and Arabic (right to left).\n"
           "Ctrl+C stops the program in the foreground; Shift+PgUp/PgDn scroll.\n");
    term_help();
}

/* ------------------------------------------------------------- dispatch */

static void run_line(char *line) {
    char *argv[MAXARGS + 1];
    int argc = split(line, argv);
    if (!argc) return;
    bool bg = false;
    if (!strcmp(argv[argc - 1], "&")) { bg = true; argc--; }
    else if (ends_with(argv[argc - 1], "&") && strlen(argv[argc - 1]) > 1) { argv[argc - 1][strlen(argv[argc - 1]) - 1] = 0; bg = true; }
    if (!argc) return;
    argv[argc] = NULL;
    const char *c = argv[0];

    /* cd= with the path in the same word or the next */
    if (!strncmp(c, "cd=", 3)) {
        const char *p = c[3] ? c + 3 : (argc > 1 ? argv[1] : "/");
        int r = chdir(p);
        if (r) err(p, r);
        else getcwd(cwd, sizeof cwd);
        return;
    }
    if (!strcmp(c, "cd")) { printf("use: cd= <path>   (for example: cd= /system)\n"); return; }
    if (!strcmp(c, "ls")) { cmd_ls(argc > 1 ? argv[1] : "."); return; }
    if (!strcmp(c, "del")) {
        int rec = argc > 1 && !strcmp(argv[1], "-r");
        if (argc < 2 + rec) { printf("use: del <path>   or   del -r <directory>\n"); return; }
        for (int i = 1 + rec; i < argc; i++) {
            if (rec && within(argv[i], cwd)) { printf("%s: in use (the terminal is in it)\n", argv[i]); continue; }
            int r = rec ? del_tree(argv[i]) : remove(argv[i]);
            if (r) err(argv[i], r); else printf("deleted %s\n", argv[i]);
        }
        return;
    }
    if (!strcmp(c, "add")) {
        if (argc < 3 || (strcmp(argv[1], "-r") && strcmp(argv[1], "-f"))) {
            printf("use: add -r <directory>   or   add -f <file> [text]\n");
            return;
        }
        if (!strcmp(argv[1], "-r")) {
            int r = mkdir(argv[2]);
            if (r) err(argv[2], r); else printf("created directory %s\n", argv[2]);
        } else {
            int fd = open(argv[2], O_WRONLY | O_CREATE | O_EXCL);
            if (fd < 0) { err(argv[2], fd); return; }
            if (argc > 3) {
                char text[LINE] = "";
                for (int i = 3; i < argc; i++) { strlcat(text, argv[i], sizeof text); strlcat(text, i + 1 < argc ? " " : "\n", sizeof text); }
                write(fd, text, strlen(text));
            }
            close(fd);
            printf("created file %s\n", argv[2]);
        }
        return;
    }
    if (!strcmp(c, "run")) {
        if (argc < 2) { printf("use: run <program.xvc|program.xvr> [arguments]\n"); return; }
        exec_program(argv[1], argc - 1, argv + 1, bg);
        return;
    }
    if (!strcmp(c, "help") || !strcmp(c, "?")) { help(); return; }
    if (!strcmp(c, "pwd")) { printf("%s\n", cwd); return; }
    if (!strcmp(c, "cat")) { if (argc > 1) cmd_cat(argv[1]); else printf("use: cat <file>\n"); return; }
    if (!strcmp(c, "write") || !strcmp(c, "append")) {
        if (argc > 1) cmd_write(argv[1], c[0] == 'a'); else printf("use: %s <file>\n", c);
        return;
    }
    if (!strcmp(c, "copy")) {
        int rec = argc > 1 && !strcmp(argv[1], "-r");
        if (argc > 2 + rec) cmd_copy(argv[1 + rec], argv[2 + rec], rec);
        else printf("use: copy <from> <to>   or   copy -r <directory> <to>\n");
        return;
    }
    if (!strcmp(c, "move")) { if (argc > 2) cmd_move(argv[1], argv[2]); else printf("use: move <from> <to>\n"); return; }
    if (!strcmp(c, "install")) {
        if (argc > 1) cmd_install(argv[1], argc > 2 ? argv[2] : NULL);
        else printf("use: install <program.xvr|program.xvc> [name]\n");
        return;
    }
    if (!strcmp(c, "usb") || !strcmp(c, "devices")) { cmd_usb(); return; }
    if (!strcmp(c, "eject")) { if (argc > 1) cmd_eject(argv[1]); else printf("use: eject /media/<volume>\n"); return; }
    if (!strcmp(c, "hex")) { if (argc > 1) cmd_hex(argv[1], argc > 2 ? argv[2] : NULL, argc > 3 ? argv[3] : NULL); else printf("use: hex <file> [offset] [count]\n"); return; }
    if (!strcmp(c, "info")) { if (argc > 1) cmd_info(argv[1]); else printf("use: info <path>\n"); return; }
    if (!strcmp(c, "ps")) { cmd_ps(); return; }
    if (!strcmp(c, "kill")) {
        if (argc < 2) { printf("use: kill <pid>\n"); return; }
        int r = kill(atoi(argv[1]));
        if (r) err(argv[1], r); else printf("stopping %s\n", argv[1]);
        return;
    }
    if (!strcmp(c, "jobs")) {
        reap(false);
        if (!njobs) printf("no background programs\n");
        for (int i = 0; i < njobs; i++) printf("[%d] running\n", jobs[i]);
        return;
    }
    if (!strcmp(c, "sys")) { sys_overview(); return; }
    if (!strcmp(c, "sys.info")) { sys_usage(); return; }
    if (!strcmp(c, "sys.info.device")) { sys_device(); return; }
    if (!strcmp(c, "sys.all")) { sys_all(); return; }
    if (!strcmp(c, "sys.top")) {
        /* a program of its own, so that it can be stopped like one */
        exec_program("/system/top.xvr", argc, argv, bg);
        return;
    }
    if (!strncmp(c, "sys.", 4)) { printf("unknown command: %s (sys, sys.info, sys.info.device, sys.all, sys.top)\n", c); return; }
    if (!strcmp(c, "mem")) { cmd_mem(); return; }
    if (!strcmp(c, "log")) { cmd_log(argc > 1 ? atoi(argv[1]) : 20); return; }
    if (!strcmp(c, "mounts")) {
        char buf[1024];
        long n = mounts(buf, sizeof buf - 1);
        if (n > 0) { buf[n] = 0; printf("%s", buf); }
        return;
    }
    if (!strcmp(c, "clear")) { printf("\x1b[2J"); return; }
    if (!strcmp(c, "echo")) {
        for (int i = 1; i < argc; i++) printf("%s%s", argv[i], i + 1 < argc ? " " : "");
        putchar('\n');
        return;
    }
    if (!strcmp(c, "sleep")) { sleep_ms(argc > 1 ? atol(argv[1]) : 1000); return; }
    if (!strcmp(c, "lang")) {
        struct itx_ttyinfo ti;
        tty_info(&ti);
        printf("keyboard: %s, input direction %s (Alt+Shift switches)\n", ti.lang ? "Arabic" : "English",
               ti.dir == TTY_DIR_RTL ? "right to left" : "left to right");
        return;
    }
    if (!strcmp(c, "sync")) { int r = sync(); if (r) err("sync", r); return; }
    if (!strcmp(c, "reboot")) { reboot(); return; }
    if (!strcmp(c, "poweroff")) { poweroff(); return; }
    if (!strcmp(c, "exit")) { flush(); exit(0); }

    /* a path to a program: ./app.xvr, /dex/app.xvr, app.xvr, or an .xvc file */
    /* the commands of this terminal's customizer */
    if (term_command(argc, argv)) return;

    if (strchr(c, '/') || ends_with(c, ".xvr") || ends_with(c, ".xvc")) {
        struct itx_stat st;
        int r = stat(c, &st);
        if (r) { err(c, r); return; }
        if (st.type == VT_DIR) { printf("%s is a directory (use: cd= %s)\n", c, c); return; }
        exec_program(c, argc, argv, bg);
        return;
    }
    /* a bare name: a program in /dex (an executable, else an XVC program) or /system */
    char path[ITX_PATH_MAX];
    const char *where[] = { "/dex/%s.xvr", "/dex/%s.xvc", "/system/%s.xvr" };
    for (int i = 0; i < 3; i++) {
        struct itx_stat st;
        snprintf(path, sizeof path, where[i], c);
        if (stat(path, &st) == 0 && st.type == VT_FILE) { exec_program(path, argc, argv, bg); return; }
    }
    printf("unknown command: %s (type help)\n", c);
}

/* --------------------------------------------- for the customizers' code */

const char *term_cwd(void) { return cwd; }
void term_err(const char *what, int code) { err(what, code); }
void term_absolute(const char *path, char *out, size_t len) { absolute(path, out, len); }
bool term_within(const char *a, const char *b) { return within(a, b); }
void term_human(uint64_t n, char *out, size_t len) { human(n, out, len); }
int  term_copy_tree(const char *a, const char *b, uint64_t *total, int *files) { return copy_tree(a, b, total, files); }
int  term_del_tree(const char *p) { return del_tree(p); }
void term_exec(const char *path, int argc, char **argv) { exec_program(path, argc, argv, false); }

int main(int argc, char **argv) {
    term_start();
    getcwd(cwd, sizeof cwd);
    term_banner();
    char line[LINE];
    for (;;) {
        reap(false);
        term_prompt(cwd);
        flush();
        ssize_t n = read(0, line, sizeof line - 1);
        if (n < 0) continue;
        if (n == 0) { printf("\n"); continue; }       /* Ctrl+D on an empty line */
        line[n] = 0;
        run_line(line);
        flush();
    }
}
