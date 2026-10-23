/* kv: the key-value database of data_place (kv.h). */
#include "kv.h"

/* --------------------------------------------------------------- buffers */

void kb_put(struct kbuf *b, const void *data, size_t len) {
    if (b->n + len + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 256;
        while (cap < b->n + len + 1) cap *= 2;
        char *p = realloc(b->p, cap);
        if (!p) { dprintf(2, "kv: out of memory\n"); exit(1); }
        b->p = p;
        b->cap = cap;
    }
    memcpy(b->p + b->n, data, len);
    b->n += len;
    b->p[b->n] = 0;
}

void kb_str(struct kbuf *b, const char *s) { kb_put(b, s, strlen(s)); }

void kb_printf(struct kbuf *b, const char *fmt, ...) {
    char t[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(t, sizeof t, fmt, ap);
    va_end(ap);
    kb_put(b, t, (size_t)(n < (int)sizeof t ? n : (int)sizeof t - 1));
}

void kb_free(struct kbuf *b) { free(b->p); b->p = NULL; b->n = b->cap = 0; }

/* ---------------------------------------------------------------- data */

enum { KT_STR = 1, KT_HASH, KT_LIST, KT_SET };
static const char *const type_names[] = { "none", "string", "hash", "list", "set" };

struct bstr { char *p; size_t n; };

struct kentry {
    struct bstr key;
    uint64_t h;
    int type;
    long expire;                  /* Unix time in ms; 0: never */
    struct bstr str;
    struct bstr *it;              /* list, set: items; hash: field, value, field, value ... */
    int nit, cap;
};

#define TOMB ((struct kentry *)1)

struct kvdb {
    struct kentry **slot;
    size_t nslot, count, used;    /* used: live entries and tombstones */
    int fd;
    bool replay;
    char path[ITX_PATH_MAX];
    uint64_t aof_bytes;
    long last_sync, base_ms;
    bool dirty;
};

static long now_ms(struct kvdb *db) { return db->base_ms + uptime_ms(); }

static uint64_t hash(const char *p, size_t n) {
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; i++) { h ^= (unsigned char)p[i]; h *= 1099511628211ULL; }
    return h;
}

static struct bstr bdup(const char *p, size_t n) {
    struct bstr s = { malloc(n + 1), n };
    memcpy(s.p, p, n);
    s.p[n] = 0;
    return s;
}

static bool beq(const struct bstr *a, const char *p, size_t n) { return a->n == n && !memcmp(a->p, p, n); }

static void entry_free(struct kentry *e) {
    free(e->key.p);
    free(e->str.p);
    for (int i = 0; i < e->nit; i++) free(e->it[i].p);
    free(e->it);
    free(e);
}

static void rehash(struct kvdb *db, size_t size) {
    struct kentry **old = db->slot;
    size_t on = db->nslot;
    db->slot = calloc(size, sizeof *db->slot);
    db->nslot = size;
    db->used = db->count;
    for (size_t i = 0; i < on; i++) {
        struct kentry *e = old[i];
        if (!e || e == TOMB) continue;
        size_t k = e->h & (size - 1);
        while (db->slot[k]) k = (k + 1) & (size - 1);
        db->slot[k] = e;
    }
    free(old);
}

static long slot_of(struct kvdb *db, const char *p, size_t n, uint64_t h) {
    size_t k = h & (db->nslot - 1);
    for (size_t i = 0; i < db->nslot; i++, k = (k + 1) & (db->nslot - 1)) {
        struct kentry *e = db->slot[k];
        if (!e) return -1;
        if (e != TOMB && e->h == h && beq(&e->key, p, n)) return (long)k;
    }
    return -1;
}

static void del_slot(struct kvdb *db, long k) {
    entry_free(db->slot[k]);
    db->slot[k] = TOMB;
    db->count--;
}

/* the entry of a key that is alive (expired ones go here) */
static struct kentry *find(struct kvdb *db, const char *p, size_t n) {
    long k = slot_of(db, p, n, hash(p, n));
    if (k < 0) return NULL;
    struct kentry *e = db->slot[k];
    if (e->expire && e->expire <= now_ms(db)) { del_slot(db, k); return NULL; }
    return e;
}

static struct kentry *create(struct kvdb *db, const char *p, size_t n, int type) {
    if ((db->used + 1) * 10 >= db->nslot * 7) rehash(db, db->count * 2 < 64 ? 64 : db->nslot * 2);
    struct kentry *e = calloc(1, sizeof *e);
    e->key = bdup(p, n);
    e->h = hash(p, n);
    e->type = type;
    size_t k = e->h & (db->nslot - 1);
    while (db->slot[k] && db->slot[k] != TOMB) k = (k + 1) & (db->nslot - 1);
    if (!db->slot[k]) db->used++;
    db->slot[k] = e;
    db->count++;
    return e;
}

static bool del_key(struct kvdb *db, const char *p, size_t n) {
    long k = slot_of(db, p, n, hash(p, n));
    if (k < 0) return false;
    bool alive = !(db->slot[k]->expire && db->slot[k]->expire <= now_ms(db));
    del_slot(db, k);
    return alive;
}

static void push_item(struct kentry *e, const char *p, size_t n, bool front) {
    if (e->nit == e->cap) {
        e->cap = e->cap ? e->cap * 2 : 8;
        e->it = realloc(e->it, sizeof *e->it * (size_t)e->cap);
    }
    if (front) {
        memmove(&e->it[1], &e->it[0], sizeof *e->it * (size_t)e->nit);
        e->it[0] = bdup(p, n);
    } else {
        e->it[e->nit] = bdup(p, n);
    }
    e->nit++;
}

