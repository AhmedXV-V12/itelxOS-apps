/* The vault of File saver (vault.h). */
#include "vault.h"

const char *const vault_folders[] = { "Inbox", "Documents", "Pictures", "Music", "Videos", "Apps", "Archives", "Code", "Data",
                                      "Private", "Other" };
const int vault_nfolders = 11;

void vault_init(void) {
    k_mkdirs(VMETA);
    k_mkdirs(VTRASH);
    k_mkdirs(VVERS);
    for (int i = 0; i < vault_nfolders; i++) {
        char p[64];
        snprintf(p, sizeof p, VAULT "/%s", vault_folders[i]);
        k_mkdirs(p);
    }
}

const char *vault_base(const char *p) {
    const char *s = strrchr(p, '/');
    return s ? s + 1 : p;
}

bool vault_hidden(const char *name) { return name[0] == '.'; }

static const char *ext_of(const char *name) {
    const char *d = strrchr(name, '.');
    return d && d != name ? d + 1 : "";
}

const char *vault_category(const char *name) {
    static const struct { const char *cat, *exts; } map[] = {
        { "Documents", " txt md pdf doc docx odt rtf xls xlsx ods ppt pptx odp epub tex log " },
        { "Pictures", " png jpg jpeg gif bmp webp svg ico tif tiff ppm pgm heic raw " },
        { "Music", " mp3 wav ogg flac m4a aac wma opus mid midi " },
        { "Videos", " mp4 mkv avi mov webm wmv flv m4v mpg mpeg 3gp " },
        { "Apps", " xvr xvc exe msi apk deb rpm appimage dmg iso img efi bin sh bat cmd ps1 " },
        { "Archives", " zip rar 7z tar gz bz2 xz tgz zst cab lz4 " },
        { "Code", " c h cpp hpp cc py js ts java go rs cs php rb html htm css xml yml yaml toml ini cfg asm s mk " },
        { "Data", " csv tsv json jsonl db sqlite sqlite3 kv vec parquet npy npz h5 hdf5 arrow avro sql bak " },
    };
    char e[24];
    const char *x = ext_of(name);
    if (!*x || strlen(x) > 20) return "Other";
    snprintf(e, sizeof e, " %s ", x);
    for (char *p = e; *p; p++) *p = (char)tolower((unsigned char)*p);
    if (ends_with(name, LOCKED_EXT)) return "Private";
    for (size_t i = 0; i < sizeof map / sizeof map[0]; i++) if (strstr(map[i].exts, e)) return map[i].cat;
    return "Other";
}

/* ------------------------------------------------------------ helpers */

static int copy_file(const char *from, const char *to, uint64_t *bytes) {
    int in = open(from, O_RDONLY);
    if (in < 0) return in;
    char tmp[ITX_PATH_MAX];
    snprintf(tmp, sizeof tmp, "%s.part", to);
    int out = open(tmp, O_WRONLY | O_CREATE | O_TRUNC);
    if (out < 0) { close(in); return out; }
    static char buf[65536];
    int r = 0;
    for (;;) {
        ssize_t n = read(in, buf, sizeof buf);
        if (n == 0) break;
        if (n < 0) { r = (int)n; break; }
        if (write(out, buf, (size_t)n) != n) { r = -ENOSPC; break; }
        if (bytes) *bytes += (uint64_t)n;
    }
    close(in);
    close(out);
    if (r) { remove(tmp); return r; }
    remove(to);
    return rename(tmp, to);
}

int vault_sha256_file(const char *path, uint8_t out[32], uint64_t *size) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return fd;
    struct k_sha256 c;
    k_sha256_init(&c);
    static uint8_t buf[65536];
    uint64_t n = 0;
    for (;;) {
        ssize_t r = read(fd, buf, sizeof buf);
        if (r < 0) { close(fd); return (int)r; }
        if (r == 0) break;
        k_sha256_add(&c, buf, (size_t)r);
        n += (uint64_t)r;
    }
    close(fd);
    k_sha256_end(&c, out);
    if (size) *size = n;
    return 0;
}

