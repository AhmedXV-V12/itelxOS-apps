/* SQLite on itelx: its operating system layer (SQLITE_OS_OTHER).
 *
 * Files are itelx files (open, read and write at an offset, truncate,
 * sync). Locking: several programs may use one database, so the writer
 * takes a lock file, DB-lock, holding its process number; a lock left by
 * a program that ended is taken over. Readers do not lock (a reader may
 * see a database while a writer changes it: SQLite's rollback journal is
 * written first, and readers check the change counter).
 *
 * The memory comes from one block (MEMSYS5) set up by itx_sqlite_init. */
#include <kit.h>
#include "sqlite3.h"
#include "itxsqlite.h"

struct ifile {
    sqlite3_file base;
    int fd;
    int lock;
    bool delete_on_close;
    char path[ITX_PATH_MAX];
};

static bool pid_alive(int pid) {
    struct itx_procinfo p[96];
    int n = proclist(p, 96);
    for (int i = 0; i < n; i++) if (p[i].pid == pid && p[i].state != PS_ZOMBIE) return true;
    return false;
}

static int lock_path(struct ifile *f, char *out, size_t len) { return snprintf(out, len, "%s-lock", f->path); }

static bool take_lock(struct ifile *f) {
    char lp[ITX_PATH_MAX + 8], me[16];
    lock_path(f, lp, sizeof lp);
    snprintf(me, sizeof me, "%d\n", getpid());
    for (int tries = 0; tries < 2; tries++) {
        int fd = open(lp, O_WRONLY | O_CREATE | O_EXCL);
        if (fd >= 0) { write(fd, me, strlen(me)); close(fd); return true; }
        size_t l;
        char *t = read_file(lp, &l, 64);
        int pid = t ? atoi(t) : 0;
        free(t);
        if (pid == getpid()) return true;
        if (pid && pid_alive(pid)) return false;
        remove(lp);                                  /* left by a program that ended */
    }
    return false;
}

static void drop_lock(struct ifile *f) {
    char lp[ITX_PATH_MAX + 8];
    lock_path(f, lp, sizeof lp);
    size_t l;
    char *t = read_file(lp, &l, 64);
    if (t && atoi(t) == getpid()) remove(lp);
    free(t);
}

/* ------------------------------------------------------------ the file */

static int x_close(sqlite3_file *p) {
    struct ifile *f = (struct ifile *)p;
    if (f->lock > SQLITE_LOCK_SHARED) drop_lock(f);
    if (f->fd >= 0) close(f->fd);
    if (f->delete_on_close) remove(f->path);
    f->fd = -1;
    return SQLITE_OK;
}

static int x_read(sqlite3_file *p, void *buf, int amt, sqlite3_int64 off) {
    struct ifile *f = (struct ifile *)p;
    if (lseek(f->fd, (long)off, SEEK_SET) < 0) return SQLITE_IOERR_READ;
    int got = 0;
    while (got < amt) {
        ssize_t n = read(f->fd, (char *)buf + got, (size_t)(amt - got));
        if (n < 0) return SQLITE_IOERR_READ;
        if (n == 0) break;
        got += (int)n;
    }
    if (got < amt) {
        memset((char *)buf + got, 0, (size_t)(amt - got));
        return SQLITE_IOERR_SHORT_READ;
    }
    return SQLITE_OK;
}

static int x_write(sqlite3_file *p, const void *buf, int amt, sqlite3_int64 off) {
    struct ifile *f = (struct ifile *)p;
    struct itx_stat st;
    if (fstat(f->fd, &st)) return SQLITE_IOERR_WRITE;
    if ((uint64_t)off > st.size) {
        /* a gap: written as zeros (no sparse files here) */
        static const char zero[4096];
        if (lseek(f->fd, (long)st.size, SEEK_SET) < 0) return SQLITE_IOERR_WRITE;
        for (uint64_t left = (uint64_t)off - st.size; left;) {
            size_t k = left < sizeof zero ? (size_t)left : sizeof zero;
            if (write(f->fd, zero, k) != (ssize_t)k) return SQLITE_FULL;
            left -= k;
        }
    }
    if (lseek(f->fd, (long)off, SEEK_SET) < 0) return SQLITE_IOERR_WRITE;
    int done = 0;
    while (done < amt) {
        ssize_t n = write(f->fd, (const char *)buf + done, (size_t)(amt - done));
        if (n <= 0) return n == -ENOSPC || n == 0 ? SQLITE_FULL : SQLITE_IOERR_WRITE;
        done += (int)n;
    }
    return SQLITE_OK;
}