static int item_index(struct kentry *e, const char *p, size_t n, int step) {
    for (int i = 0; i < e->nit; i += step) if (beq(&e->it[i], p, n)) return i;
    return -1;
}

static void remove_items(struct kentry *e, int i, int count) {
    for (int k = 0; k < count; k++) free(e->it[i + k].p);
    memmove(&e->it[i], &e->it[i + count], sizeof *e->it * (size_t)(e->nit - i - count));
    e->nit -= count;
}

/* ------------------------------------------------------------ answers */

static void r_ok(struct kbuf *o) { kb_str(o, "+OK\r\n"); }
static void r_status(struct kbuf *o, const char *s) { kb_printf(o, "+%s\r\n", s); }
static void r_err(struct kbuf *o, const char *s) { kb_printf(o, "-ERR %s\r\n", s); }
static void r_int(struct kbuf *o, long v) { kb_printf(o, ":%ld\r\n", v); }
static void r_nil(struct kbuf *o) { kb_str(o, "$-1\r\n"); }
static void r_arr(struct kbuf *o, long n) { kb_printf(o, "*%ld\r\n", n); }
static void r_bulk(struct kbuf *o, const char *p, size_t n) { kb_printf(o, "$%lu\r\n", (unsigned long)n); kb_put(o, p, n); kb_str(o, "\r\n"); }
static void r_cstr(struct kbuf *o, const char *s) { r_bulk(o, s, strlen(s)); }
static void r_wrong(struct kbuf *o) { kb_str(o, "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n"); }

static bool num(const char *p, size_t n, long *v) {
    if (!n || n > 20) return false;
    char t[24];
    memcpy(t, p, n);
    t[n] = 0;
    char *e;
    *v = strtol(t, &e, 10);
    return !*e && (isdigit((unsigned char)t[0]) || (t[0] == '-' && n > 1));
}

/* ----------------------------------------------------------------- AOF */

static void aof_cmd(struct kbuf *b, int argc, char **argv, size_t *lens) {
    r_arr(b, argc);
    for (int i = 0; i < argc; i++) r_bulk(b, argv[i], lens[i]);
}

static void aof_write(struct kvdb *db, const struct kbuf *b) {
    if (db->replay || db->fd < 0) return;
    if (write(db->fd, b->p, b->n) != (ssize_t)b->n) dprintf(2, "kv: %s could not be written (disk full?)\n", db->path);
    db->aof_bytes += b->n;
    db->dirty = true;
    long now = uptime_ms();
    if (now - db->last_sync > 1000) { sync(); db->last_sync = now; db->dirty = false; }   /* at most once a second */
}

/* the command as it is logged: relative times become absolute ones */
static void log_cmd(struct kvdb *db, int argc, char **argv, size_t *lens) {
    if (db->replay) return;
    struct kbuf b = { 0 };
    aof_cmd(&b, argc, argv, lens);
    aof_write(db, &b);
    kb_free(&b);
}

static void log_expire(struct kvdb *db, struct kentry *e) {
    if (db->replay) return;
    char at[24];
    snprintf(at, sizeof at, "%ld", e->expire);
    char *a[] = { "PEXPIREAT", e->key.p, at };
    size_t l[] = { 9, e->key.n, strlen(at) };
    log_cmd(db, 3, a, l);
}

/* the whole database as commands, into path (kv_save) */
static int write_state(struct kvdb *db, const char *path) {
    int fd = open(path, O_WRONLY | O_CREATE | O_TRUNC);
    if (fd < 0) return fd;
    struct kbuf b = { 0 };
    long now = now_ms(db);
    for (size_t i = 0; i < db->nslot; i++) {
        struct kentry *e = db->slot[i];
        if (!e || e == TOMB || (e->expire && e->expire <= now)) continue;
        if (e->type == KT_STR) {
            char *a[] = { "SET", e->key.p, e->str.p };
            size_t l[] = { 3, e->key.n, e->str.n };
            aof_cmd(&b, 3, a, l);
        } else if (e->nit) {
            int n = e->nit + 2;
            char **a = malloc(sizeof(char *) * (size_t)n);
            size_t *l = malloc(sizeof(size_t) * (size_t)n);
            a[0] = e->type == KT_HASH ? "HSET" : e->type == KT_LIST ? "RPUSH" : "SADD";
            l[0] = strlen(a[0]);
            a[1] = e->key.p;
            l[1] = e->key.n;
            for (int k = 0; k < e->nit; k++) { a[k + 2] = e->it[k].p; l[k + 2] = e->it[k].n; }
            aof_cmd(&b, n, a, l);
            free(a);
            free(l);
        }
        if (e->expire) {
            char at[24];
            snprintf(at, sizeof at, "%ld", e->expire);
            char *a[] = { "PEXPIREAT", e->key.p, at };
            size_t l[] = { 9, e->key.n, strlen(at) };
            aof_cmd(&b, 3, a, l);
        }
        if (b.n > (1 << 20)) {
            if (write(fd, b.p, b.n) != (ssize_t)b.n) { close(fd); kb_free(&b); return -ENOSPC; }
            b.n = 0;
        }
    }
    int r = 0;
    if (b.n && write(fd, b.p, b.n) != (ssize_t)b.n) r = -ENOSPC;
    kb_free(&b);
    close(fd);
    return r;
}

int kv_save(struct kvdb *db) {
    char tmp[ITX_PATH_MAX + 8];
    snprintf(tmp, sizeof tmp, "%s.new", db->path);
    int r = write_state(db, tmp);
    if (r) { remove(tmp); return r; }
    sync();
    if (db->fd >= 0) close(db->fd);
    remove(db->path);
    r = rename(tmp, db->path);
    db->fd = open(db->path, O_WRONLY | O_APPEND);
    struct itx_stat st;
    db->aof_bytes = stat(db->path, &st) == 0 ? st.size : 0;
    sync();
    return r;
}