static bool same_file(const char *a, const char *b) {
    uint8_t x[32], y[32];
    uint64_t sa, sb;
    struct itx_stat st1, st2;
    if (stat(a, &st1) || stat(b, &st2) || st1.size != st2.size) return false;
    return !vault_sha256_file(a, x, &sa) && !vault_sha256_file(b, y, &sb) && !memcmp(x, y, 32);
}

/* calls fn for every file under dir (folders whose name starts with a dot
   are left out unless all is set) */
static int walk(const char *dir, bool all, int (*fn)(const char *path, const struct itx_dirent *de, void *ctx), void *ctx) {
    int fd = open(dir, O_RDONLY);
    if (fd < 0) return fd;
    int cap = 32, n = 0;
    struct itx_dirent *list = malloc(sizeof *list * cap);
    struct itx_dirent de;
    while (readdir(fd, &de) == 1) {
        if (n == cap) { cap *= 2; list = realloc(list, sizeof *list * cap); }
        list[n++] = de;
    }
    close(fd);
    int r = 0;
    for (int i = 0; i < n && !r; i++) {
        char p[ITX_PATH_MAX];
        snprintf(p, sizeof p, "%s/%s", strcmp(dir, "/") ? dir : "", list[i].name);
        if (list[i].type == VT_DIR) {
            if (!all && vault_hidden(list[i].name)) continue;
            r = walk(p, all, fn, ctx);
        } else if (list[i].type == VT_FILE) {
            r = fn(p, &list[i], ctx);
        }
    }
    free(list);
    return r;
}

static void stamp(char *out, size_t len) {
    long t = k_now();
    int y, m, d;
    k_civil(t / 86400, &y, &m, &d);
    snprintf(out, len, "%04d%02d%02d-%02ld%02ld%02ld", y, m, d, t % 86400 / 3600, t % 3600 / 60, t % 60);
}

/* -------------------------------------------------------------- store */

/* the earlier version of dst goes to /vault/.versions/<its folder>/NAME.<time> */
static void keep_version(const char *dst) {
    char rel[ITX_PATH_MAX], dir[ITX_PATH_MAX], vp[ITX_PATH_MAX], st[32];
    strlcpy(rel, dst + strlen(VAULT), sizeof rel);
    strlcpy(dir, rel, sizeof dir);
    char *s = strrchr(dir, '/');
    if (s) *s = 0;
    stamp(st, sizeof st);
    snprintf(vp, sizeof vp, VVERS "%s", dir);
    k_mkdirs(vp);
    snprintf(vp, sizeof vp, VVERS "%s/%s.%s", dir, vault_base(dst), st);
    rename(dst, vp);
}

struct store_ctx { const char *src_root, *dst_root; int files; uint64_t bytes; int err; };

static int store_one(const char *path, const struct itx_dirent *de, void *vctx) {
    struct store_ctx *c = vctx;
    char dst[ITX_PATH_MAX];
    snprintf(dst, sizeof dst, "%s%s", c->dst_root, path + strlen(c->src_root));
    char dir[ITX_PATH_MAX];
    strlcpy(dir, dst, sizeof dir);
    char *s = strrchr(dir, '/');
    if (s) { *s = 0; k_mkdirs(dir); }
    struct itx_stat st;
    if (stat(dst, &st) == 0) {
        if (same_file(path, dst)) return 0;
        keep_version(dst);
    }
    int r = copy_file(path, dst, &c->bytes);
    if (r) { c->err = r; return r; }
    c->files++;
    return 0;
}