static int x_truncate(sqlite3_file *p, sqlite3_int64 size) {
    struct ifile *f = (struct ifile *)p;
    return truncate(f->path, (long)size) ? SQLITE_IOERR_TRUNCATE : SQLITE_OK;
}

static int x_sync(sqlite3_file *p, int flags) { return sync() ? SQLITE_IOERR_FSYNC : SQLITE_OK; }

static int x_size(sqlite3_file *p, sqlite3_int64 *size) {
    struct ifile *f = (struct ifile *)p;
    struct itx_stat st;
    if (fstat(f->fd, &st)) return SQLITE_IOERR_FSTAT;
    *size = (sqlite3_int64)st.size;
    return SQLITE_OK;
}

static int x_lock(sqlite3_file *p, int level) {
    struct ifile *f = (struct ifile *)p;
    if (level > SQLITE_LOCK_SHARED && f->lock <= SQLITE_LOCK_SHARED && !take_lock(f)) return SQLITE_BUSY;
    if (level > f->lock) f->lock = level;
    return SQLITE_OK;
}

static int x_unlock(sqlite3_file *p, int level) {
    struct ifile *f = (struct ifile *)p;
    if (level <= SQLITE_LOCK_SHARED && f->lock > SQLITE_LOCK_SHARED) drop_lock(f);
    f->lock = level;
    return SQLITE_OK;
}

static int x_check_reserved(sqlite3_file *p, int *out) {
    struct ifile *f = (struct ifile *)p;
    char lp[ITX_PATH_MAX + 8];
    lock_path(f, lp, sizeof lp);
    struct itx_stat st;
    *out = 0;
    if (f->lock > SQLITE_LOCK_SHARED) { *out = 1; return SQLITE_OK; }
    if (stat(lp, &st) == 0) {
        size_t l;
        char *t = read_file(lp, &l, 64);
        int pid = t ? atoi(t) : 0;
        free(t);
        *out = pid && pid_alive(pid);
    }
    return SQLITE_OK;
}

static int x_control(sqlite3_file *p, int op, void *arg) { return SQLITE_NOTFOUND; }
static int x_sector(sqlite3_file *p) { return 512; }
static int x_caps(sqlite3_file *p) { return 0; }

static const sqlite3_io_methods io = {
    1, x_close, x_read, x_write, x_truncate, x_sync, x_size, x_lock, x_unlock, x_check_reserved, x_control, x_sector, x_caps,
    NULL, NULL, NULL, NULL, NULL, NULL,
};

/* --------------------------------------------------------------- the VFS */