void kv_expire_some(struct kvdb *db) {
    long now = now_ms(db);
    for (size_t i = 0; i < db->nslot; i++) {
        struct kentry *e = db->slot[i];
        if (e && e != TOMB && e->expire && e->expire <= now) del_slot(db, (long)i);
    }
    if (db->dirty && uptime_ms() - db->last_sync > 1000) { sync(); db->last_sync = uptime_ms(); db->dirty = false; }
}

long kv_size(struct kvdb *db) { return (long)db->count; }

/* ------------------------------------------------------- glob (KEYS) */

static bool glob(const char *p, size_t pn, const char *s, size_t sn) {
    size_t pi = 0, si = 0, star_p = (size_t)-1, star_s = 0;
    while (si < sn) {
        if (pi < pn && p[pi] == '*') { star_p = pi++; star_s = si; continue; }
        if (pi < pn && (p[pi] == '?' || (p[pi] == '\\' && pi + 1 < pn && p[pi + 1] == s[si] && ++pi) || p[pi] == s[si])) {
            pi++;
            si++;
            continue;
        }
        if (pi < pn && p[pi] == '[') {
            size_t k = pi + 1;
            bool neg = k < pn && p[k] == '^', hit = false;
            if (neg) k++;
            for (; k < pn && p[k] != ']'; k++) {
                if (k + 2 < pn && p[k + 1] == '-' && p[k + 2] != ']') {
                    if (s[si] >= p[k] && s[si] <= p[k + 2]) hit = true;
                    k += 2;
                } else if (p[k] == s[si]) hit = true;
            }
            if (hit != neg && k < pn) { pi = k + 1; si++; continue; }
        }
        if (star_p != (size_t)-1) { pi = star_p + 1; si = ++star_s; continue; }
        return false;
    }
    while (pi < pn && p[pi] == '*') pi++;
    return pi == pn;
}

/* -------------------------------------------------------- the commands */

#define ARG(i) argv[i], lens[i]
#define IS(name) (!strcasecmp(cmd, name))

static bool cmdname(char *out, size_t len, const char *p, size_t n) {
    if (n >= len) return false;
    memcpy(out, p, n);
    out[n] = 0;
    return true;
}

static void reply_items(struct kbuf *o, struct kentry *e, int from, int to, int step) {
    long n = 0;
    for (int i = from; i <= to && i < e->nit; i += step) n++;
    r_arr(o, n);
    for (int i = from; i <= to && i < e->nit; i += step) r_bulk(o, e->it[i].p, e->it[i].n);
}

static struct kentry *typed(struct kvdb *db, char **argv, size_t *lens, int type, bool make, struct kbuf *o, bool *wrong) {
    struct kentry *e = find(db, ARG(1));
    *wrong = false;
    if (e && e->type != type) { r_wrong(o); *wrong = true; return NULL; }
    if (!e && make) e = create(db, ARG(1), type);
    return e;
}

static const char HELP[] =
    "strings  SET k v [EX s|PX ms|PXAT ms] [NX|XX]  GET  GETSET  MSET  MGET  APPEND  STRLEN  INCR  DECR  INCRBY  DECRBY\n"
    "keys     DEL  EXISTS  TYPE  KEYS pattern  SCAN cursor [MATCH p] [COUNT n]  RENAME  EXPIRE  PEXPIRE  TTL  PTTL  PERSIST\n"
    "hashes   HSET k f v ...  HGET  HMGET  HDEL  HGETALL  HKEYS  HVALS  HLEN  HEXISTS  HINCRBY\n"
    "lists    LPUSH  RPUSH  LPOP  RPOP  LRANGE k start stop  LLEN  LINDEX  LSET  LREM k count v\n"
    "sets     SADD  SREM  SMEMBERS  SISMEMBER  SCARD\n"
    "server   PING  ECHO  DBSIZE  FLUSHDB  INFO  SAVE  HELP\n";