int vault_store(const char *src, const char *folder, char *dst, size_t dlen, int *files, uint64_t *bytes) {
    vault_init();
    struct itx_stat st;
    int r = stat(src, &st);
    if (r) return r;
    char target[ITX_PATH_MAX];
    *files = 0;
    *bytes = 0;
    if (st.type == VT_DIR) {
        snprintf(target, sizeof target, "%s/%s", folder ? folder : VAULT "/Inbox", vault_base(src));
        k_mkdirs(target);
        struct store_ctx c = { src, target, 0, 0, 0 };
        walk(src, true, store_one, &c);
        *files = c.files;
        *bytes = c.bytes;
        strlcpy(dst, target, dlen);
        vault_index_update();
        return c.err;
    }
    snprintf(target, sizeof target, "%s/%s", folder ? folder : "", vault_base(src));
    if (!folder) snprintf(target, sizeof target, VAULT "/%s/%s", vault_category(vault_base(src)), vault_base(src));
    strlcpy(dst, target, dlen);
    char dir[ITX_PATH_MAX];
    strlcpy(dir, target, sizeof dir);
    char *s = strrchr(dir, '/');
    if (s) { *s = 0; k_mkdirs(dir); }
    if (stat(target, &st) == 0) {
        if (same_file(src, target)) return 0;
        keep_version(target);
    }
    r = copy_file(src, target, bytes);
    if (!r) *files = 1;
    vault_index_update();
    sync();
    return r;
}

int vault_organize(const char *dir, int *moved) {
    *moved = 0;
    int fd = open(dir, O_RDONLY);
    if (fd < 0) return fd;
    char names[256][256];
    int n = 0;
    struct itx_dirent de;
    while (readdir(fd, &de) == 1 && n < 256) if (de.type == VT_FILE) strlcpy(names[n++], de.name, sizeof names[0]);
    close(fd);
    for (int i = 0; i < n; i++) {
        char from[ITX_PATH_MAX], to[ITX_PATH_MAX], todir[ITX_PATH_MAX];
        snprintf(from, sizeof from, "%s/%s", dir, names[i]);
        snprintf(todir, sizeof todir, VAULT "/%s", vault_category(names[i]));
        if (!strcmp(todir, dir)) continue;
        k_mkdirs(todir);
        snprintf(to, sizeof to, "%s/%s", todir, names[i]);
        struct itx_stat st;
        if (stat(to, &st) == 0) {
            if (same_file(from, to)) { remove(from); (*moved)++; continue; }
            keep_version(to);
        }
        if (rename(from, to) == 0) (*moved)++;
    }
    vault_index_update();
    sync();
    return 0;
}

/* -------------------------------------------------------------- trash */

int vault_trash(const char *path) {
    k_mkdirs(VTRASH);
    char st[32], to[ITX_PATH_MAX], from[ITX_PATH_MAX];
    stamp(st, sizeof st);
    snprintf(to, sizeof to, VTRASH "/%s-%s", st, vault_base(path));
    int r = rename(path, to);
    if (r) return r;
    snprintf(from, sizeof from, "%s.from", to);
    write_file(from, path, strlen(path), false);
    sync();
    return 0;
}

int vault_trash_restore(const char *name, char *where, size_t len) {
    char p[ITX_PATH_MAX], f[ITX_PATH_MAX];
    snprintf(p, sizeof p, VTRASH "/%s", name);
    snprintf(f, sizeof f, "%s.from", p);
    size_t l;
    char *orig = read_file(f, &l, ITX_PATH_MAX);
    if (!orig) return -ENOENT;
    strlcpy(where, orig, len);
    free(orig);
    char dir[ITX_PATH_MAX];
    strlcpy(dir, where, sizeof dir);
    char *s = strrchr(dir, '/');
    if (s) { *s = 0; k_mkdirs(dir); }
    struct itx_stat st;
    if (stat(where, &st) == 0) strlcat(where, ".restored", len);
    int r = rename(p, where);
    if (!r) remove(f);
    sync();
    return r;
}

static int del_tree(const char *p) {
    struct itx_stat st;
    int r = stat(p, &st);
    if (r) return r;
    if (st.type == VT_DIR) {
        int fd = open(p, O_RDONLY);
        if (fd < 0) return fd;
        char (*names)[256] = malloc(256 * 256);
        int n = 0;
        struct itx_dirent de;
        while (readdir(fd, &de) == 1 && n < 256) strlcpy(names[n++], de.name, 256);
        close(fd);
        for (int i = 0; i < n; i++) {
            char q[ITX_PATH_MAX];
            snprintf(q, sizeof q, "%s/%s", p, names[i]);
            del_tree(q);
        }
        free(names);
    }
    return remove(p);
}