static void full_path(const char *name, char *out, size_t len) {
    char tmp[ITX_PATH_MAX * 2];
    if (name[0] == '/') strlcpy(tmp, name, sizeof tmp);
    else {
        char cwd[ITX_PATH_MAX];
        getcwd(cwd, sizeof cwd);
        snprintf(tmp, sizeof tmp, "%s/%s", strcmp(cwd, "/") ? cwd : "", name);
    }
    /* . and .. worked out */
    size_t o = 0;
    out[o++] = '/';
    for (char *p = tmp; *p;) {
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

static int v_open(sqlite3_vfs *vfs, sqlite3_filename name, sqlite3_file *p, int flags, int *out_flags) {
    struct ifile *f = (struct ifile *)p;
    memset(f, 0, sizeof *f);
    f->fd = -1;
    if (!name) {
        /* a temporary file (with SQLITE_TEMP_STORE=3 they are in memory; this is the rest) */
        static int n;
        snprintf(f->path, sizeof f->path, "/data/.tmp-%d-%d", getpid(), ++n);
        k_mkdirs("/data");
        flags |= SQLITE_OPEN_CREATE | SQLITE_OPEN_READWRITE | SQLITE_OPEN_DELETEONCLOSE;
    } else {
        full_path(name, f->path, sizeof f->path);
    }
    int of = (flags & SQLITE_OPEN_READWRITE) ? O_RDWR : O_RDONLY;
    struct itx_stat st;
    bool exists = stat(f->path, &st) == 0;
    if ((flags & SQLITE_OPEN_EXCLUSIVE) && exists) return SQLITE_CANTOPEN;
    if (!exists && (flags & SQLITE_OPEN_CREATE)) of |= O_CREATE;
    int fd = open(f->path, of);
    if (fd < 0 && (flags & SQLITE_OPEN_READWRITE) && exists) {
        fd = open(f->path, O_RDONLY);               /* a read-only disk */
        if (fd >= 0 && out_flags) flags = (flags & ~SQLITE_OPEN_READWRITE) | SQLITE_OPEN_READONLY;
    }
    if (fd < 0) return SQLITE_CANTOPEN;
    f->fd = fd;
    f->delete_on_close = (flags & SQLITE_OPEN_DELETEONCLOSE) != 0;
    f->base.pMethods = &io;
    if (out_flags) *out_flags = flags;
    return SQLITE_OK;
}

static int v_delete(sqlite3_vfs *vfs, const char *name, int sync_dir) {
    char p[ITX_PATH_MAX];
    full_path(name, p, sizeof p);
    int r = remove(p);
    if (r && r != -ENOENT) return SQLITE_IOERR_DELETE;
    if (sync_dir) sync();
    return SQLITE_OK;
}

static int v_access(sqlite3_vfs *vfs, const char *name, int flags, int *out) {
    char p[ITX_PATH_MAX];
    full_path(name, p, sizeof p);
    struct itx_stat st;
    *out = stat(p, &st) == 0 && !(flags == SQLITE_ACCESS_READWRITE && (st.flags & ST_RDONLY));
    return SQLITE_OK;
}

static int v_full(sqlite3_vfs *vfs, const char *name, int n, char *out) {
    full_path(name, out, (size_t)n);
    return SQLITE_OK;
}

static int v_random(sqlite3_vfs *vfs, int n, char *out) { k_random(out, (size_t)n); return n; }
static int v_sleep(sqlite3_vfs *vfs, int us) { sleep_ms(us / 1000 ? us / 1000 : 1); return us; }

static int v_time64(sqlite3_vfs *vfs, sqlite3_int64 *t) {
    /* Julian day number in milliseconds: the Unix epoch is day 2440587.5 */
    *t = (sqlite3_int64)210866760000000LL + (sqlite3_int64)unix_time() * 1000;
    return SQLITE_OK;
}

static int v_time(sqlite3_vfs *vfs, double *t) {
    sqlite3_int64 ms;
    v_time64(vfs, &ms);
    *t = (double)ms / 86400000.0;
    return SQLITE_OK;
}

static int v_error(sqlite3_vfs *vfs, int n, char *out) { if (n) out[0] = 0; return 0; }

static sqlite3_vfs itx_vfs = {
    2, sizeof(struct ifile), ITX_PATH_MAX, NULL, "itelx", NULL, v_open, v_delete, v_access, v_full,
    NULL, NULL, NULL, NULL, v_random, v_sleep, v_time, v_error, v_time64, NULL, NULL, NULL,
};

int sqlite3_os_init(void) { return sqlite3_vfs_register(&itx_vfs, 1); }
int sqlite3_os_end(void) { return SQLITE_OK; }

/* time() for SQLite's date functions */
long time(long *t) {
    long now = unix_time();
    if (t) *t = now;
    return now;
}

int itx_sqlite_init(int heap_mib) {
    static bool done;
    if (done) return SQLITE_OK;
    size_t size = (size_t)heap_mib << 20;
    void *heap = malloc(size);
    if (!heap) return SQLITE_NOMEM;
    int r = sqlite3_config(SQLITE_CONFIG_HEAP, heap, (int)size, 64);
    if (r) return r;
    r = sqlite3_initialize();
    done = r == SQLITE_OK;
    return r;
}