void kv_exec(struct kvdb *db, int argc, char **argv, size_t *lens, struct kbuf *o) {
    char cmd[32];
    if (!argc) return;
    if (!cmdname(cmd, sizeof cmd, ARG(0))) { r_err(o, "unknown command"); return; }
    bool wrong;
    long v;
#define NEED(n) do { if (argc < (n)) { kb_printf(o, "-ERR wrong number of arguments for '%s'\r\n", cmd); return; } } while (0)

    /* ------------------------------------------------ server */
    if (IS("PING")) { if (argc > 1) r_bulk(o, ARG(1)); else r_status(o, "PONG"); return; }
    if (IS("ECHO")) { NEED(2); r_bulk(o, ARG(1)); return; }
    if (IS("DBSIZE")) { r_int(o, (long)db->count); return; }
    if (IS("SELECT")) { r_ok(o); return; }
    if (IS("COMMAND")) { r_arr(o, 0); return; }
    if (IS("HELP")) { r_cstr(o, HELP); return; }
    if (IS("SAVE") || IS("BGSAVE") || IS("BGREWRITEAOF")) {
        int r = kv_save(db);
        if (r) r_err(o, strerror(r)); else r_ok(o);
        return;
    }
    if (IS("INFO")) {
        char t[512];
        snprintf(t, sizeof t, "# Server\r\nkv_version:1.0 (data_place, itelx)\r\nfile:%s\r\nfile_bytes:%lu\r\n# Keyspace\r\ndb0:keys=%lu\r\n",
                 db->path, (unsigned long)db->aof_bytes, (unsigned long)db->count);
        r_cstr(o, t);
        return;
    }
    if (IS("FLUSHDB") || IS("FLUSHALL")) {
        for (size_t i = 0; i < db->nslot; i++) if (db->slot[i] && db->slot[i] != TOMB) entry_free(db->slot[i]);
        memset(db->slot, 0, sizeof *db->slot * db->nslot);
        db->count = db->used = 0;
        log_cmd(db, argc, argv, lens);
        r_ok(o);
        return;
    }

    /* ------------------------------------------------- keys */
    if (IS("DEL") || IS("UNLINK")) {
        NEED(2);
        long n = 0;
        for (int i = 1; i < argc; i++) if (del_key(db, ARG(i))) n++;
        if (n) log_cmd(db, argc, argv, lens);
        r_int(o, n);
        return;
    }
    if (IS("EXISTS")) {
        NEED(2);
        long n = 0;
        for (int i = 1; i < argc; i++) if (find(db, ARG(i))) n++;
        r_int(o, n);
        return;
    }
    if (IS("TYPE")) { NEED(2); struct kentry *e = find(db, ARG(1)); r_status(o, type_names[e ? e->type : 0]); return; }
    if (IS("KEYS")) {
        NEED(2);
        long n = 0;
        struct kbuf items = { 0 };
        long now = now_ms(db);
        for (size_t i = 0; i < db->nslot; i++) {
            struct kentry *e = db->slot[i];
            if (!e || e == TOMB || (e->expire && e->expire <= now)) continue;
            if (glob(argv[1], lens[1], e->key.p, e->key.n)) { r_bulk(&items, e->key.p, e->key.n); n++; }
        }
        r_arr(o, n);
        if (items.n) kb_put(o, items.p, items.n);
        kb_free(&items);
        return;
    }
    if (IS("SCAN")) {
        NEED(2);
        long cursor = atol(argv[1]), count = 10;
        const char *pat = "*";
        size_t pl = 1;
        for (int i = 2; i + 1 < argc; i += 2) {
            if (!strcasecmp(argv[i], "MATCH")) { pat = argv[i + 1]; pl = lens[i + 1]; }
            else if (!strcasecmp(argv[i], "COUNT")) count = atol(argv[i + 1]);
        }
        if (count < 1) count = 10;
        struct kbuf items = { 0 };
        long n = 0, now = now_ms(db);
        size_t i = cursor < 0 ? 0 : (size_t)cursor;
        for (long seen = 0; i < db->nslot && seen < count; i++) {
            struct kentry *e = db->slot[i];
            if (!e || e == TOMB || (e->expire && e->expire <= now)) continue;
            seen++;
            if (glob(pat, pl, e->key.p, e->key.n)) { r_bulk(&items, e->key.p, e->key.n); n++; }
        }
        r_arr(o, 2);
        char c[24];
        snprintf(c, sizeof c, "%lu", (unsigned long)(i >= db->nslot ? 0 : i));
        r_cstr(o, c);
        r_arr(o, n);
        if (items.n) kb_put(o, items.p, items.n);
        kb_free(&items);
        return;
    }
    if (IS("RENAME")) {
        NEED(3);
        struct kentry *e = find(db, ARG(1));
        if (!e) { r_err(o, "no such key"); return; }
        del_key(db, ARG(2));
        long k = slot_of(db, ARG(1), hash(ARG(1)));
        db->slot[k] = TOMB;
        db->count--;
        free(e->key.p);
        e->key = bdup(ARG(2));
        e->h = hash(ARG(2));
        if ((db->used + 1) * 10 >= db->nslot * 7) rehash(db, db->nslot * 2);
        size_t s = e->h & (db->nslot - 1);
        while (db->slot[s] && db->slot[s] != TOMB) s = (s + 1) & (db->nslot - 1);
        if (!db->slot[s]) db->used++;
        db->slot[s] = e;
        db->count++;
        log_cmd(db, argc, argv, lens);
        r_ok(o);
        return;
    }
    if (IS("EXPIRE") || IS("PEXPIRE") || IS("PEXPIREAT") || IS("EXPIREAT")) {
        NEED(3);
        struct kentry *e = find(db, ARG(1));
        if (!e || !num(ARG(2), &v)) { r_int(o, 0); return; }
        e->expire = IS("EXPIRE") ? now_ms(db) + v * 1000 : IS("PEXPIRE") ? now_ms(db) + v : IS("EXPIREAT") ? v * 1000 : v;
        log_expire(db, e);
        r_int(o, 1);
        return;
    }
    if (IS("TTL") || IS("PTTL")) {
        NEED(2);
        struct kentry *e = find(db, ARG(1));
        if (!e) r_int(o, -2);
        else if (!e->expire) r_int(o, -1);
        else r_int(o, IS("TTL") ? (e->expire - now_ms(db) + 999) / 1000 : e->expire - now_ms(db));
        return;
    }
    if (IS("PERSIST")) {
        NEED(2);
        struct kentry *e = find(db, ARG(1));
        if (!e || !e->expire) { r_int(o, 0); return; }
        e->expire = 0;
        log_cmd(db, argc, argv, lens);
        r_int(o, 1);
        return;
    }

    /* ---------------------------------------------- strings */
    if (IS("SET")) {
        NEED(3);
        long exp = 0;
        bool nx = false, xx = false;
        for (int i = 3; i < argc; i++) {
            if (!strcasecmp(argv[i], "NX")) nx = true;
            else if (!strcasecmp(argv[i], "XX")) xx = true;
            else if (i + 1 < argc && num(ARG(i + 1), &v) && (!strcasecmp(argv[i], "EX") || !strcasecmp(argv[i], "PX") || !strcasecmp(argv[i], "PXAT"))) {
                exp = !strcasecmp(argv[i], "EX") ? now_ms(db) + v * 1000 : !strcasecmp(argv[i], "PX") ? now_ms(db) + v : v;
                i++;
            } else { r_err(o, "syntax error"); return; }
        }
        struct kentry *e = find(db, ARG(1));
        if ((nx && e) || (xx && !e)) { r_nil(o); return; }
        if (e) del_key(db, ARG(1));
        e = create(db, ARG(1), KT_STR);
        e->str = bdup(ARG(2));
        e->expire = exp;
        if (!db->replay) {
            char at[24];
            snprintf(at, sizeof at, "%ld", exp);
            char *a[] = { "SET", argv[1], argv[2], "PXAT", at };
            size_t l[] = { 3, lens[1], lens[2], 4, strlen(at) };
            log_cmd(db, exp ? 5 : 3, a, l);
        }
        r_ok(o);
        return;
    }
    if (IS("GET")) {
        NEED(2);
        struct kentry *e = find(db, ARG(1));
        if (!e) r_nil(o);
        else if (e->type != KT_STR) r_wrong(o);
        else r_bulk(o, e->str.p, e->str.n);
        return;
    }
    if (IS("GETSET")) {
        NEED(3);
        struct kentry *e = find(db, ARG(1));
        if (e && e->type != KT_STR) { r_wrong(o); return; }
        if (e) { r_bulk(o, e->str.p, e->str.n); free(e->str.p); e->expire = 0; }
        else { r_nil(o); e = create(db, ARG(1), KT_STR); }
        e->str = bdup(ARG(2));
        char *a[] = { "SET", argv[1], argv[2] };
        size_t l[] = { 3, lens[1], lens[2] };
        log_cmd(db, 3, a, l);
        return;
    }
    if (IS("MSET")) {
        if (argc < 3 || argc % 2 == 0) { r_err(o, "wrong number of arguments for 'MSET'"); return; }
        for (int i = 1; i + 1 < argc; i += 2) {
            del_key(db, ARG(i));
            struct kentry *e = create(db, ARG(i), KT_STR);
            e->str = bdup(ARG(i + 1));
        }
        log_cmd(db, argc, argv, lens);
        r_ok(o);
        return;
    }
    if (IS("MGET")) {
        NEED(2);
        r_arr(o, argc - 1);
        for (int i = 1; i < argc; i++) {
            struct kentry *e = find(db, ARG(i));
            if (e && e->type == KT_STR) r_bulk(o, e->str.p, e->str.n); else r_nil(o);
        }
        return;
    }
    if (IS("APPEND")) {
        NEED(3);
        struct kentry *e = typed(db, argv, lens, KT_STR, true, o, &wrong);
        if (wrong) return;
        e->str.p = realloc(e->str.p, e->str.n + lens[2] + 1);
        memcpy(e->str.p + e->str.n, argv[2], lens[2]);
        e->str.n += lens[2];
        e->str.p[e->str.n] = 0;
        log_cmd(db, argc, argv, lens);
        r_int(o, (long)e->str.n);
        return;
    }
    if (IS("STRLEN")) {
        NEED(2);
        struct kentry *e = typed(db, argv, lens, KT_STR, false, o, &wrong);
        if (!wrong) r_int(o, e ? (long)e->str.n : 0);
        return;
    }
    if (IS("INCR") || IS("DECR") || IS("INCRBY") || IS("DECRBY")) {
        NEED(IS("INCR") || IS("DECR") ? 2 : 3);
        long by = 1;
        if (argc > 2 && !num(ARG(2), &by)) { r_err(o, "value is not an integer or out of range"); return; }
        if (IS("DECR") || IS("DECRBY")) by = -by;
        struct kentry *e = typed(db, argv, lens, KT_STR, true, o, &wrong);
        if (wrong) return;
        long cur = 0;
        if (e->str.p && !num(e->str.p, e->str.n, &cur)) { r_err(o, "value is not an integer or out of range"); return; }
        cur += by;
        char t[24];
        snprintf(t, sizeof t, "%ld", cur);
        free(e->str.p);
        e->str = bdup(t, strlen(t));
        char *a[] = { "SET", argv[1], t };
        size_t l[] = { 3, lens[1], strlen(t) };
        log_cmd(db, 3, a, l);
        if (e->expire) log_expire(db, e);
        r_int(o, cur);
        return;
    }

    /* ---------------------------------------------- hashes */
    if (IS("HSET") || IS("HMSET")) {
        if (argc < 4 || argc % 2) { r_err(o, "wrong number of arguments for 'HSET'"); return; }
        struct kentry *e = typed(db, argv, lens, KT_HASH, true, o, &wrong);
        if (wrong) return;
        long added = 0;
        for (int i = 2; i + 1 < argc; i += 2) {
            int k = item_index(e, ARG(i), 2);
            if (k >= 0) { free(e->it[k + 1].p); e->it[k + 1] = bdup(ARG(i + 1)); }
            else { push_item(e, ARG(i), false); push_item(e, ARG(i + 1), false); added++; }
        }
        log_cmd(db, argc, argv, lens);
        if (IS("HMSET")) r_ok(o); else r_int(o, added);
        return;
    }
    if (IS("HGET")) {
        NEED(3);
        struct kentry *e = typed(db, argv, lens, KT_HASH, false, o, &wrong);
        if (wrong) return;
        int k = e ? item_index(e, ARG(2), 2) : -1;
        if (k < 0) r_nil(o); else r_bulk(o, e->it[k + 1].p, e->it[k + 1].n);
        return;
    }
    if (IS("HMGET")) {
        NEED(3);
        struct kentry *e = typed(db, argv, lens, KT_HASH, false, o, &wrong);
        if (wrong) return;
        r_arr(o, argc - 2);
        for (int i = 2; i < argc; i++) {
            int k = e ? item_index(e, ARG(i), 2) : -1;
            if (k < 0) r_nil(o); else r_bulk(o, e->it[k + 1].p, e->it[k + 1].n);
        }
        return;
    }
    if (IS("HDEL")) {
        NEED(3);
        struct kentry *e = typed(db, argv, lens, KT_HASH, false, o, &wrong);
        if (wrong) return;
        long n = 0;
        for (int i = 2; e && i < argc; i++) { int k = item_index(e, ARG(i), 2); if (k >= 0) { remove_items(e, k, 2); n++; } }
        if (e && !e->nit) del_key(db, ARG(1));
        if (n) log_cmd(db, argc, argv, lens);
        r_int(o, n);
        return;
    }
    if (IS("HGETALL") || IS("HKEYS") || IS("HVALS")) {
        NEED(2);
        struct kentry *e = typed(db, argv, lens, KT_HASH, false, o, &wrong);
        if (wrong) return;
        if (!e) { r_arr(o, 0); return; }
        if (IS("HGETALL")) reply_items(o, e, 0, e->nit - 1, 1);
        else reply_items(o, e, IS("HKEYS") ? 0 : 1, e->nit - 1, 2);
        return;
    }
    if (IS("HLEN")) { NEED(2); struct kentry *e = typed(db, argv, lens, KT_HASH, false, o, &wrong); if (!wrong) r_int(o, e ? e->nit / 2 : 0); return; }
    if (IS("HEXISTS")) {
        NEED(3);
        struct kentry *e = typed(db, argv, lens, KT_HASH, false, o, &wrong);
        if (!wrong) r_int(o, e && item_index(e, ARG(2), 2) >= 0);
        return;
    }
    if (IS("HINCRBY")) {
        NEED(4);
        if (!num(ARG(3), &v)) { r_err(o, "value is not an integer or out of range"); return; }
        struct kentry *e = typed(db, argv, lens, KT_HASH, true, o, &wrong);
        if (wrong) return;
        int k = item_index(e, ARG(2), 2);
        long cur = 0;
        if (k >= 0 && !num(e->it[k + 1].p, e->it[k + 1].n, &cur)) { r_err(o, "hash value is not an integer"); return; }
        cur += v;
        char t[24];
        snprintf(t, sizeof t, "%ld", cur);
        if (k >= 0) { free(e->it[k + 1].p); e->it[k + 1] = bdup(t, strlen(t)); }
        else { push_item(e, ARG(2), false); push_item(e, t, strlen(t), false); }
        char *a[] = { "HSET", argv[1], argv[2], t };
        size_t l[] = { 4, lens[1], lens[2], strlen(t) };
        log_cmd(db, 4, a, l);
        r_int(o, cur);
        return;
    }

    /* ----------------------------------------------- lists */
    if (IS("LPUSH") || IS("RPUSH")) {
        NEED(3);
        struct kentry *e = typed(db, argv, lens, KT_LIST, true, o, &wrong);
        if (wrong) return;
        for (int i = 2; i < argc; i++) push_item(e, ARG(i), IS("LPUSH"));
        log_cmd(db, argc, argv, lens);
        r_int(o, e->nit);
        return;
    }
    if (IS("LPOP") || IS("RPOP")) {
        NEED(2);
        struct kentry *e = typed(db, argv, lens, KT_LIST, false, o, &wrong);
        if (wrong) return;
        if (!e || !e->nit) { r_nil(o); return; }
        int k = IS("LPOP") ? 0 : e->nit - 1;
        r_bulk(o, e->it[k].p, e->it[k].n);
        remove_items(e, k, 1);
        if (!e->nit) del_key(db, ARG(1));
        log_cmd(db, 2, argv, lens);
        return;
    }
    if (IS("LRANGE")) {
        NEED(4);
        long a, b;
        if (!num(ARG(2), &a) || !num(ARG(3), &b)) { r_err(o, "value is not an integer"); return; }
        struct kentry *e = typed(db, argv, lens, KT_LIST, false, o, &wrong);
        if (wrong) return;
        if (!e) { r_arr(o, 0); return; }
        if (a < 0) a += e->nit;
        if (b < 0) b += e->nit;
        if (a < 0) a = 0;
        if (b >= e->nit) b = e->nit - 1;
        if (a > b) { r_arr(o, 0); return; }
        reply_items(o, e, (int)a, (int)b, 1);
        return;
    }
    if (IS("LLEN")) { NEED(2); struct kentry *e = typed(db, argv, lens, KT_LIST, false, o, &wrong); if (!wrong) r_int(o, e ? e->nit : 0); return; }
    if (IS("LINDEX")) {
        NEED(3);
        struct kentry *e = typed(db, argv, lens, KT_LIST, false, o, &wrong);
        if (wrong) return;
        if (!e || !num(ARG(2), &v)) { r_nil(o); return; }
        if (v < 0) v += e->nit;
        if (v < 0 || v >= e->nit) r_nil(o); else r_bulk(o, e->it[v].p, e->it[v].n);
        return;
    }
    if (IS("LSET")) {
        NEED(4);
        struct kentry *e = typed(db, argv, lens, KT_LIST, false, o, &wrong);
        if (wrong) return;
        if (!e) { r_err(o, "no such key"); return; }
        if (!num(ARG(2), &v)) { r_err(o, "value is not an integer"); return; }
        if (v < 0) v += e->nit;
        if (v < 0 || v >= e->nit) { r_err(o, "index out of range"); return; }
        free(e->it[v].p);
        e->it[v] = bdup(ARG(3));
        log_cmd(db, argc, argv, lens);
        r_ok(o);
        return;
    }
    if (IS("LREM")) {
        NEED(4);
        struct kentry *e = typed(db, argv, lens, KT_LIST, false, o, &wrong);
        if (wrong) return;
        if (!e || !num(ARG(2), &v)) { r_int(o, 0); return; }
        long n = 0;
        for (int i = 0; i < e->nit && (!v || n < (v < 0 ? -v : v));) {
            if (beq(&e->it[i], ARG(3))) { remove_items(e, i, 1); n++; } else i++;
        }
        if (!e->nit) del_key(db, ARG(1));
        if (n) log_cmd(db, argc, argv, lens);
        r_int(o, n);
        return;
    }

    /* ------------------------------------------------ sets */
    if (IS("SADD")) {
        NEED(3);
        struct kentry *e = typed(db, argv, lens, KT_SET, true, o, &wrong);
        if (wrong) return;
        long n = 0;
        for (int i = 2; i < argc; i++) if (item_index(e, ARG(i), 1) < 0) { push_item(e, ARG(i), false); n++; }
        if (n) log_cmd(db, argc, argv, lens);
        r_int(o, n);
        return;
    }
    if (IS("SREM")) {
        NEED(3);
        struct kentry *e = typed(db, argv, lens, KT_SET, false, o, &wrong);
        if (wrong) return;
        long n = 0;
        for (int i = 2; e && i < argc; i++) { int k = item_index(e, ARG(i), 1); if (k >= 0) { remove_items(e, k, 1); n++; } }
        if (e && !e->nit) del_key(db, ARG(1));
        if (n) log_cmd(db, argc, argv, lens);
        r_int(o, n);
        return;
    }
    if (IS("SMEMBERS")) {
        NEED(2);
        struct kentry *e = typed(db, argv, lens, KT_SET, false, o, &wrong);
        if (wrong) return;
        if (!e) r_arr(o, 0); else reply_items(o, e, 0, e->nit - 1, 1);
        return;
    }
    if (IS("SISMEMBER")) {
        NEED(3);
        struct kentry *e = typed(db, argv, lens, KT_SET, false, o, &wrong);
        if (!wrong) r_int(o, e && item_index(e, ARG(2), 1) >= 0);
        return;
    }
    if (IS("SCARD")) { NEED(2); struct kentry *e = typed(db, argv, lens, KT_SET, false, o, &wrong); if (!wrong) r_int(o, e ? e->nit : 0); return; }

    kb_printf(o, "-ERR unknown command '%s' (HELP lists them)\r\n", cmd);
#undef NEED
}