int vault_trash_empty(int *n) {
    *n = 0;
    int fd = open(VTRASH, O_RDONLY);
    if (fd < 0) return fd;
    char (*names)[256] = malloc(512 * 256);
    int k = 0;
    struct itx_dirent de;
    while (readdir(fd, &de) == 1 && k < 512) strlcpy(names[k++], de.name, 256);
    close(fd);
    for (int i = 0; i < k; i++) {
        char p[ITX_PATH_MAX];
        snprintf(p, sizeof p, VTRASH "/%s", names[i]);
        if (!del_tree(p) && !ends_with(names[i], ".from")) (*n)++;
    }
    free(names);
    sync();
    return 0;
}

int vault_versions(const char *path, char names[][128], int max) {
    char rel[ITX_PATH_MAX], dir[ITX_PATH_MAX];
    if (strncmp(path, VAULT "/", strlen(VAULT) + 1)) return 0;
    strlcpy(rel, path + strlen(VAULT), sizeof rel);
    char *s = strrchr(rel, '/');
    if (!s) return 0;
    *s = 0;
    snprintf(dir, sizeof dir, VVERS "%s", rel);
    int fd = open(dir, O_RDONLY);
    if (fd < 0) return 0;
    const char *base = vault_base(path);
    size_t bl = strlen(base);
    int n = 0;
    struct itx_dirent de;
    while (readdir(fd, &de) == 1 && n < max)
        if (!strncmp(de.name, base, bl) && de.name[bl] == '.') snprintf(names[n++], 128, "%s/%s", dir + strlen(VVERS), de.name);
    close(fd);
    /* newest first: the names end with the time */
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && strcmp(names[j - 1], names[j]) < 0; j--) {
            char t[128];
            memcpy(t, names[j], 128);
            memcpy(names[j], names[j - 1], 128);
            memcpy(names[j - 1], t, 128);
        }
    return n;
}

/* -------------------------------------------------------------- index */

struct ient { char hash[65]; uint64_t size, mtime; char *path; bool seen; };
struct index { struct ient *e; int n, cap; };

static void index_load(struct index *ix) {
    memset(ix, 0, sizeof *ix);
    size_t len;
    char *t = read_file(VINDEX, &len, 64u << 20);
    if (!t) return;
    for (char *p = t; *p;) {
        char *eol = strchr(p, '\n');
        if (eol) *eol = 0;
        char *a = strchr(p, ' '), *b = a ? strchr(a + 1, ' ') : NULL, *c = b ? strchr(b + 1, ' ') : NULL;
        if (a && b && c && a - p == 64) {
            if (ix->n == ix->cap) { ix->cap = ix->cap ? ix->cap * 2 : 256; ix->e = realloc(ix->e, sizeof *ix->e * ix->cap); }
            struct ient *e = &ix->e[ix->n++];
            memcpy(e->hash, p, 64);
            e->hash[64] = 0;
            e->size = strtoul(a + 1, NULL, 10);
            e->mtime = strtoul(b + 1, NULL, 10);
            e->path = strdup(c + 1);
            e->seen = false;
        }
        if (!eol) break;
        p = eol + 1;
    }
    free(t);
}

static struct ient *index_find(struct index *ix, const char *path) {
    for (int i = 0; i < ix->n; i++) if (!strcmp(ix->e[i].path, path)) return &ix->e[i];
    return NULL;
}

static void index_free(struct index *ix) {
    for (int i = 0; i < ix->n; i++) free(ix->e[i].path);
    free(ix->e);
}

struct upd_ctx { struct index *old; char *out; size_t len, cap; int hashed; };