/* ------------------------------------------------------- open / close */

struct kvdb *kv_open(const char *path, char *err, size_t elen) {
    struct kvdb *db = calloc(1, sizeof *db);
    strlcpy(db->path, path, sizeof db->path);
    db->base_ms = unix_time() * 1000 - uptime_ms();
    db->fd = -1;
    rehash(db, 64);
    char dir[ITX_PATH_MAX];
    strlcpy(dir, path, sizeof dir);
    char *s = strrchr(dir, '/');
    if (s && s != dir) { *s = 0; k_mkdirs(dir); }
    /* the commands of the file, again */
    size_t len;
    char *t = read_file(path, &len, 1u << 30);
    if (t) {
        db->replay = true;
        size_t at = 0;
        struct kbuf sink = { 0 };
        while (at < len) {
            int argc;
            char **argv;
            size_t *lens;
            long used = resp_parse(t + at, len - at, &argc, &argv, &lens);
            if (used <= 0) break;                          /* a command cut off by a crash: the rest is dropped */
            sink.n = 0;
            kv_exec(db, argc, argv, lens, &sink);
            resp_free(argc, argv, lens);
            at += (size_t)used;
        }
        kb_free(&sink);
        free(t);
        db->replay = false;
        if (at < len) {
            /* a half-written last command: the file is cut there */
            truncate(path, (long)at);
            len = at;
        }
        db->aof_bytes = len;
    }
    db->fd = open(path, O_WRONLY | O_CREATE | O_APPEND);
    if (db->fd < 0) {
        snprintf(err, elen, "%s: %s", path, strerror(db->fd));
        kv_close(db);
        return NULL;
    }
    /* a file much bigger than its data is written again */
    if (db->aof_bytes > 4u << 20 && db->aof_bytes > db->count * 512) kv_save(db);
    return db;
}

void kv_close(struct kvdb *db) {
    if (!db) return;
    if (db->fd >= 0) { close(db->fd); sync(); }
    for (size_t i = 0; i < db->nslot; i++) if (db->slot[i] && db->slot[i] != TOMB) entry_free(db->slot[i]);
    free(db->slot);
    free(db);
}

/* ---------------------------------------------------------------- RESP */

static long line_end(const char *p, size_t n, size_t from) {
    for (size_t i = from; i + 1 < n; i++) if (p[i] == '\r' && p[i + 1] == '\n') return (long)i;
    return -1;
}

long resp_parse(const char *p, size_t n, int *argc, char ***argv, size_t **lens) {
    *argc = 0;
    *argv = NULL;
    *lens = NULL;
    if (!n) return 0;
    if (p[0] != '*') {
        /* an inline command: a line of words (telnet, nc) */
        size_t e = 0;
        while (e < n && p[e] != '\n') e++;
        if (e == n) return 0;
        char *line = malloc(e + 1);
        memcpy(line, p, e);
        line[e] = 0;
        if (e && line[e - 1] == '\r') line[e - 1] = 0;
        *argc = kv_split(line, argv, lens);
        free(line);
        return (long)(e + 1);
    }
    long le = line_end(p, n, 1);
    if (le < 0) return 0;
    long cnt = atol(p + 1);
    if (cnt < 0 || cnt > 1048576) return -1;
    size_t at = (size_t)le + 2;
    char **a = calloc((size_t)cnt + 1, sizeof(char *));
    size_t *l = calloc((size_t)cnt + 1, sizeof(size_t));
    for (long i = 0; i < cnt; i++) {
        if (at >= n) { resp_free((int)i, a, l); return 0; }
        if (p[at] != '$') { resp_free((int)i, a, l); return -1; }
        long e = line_end(p, n, at + 1);
        if (e < 0) { resp_free((int)i, a, l); return 0; }
        long bl = atol(p + at + 1);
        if (bl < 0 || bl > (512L << 20)) { resp_free((int)i, a, l); return -1; }
        at = (size_t)e + 2;
        if (at + (size_t)bl + 2 > n) { resp_free((int)i, a, l); return 0; }
        a[i] = malloc((size_t)bl + 1);
        memcpy(a[i], p + at, (size_t)bl);
        a[i][bl] = 0;
        l[i] = (size_t)bl;
        at += (size_t)bl + 2;
    }
    *argc = (int)cnt;
    *argv = a;
    *lens = l;
    return (long)at;
}