static int upd_one(const char *path, const struct itx_dirent *de, void *vctx) {
    struct upd_ctx *c = vctx;
    struct itx_stat st;
    if (stat(path, &st)) return 0;
    if (ends_with(path, ".part")) return 0;
    struct ient *e = index_find(c->old, path);
    char hash[65];
    if (e && e->size == st.size && e->mtime == st.mtime) {
        memcpy(hash, e->hash, 65);
    } else {
        uint8_t h[32];
        if (vault_sha256_file(path, h, NULL)) return 0;
        k_hex(hash, h, 32);
        c->hashed++;
    }
    size_t need = strlen(path) + 128;
    if (c->len + need >= c->cap) { while (c->len + need >= c->cap) c->cap *= 2; c->out = realloc(c->out, c->cap); }
    c->len += (size_t)snprintf(c->out + c->len, c->cap - c->len, "%s %lu %lu %s\n", hash, (unsigned long)st.size,
                               (unsigned long)st.mtime, path);
    return 0;
}

int vault_index_update(void) {
    struct index ix;
    index_load(&ix);
    struct upd_ctx c = { &ix, malloc(65536), 0, 65536, 0 };
    walk(VAULT, false, upd_one, &c);
    k_mkdirs(VMETA);
    k_save_file(VINDEX, c.out, c.len);
    free(c.out);
    index_free(&ix);
    return c.hashed;
}

struct chk_ctx { struct index *ix; struct vcheck *r; void (*report)(const char *, const char *); };

static int chk_one(const char *path, const struct itx_dirent *de, void *vctx) {
    struct chk_ctx *c = vctx;
    if (ends_with(path, ".part")) return 0;
    struct ient *e = index_find(c->ix, path);
    uint8_t h[32];
    char hash[65];
    if (vault_sha256_file(path, h, NULL)) return 0;
    k_hex(hash, h, 32);
    if (!e) { c->r->added++; if (c->report) c->report(path, "new (not in the index)"); return 0; }
    e->seen = true;
    if (strcmp(e->hash, hash)) { c->r->changed++; if (c->report) c->report(path, "CHANGED since it was indexed"); }
    else c->r->ok++;
    return 0;
}

int vault_check(struct vcheck *r, void (*report)(const char *path, const char *what)) {
    memset(r, 0, sizeof *r);
    struct index ix;
    index_load(&ix);
    struct chk_ctx c = { &ix, r, report };
    walk(VAULT, false, chk_one, &c);
    for (int i = 0; i < ix.n; i++)
        if (!ix.e[i].seen) { r->missing++; if (report) report(ix.e[i].path, "MISSING"); }
    index_free(&ix);
    return 0;
}

/* ------------------------------------------------------ the password */

#define KDF_ITER 200000

static uint8_t master[64];          /* 0..31 the cipher key, 32..63 the MAC key */
static bool have_master;

bool vault_has_password(void) { struct itx_stat st; return stat(VKEY, &st) == 0; }
bool vault_unlocked(void) { return have_master; }
void vault_forget_key(void) { memset(master, 0, sizeof master); have_master = false; }

static void kv(const char *t, const char *key, char *out, size_t len) {
    out[0] = 0;
    const char *p = strstr(t, key);
    if (!p) return;
    p += strlen(key);
    size_t n = strcspn(p, "\n");
    if (n >= len) n = len - 1;
    memcpy(out, p, n);
    out[n] = 0;
}

static int unhex(const char *s, uint8_t *out, size_t max) {
    size_t n = 0;
    for (; s[0] && s[1] && n < max; s += 2) {
        int a = isdigit(s[0]) ? s[0] - '0' : (s[0] | 32) - 'a' + 10, b = isdigit(s[1]) ? s[1] - '0' : (s[1] | 32) - 'a' + 10;
        if (a < 0 || a > 15 || b < 0 || b > 15) return -1;
        out[n++] = (uint8_t)(a << 4 | b);
    }
    return (int)n;
}

/* the key file: the master key locked with a key from the password */
static int write_key(const char *password) {
    uint8_t salt[16], nonce[12], kek[64], wrapped[64], check[32];
    k_random(salt, sizeof salt);
    k_random(nonce, sizeof nonce);
    k_pbkdf2_sha256(password, strlen(password), salt, sizeof salt, KDF_ITER, kek, sizeof kek);
    memcpy(wrapped, master, 64);
    struct k_chacha20 c;
    k_chacha20_init(&c, kek, nonce, 0);
    k_chacha20_xor(&c, wrapped, 64);
    k_hmac_sha256(kek + 32, 32, wrapped, 64, check);
    char hs[40], hn[30], hw[130], hc[70], text[400];
    k_hex(hs, salt, 16);
    k_hex(hn, nonce, 12);
    k_hex(hw, wrapped, 64);
    k_hex(hc, check, 32);
    int n = snprintf(text, sizeof text, "# the vault key, locked with the password (PBKDF2-SHA-256, ChaCha20, HMAC-SHA-256)\n"
                     "salt=%s\niter=%d\nnonce=%s\nwrapped=%s\ncheck=%s\n", hs, KDF_ITER, hn, hw, hc);
    memset(kek, 0, sizeof kek);
    k_mkdirs(VMETA);
    return k_save_file(VKEY, text, (size_t)n) ? 0 : -EIO;
}

int vault_open(const char *password) {
    size_t len;
    char *t = read_file(VKEY, &len, 4096);
    if (!t) return -ENOENT;
    char s[64], it[16], no[64], wr[160], ch[80];
    kv(t, "salt=", s, sizeof s);
    kv(t, "iter=", it, sizeof it);
    kv(t, "nonce=", no, sizeof no);
    kv(t, "wrapped=", wr, sizeof wr);
    kv(t, "check=", ch, sizeof ch);
    free(t);
    uint8_t salt[16], nonce[12], wrapped[64], check[32], kek[64], mine[32];
    long iter = atol(it);
    if (unhex(s, salt, 16) != 16 || unhex(no, nonce, 12) != 12 || unhex(wr, wrapped, 64) != 64 || unhex(ch, check, 32) != 32 ||
        iter < 1000 || iter > 10000000)
        return -ECORRUPT;
    k_pbkdf2_sha256(password, strlen(password), salt, 16, (uint32_t)iter, kek, 64);
    k_hmac_sha256(kek + 32, 32, wrapped, 64, mine);
    uint8_t diff = 0;
    for (int i = 0; i < 32; i++) diff |= mine[i] ^ check[i];
    if (diff) { memset(kek, 0, sizeof kek); return -EACCES; }
    struct k_chacha20 c;
    k_chacha20_init(&c, kek, nonce, 0);
    memcpy(master, wrapped, 64);
    k_chacha20_xor(&c, master, 64);
    memset(kek, 0, sizeof kek);
    have_master = true;
    return 0;
}

int vault_set_password(const char *old, const char *new_password) {
    if (vault_has_password()) {
        int r = vault_open(old ? old : "");
        if (r) return r;
    } else {
        k_random(master, sizeof master);
        have_master = true;
    }
    return write_key(new_password);
}

/* ------------------------------------------------------- locked files */

#define LMAGIC "ITXLOCK1"
#define LHDR   (8 + 12 + 8 + 2)

struct hmac { struct k_sha256 in; uint8_t opad[64]; };

static void hmac_begin(struct hmac *h, const uint8_t key[32]) {
    uint8_t ipad[64];
    for (int i = 0; i < 64; i++) { uint8_t k = i < 32 ? key[i] : 0; ipad[i] = k ^ 0x36; h->opad[i] = k ^ 0x5c; }
    k_sha256_init(&h->in);
    k_sha256_add(&h->in, ipad, 64);
}

static void hmac_end(struct hmac *h, uint8_t out[32]) {
    uint8_t inner[32];
    k_sha256_end(&h->in, inner);
    struct k_sha256 o;
    k_sha256_init(&o);
    k_sha256_add(&o, h->opad, 64);
    k_sha256_add(&o, inner, 32);
    k_sha256_end(&o, out);
}