void resp_free(int argc, char **argv, size_t *lens) {
    for (int i = 0; i < argc; i++) free(argv[i]);
    free(argv);
    free(lens);
}

/* one answer as text; returns the bytes it took */
static size_t print_one(const char *p, size_t n, struct kbuf *o, int depth, int index) {
    if (!n) return 0;
    long le = line_end(p, n, 1);
    if (le < 0) return n;
    char head[64];
    size_t hl = (size_t)le - 1 < sizeof head - 1 ? (size_t)le - 1 : sizeof head - 1;
    memcpy(head, p + 1, hl);
    head[hl] = 0;
    size_t at = (size_t)le + 2;
    if (index) for (int i = 0; i < depth - 1; i++) kb_str(o, "   ");
    if (index) kb_printf(o, "%d) ", index);
    switch (p[0]) {
    case '+': kb_printf(o, "%s\n", head); return at;
    case '-': kb_printf(o, "(error) %s\n", head); return at;
    case ':': kb_printf(o, "(integer) %s\n", head); return at;
    case '$': {
        long bl = atol(head);
        if (bl < 0) { kb_str(o, "(nil)\n"); return at; }
        bool text = true;
        for (long i = 0; i < bl && at + (size_t)i < n; i++) if ((unsigned char)p[at + i] < 0x20 && p[at + i] != '\n' && p[at + i] != '\r' && p[at + i] != '\t') text = false;
        if (bl > 200 && memchr(p + at, '\n', (size_t)bl)) {     /* long text (INFO, HELP): as it is */
            kb_put(o, p + at, (size_t)bl);
            kb_str(o, "\n");
        } else if (text) {
            kb_str(o, "\"");
            kb_put(o, p + at, (size_t)bl);
            kb_str(o, "\"\n");
        } else {
            kb_str(o, "\"");
            for (long i = 0; i < bl; i++) {
                unsigned char c = (unsigned char)p[at + i];
                if (c >= 0x20 && c < 0x7F && c != '"' && c != '\\') kb_put(o, &c, 1);
                else kb_printf(o, "\\x%02x", c);
            }
            kb_str(o, "\"\n");
        }
        return at + (size_t)bl + 2;
    }
    case '*': {
        long cnt = atol(head);
        if (cnt < 0) { kb_str(o, "(nil)\n"); return at; }
        if (cnt == 0) { kb_str(o, "(empty array)\n"); return at; }
        for (long i = 0; i < cnt && at < n; i++) {
            if (i && index) for (int k = 0; k < depth; k++) kb_str(o, "   ");
            at += print_one(p + at, n - at, o, depth + 1, (int)i + 1);
        }
        return at;
    }
    }
    return n;
}