int vault_lock_file(const char *path) {
    if (!have_master) return -EACCES;
    if (ends_with(path, LOCKED_EXT)) return -EINVAL;
    struct itx_stat st;
    int r = stat(path, &st);
    if (r) return r;
    if (st.type != VT_FILE) return -EISDIR;
    char out[ITX_PATH_MAX], tmp[ITX_PATH_MAX];
    snprintf(out, sizeof out, "%s" LOCKED_EXT, path);
    snprintf(tmp, sizeof tmp, "%s.part", out);
    int in = open(path, O_RDONLY);
    if (in < 0) return in;
    int fd = open(tmp, O_WRONLY | O_CREATE | O_TRUNC);
    if (fd < 0) { close(in); return fd; }
    const char *name = vault_base(path);
    uint16_t nl = (uint16_t)strlen(name);
    uint8_t hdr[LHDR], nonce[12];
    k_random(nonce, 12);
    memcpy(hdr, LMAGIC, 8);
    memcpy(hdr + 8, nonce, 12);
    for (int i = 0; i < 8; i++) hdr[20 + i] = (uint8_t)(st.size >> (8 * i));
    hdr[28] = (uint8_t)nl;
    hdr[29] = (uint8_t)(nl >> 8);
    struct hmac h;
    hmac_begin(&h, master + 32);
    k_sha256_add(&h.in, hdr, LHDR);
    struct k_chacha20 c;
    k_chacha20_init(&c, master, nonce, 1);
    r = write(fd, hdr, LHDR) == LHDR ? 0 : -ENOSPC;
    static uint8_t buf[65536];
    if (!r) {
        memcpy(buf, name, nl);
        k_chacha20_xor(&c, buf, nl);
        k_sha256_add(&h.in, buf, nl);
        if (write(fd, buf, nl) != nl) r = -ENOSPC;
    }
    while (!r) {
        ssize_t n = read(in, buf, sizeof buf);
        if (n == 0) break;
        if (n < 0) { r = (int)n; break; }
        k_chacha20_xor(&c, buf, (size_t)n);
        k_sha256_add(&h.in, buf, (size_t)n);
        if (write(fd, buf, (size_t)n) != n) r = -ENOSPC;
    }
    uint8_t tag[32];
    hmac_end(&h, tag);
    if (!r && write(fd, tag, 32) != 32) r = -ENOSPC;
    close(in);
    close(fd);
    if (r) { remove(tmp); return r; }
    remove(out);
    r = rename(tmp, out);
    if (r) return r;
    /* the plain file goes: overwritten first, then deleted */
    int w = open(path, O_WRONLY);
    if (w >= 0) {
        memset(buf, 0, sizeof buf);
        for (uint64_t left = st.size; left;) {
            size_t k = left < sizeof buf ? (size_t)left : sizeof buf;
            if (write(w, buf, k) != (ssize_t)k) break;
            left -= k;
        }
        close(w);
    }
    remove(path);
    vault_index_update();
    sync();
    return 0;
}

int vault_unlock_file(const char *path) {
    if (!have_master) return -EACCES;
    if (!ends_with(path, LOCKED_EXT)) return -EINVAL;
    int fd = open(path, O_RDONLY);
    if (fd < 0) return fd;
    struct itx_stat st;
    fstat(fd, &st);
    uint8_t hdr[LHDR];
    if (st.size < LHDR + 32 || read(fd, hdr, LHDR) != LHDR || memcmp(hdr, LMAGIC, 8)) { close(fd); return -ECORRUPT; }
    uint16_t nl = (uint16_t)(hdr[28] | hdr[29] << 8);
    uint64_t size = 0;
    for (int i = 0; i < 8; i++) size |= (uint64_t)hdr[20 + i] << (8 * i);
    if (LHDR + (uint64_t)nl + size + 32 != st.size) { close(fd); return -ECORRUPT; }
    /* first the tag: nothing is written before the file is known to be whole */
    struct hmac h;
    hmac_begin(&h, master + 32);
    k_sha256_add(&h.in, hdr, LHDR);
    static uint8_t buf[65536];
    uint64_t body = (uint64_t)nl + size;
    while (body) {
        size_t k = body < sizeof buf ? (size_t)body : sizeof buf;
        if (read(fd, buf, k) != (ssize_t)k) { close(fd); return -EIO; }
        k_sha256_add(&h.in, buf, k);
        body -= k;
    }
    uint8_t tag[32], want[32];
    hmac_end(&h, tag);
    if (read(fd, want, 32) != 32) { close(fd); return -EIO; }
    uint8_t diff = 0;
    for (int i = 0; i < 32; i++) diff |= tag[i] ^ want[i];
    if (diff) { close(fd); return -EACCES; }   /* another vault's key, or the file was changed */
    lseek(fd, LHDR, SEEK_SET);
    struct k_chacha20 c;
    k_chacha20_init(&c, master, hdr + 8, 1);
    char name[256];
    if (nl >= sizeof name || read(fd, name, nl) != nl) { close(fd); return -ECORRUPT; }
    k_chacha20_xor(&c, name, nl);
    name[nl] = 0;
    char out[ITX_PATH_MAX], tmp[ITX_PATH_MAX], dir[ITX_PATH_MAX];
    strlcpy(dir, path, sizeof dir);
    char *s = strrchr(dir, '/');
    if (s) *s = 0;
    if (strchr(name, '/') || !name[0]) snprintf(name, sizeof name, "%.*s", (int)(strlen(vault_base(path)) - strlen(LOCKED_EXT)), vault_base(path));
    snprintf(out, sizeof out, "%s/%s", dir, name);
    snprintf(tmp, sizeof tmp, "%s.part", out);
    int o = open(tmp, O_WRONLY | O_CREATE | O_TRUNC);
    if (o < 0) { close(fd); return o; }
    int r = 0;
    for (uint64_t left = size; left && !r;) {
        size_t k = left < sizeof buf ? (size_t)left : sizeof buf;
        if (read(fd, buf, k) != (ssize_t)k) { r = -EIO; break; }
        k_chacha20_xor(&c, buf, k);
        if (write(o, buf, k) != (ssize_t)k) r = -ENOSPC;
        left -= k;
    }
    close(o);
    close(fd);
    if (r) { remove(tmp); return r; }
    struct itx_stat x;
    if (stat(out, &x) == 0) keep_version(out);
    r = rename(tmp, out);
    if (r) return r;
    remove(path);
    vault_index_update();
    sync();
    return 0;
}

/* -------------------------------------------------------------- backup */

struct bk_ctx { const char *from_root, *to_root; int copied, same; uint64_t bytes; void (*progress)(const char *); int err; };

static int bk_one(const char *path, const struct itx_dirent *de, void *vctx) {
    struct bk_ctx *c = vctx;
    if (ends_with(path, ".part")) return 0;
    char to[ITX_PATH_MAX], dir[ITX_PATH_MAX];
    snprintf(to, sizeof to, "%s%s", c->to_root, path + strlen(c->from_root));
    if (!strncmp(path + strlen(c->from_root), "/.trash", 7)) return 0;      /* the trash stays behind */
    struct itx_stat st;
    if (stat(to, &st) == 0 && same_file(path, to)) { c->same++; return 0; }
    strlcpy(dir, to, sizeof dir);
    char *s = strrchr(dir, '/');
    if (s) { *s = 0; k_mkdirs(dir); }
    if (c->progress) c->progress(path);
    int r = copy_file(path, to, &c->bytes);
    if (r) { c->err = r; return r; }
    c->copied++;
    return 0;
}

int vault_backup(const char *stick, int *copied, int *same, uint64_t *bytes, void (*progress)(const char *)) {
    char to[ITX_PATH_MAX];
    snprintf(to, sizeof to, "%s/vault-backup", stick);
    k_mkdirs(to);
    vault_index_update();
    struct bk_ctx c = { VAULT, to, 0, 0, 0, progress, 0 };
    walk(VAULT, true, bk_one, &c);
    sync();
    *copied = c.copied;
    *same = c.same;
    *bytes = c.bytes;
    return c.err;
}

int vault_restore(const char *stick, int *copied, int *same, void (*progress)(const char *)) {
    char from[ITX_PATH_MAX];
    snprintf(from, sizeof from, "%s/vault-backup", stick);
    struct itx_stat st;
    if (stat(from, &st) || st.type != VT_DIR) return -ENOENT;
    vault_init();
    struct bk_ctx c = { from, VAULT, 0, 0, 0, progress, 0 };
    walk(from, true, bk_one, &c);
    vault_index_update();
    sync();
    *copied = c.copied;
    *same = c.same;
    return c.err;
}