void resp_print(const char *p, size_t n, struct kbuf *out) {
    size_t at = 0;
    while (at < n) {
        size_t k = print_one(p + at, n - at, out, 0, 0);
        if (!k) break;
        at += k;
    }
}

int kv_split(const char *line, char ***argv, size_t **lens) {
    int cap = 8, n = 0;
    char **a = malloc(sizeof(char *) * (size_t)cap);
    size_t *l = malloc(sizeof(size_t) * (size_t)cap);
    const char *p = line;
    for (;;) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        if (!*p) break;
        struct kbuf w = { 0 };
        kb_put(&w, "", 0);
        if (*p == '"' || *p == '\'') {
            char q = *p++;
            while (*p && *p != q) {
                if (q == '"' && *p == '\\' && p[1]) {
                    p++;
                    char c = *p == 'n' ? '\n' : *p == 't' ? '\t' : *p == 'r' ? '\r' : *p;
                    if (*p == 'x' && isxdigit((unsigned char)p[1]) && isxdigit((unsigned char)p[2])) {
                        int hi = isdigit(p[1]) ? p[1] - '0' : (p[1] | 32) - 'a' + 10, lo = isdigit(p[2]) ? p[2] - '0' : (p[2] | 32) - 'a' + 10;
                        c = (char)(hi << 4 | lo);
                        p += 2;
                    }
                    kb_put(&w, &c, 1);
                    p++;
                } else {
                    kb_put(&w, p++, 1);
                }
            }
            if (*p) p++;
        } else {
            while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') kb_put(&w, p++, 1);
        }
        if (n == cap) { cap *= 2; a = realloc(a, sizeof(char *) * (size_t)cap); l = realloc(l, sizeof(size_t) * (size_t)cap); }
        a[n] = w.p;
        l[n] = w.n;
        n++;
    }
    *argv = a;
    *lens = l;
    return n;
}
