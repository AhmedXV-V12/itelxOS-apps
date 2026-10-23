/* vec: the vector database of data_place (vec.h). */
#include "vec.h"

#define MAGIC "ITXVEC01"
enum { OP_ADD = 1, OP_DEL = 2 };

const char *vec_metric_name(int m) { return m == VM_L2 ? "l2" : m == VM_DOT ? "dot" : "cosine"; }

static float vsqrt(float x) {
    float r;
    __asm__("sqrtss %1, %0" : "=x"(r) : "x"(x));
    return r;
}

static void normalize(float *v, uint32_t dim) {
    float s = 0;
    for (uint32_t i = 0; i < dim; i++) s += v[i] * v[i];
    if (s <= 0) return;
    float inv = 1.0f / vsqrt(s);
    for (uint32_t i = 0; i < dim; i++) v[i] *= inv;
}

/* ------------------------------------------------------------ the file */

static void put32(struct kbuf *b, uint32_t v) { kb_put(b, &v, 4); }

static int write_header(int fd, struct vcoll *c) {
    struct kbuf b = { 0 };
    kb_put(&b, MAGIC, 8);
    put32(&b, c->dim);
    put32(&b, c->metric | (c->text ? 0x100u : 0));
    int r = write(fd, b.p, b.n) == (ssize_t)b.n ? 0 : -ENOSPC;
    kb_free(&b);
    return r;
}

static void rec_add(struct kbuf *b, struct vcoll *c, int slot) {
    uint8_t op = OP_ADD;
    kb_put(b, &op, 1);
    uint16_t il = (uint16_t)strlen(c->ids[slot]);
    kb_put(b, &il, 2);
    kb_put(b, c->ids[slot], il);
    kb_put(b, c->vecs + (size_t)slot * c->dim, sizeof(float) * c->dim);
    uint32_t ml = (uint32_t)strlen(c->meta[slot]);
    put32(b, ml);
    kb_put(b, c->meta[slot], ml);
    uint32_t tl = c->texts[slot] ? (uint32_t)strlen(c->texts[slot]) : 0;
    put32(b, tl);
    if (tl) kb_put(b, c->texts[slot], tl);
}

static void grow(struct vcoll *c) {
    if (c->n < c->cap) return;
    c->cap = c->cap ? c->cap * 2 : 256;
    c->ids = realloc(c->ids, sizeof(char *) * (size_t)c->cap);
    c->meta = realloc(c->meta, sizeof(char *) * (size_t)c->cap);
    c->texts = realloc(c->texts, sizeof(char *) * (size_t)c->cap);
    c->dead = realloc(c->dead, sizeof(bool) * (size_t)c->cap);
    c->vecs = realloc(c->vecs, sizeof(float) * c->dim * (size_t)c->cap);
    if (!c->ids || !c->meta || !c->texts || !c->dead || !c->vecs) { dprintf(2, "vec: out of memory\n"); exit(1); }
}

/* ids to slots: open addressing over slot numbers (+1; 0 is empty) */
static uint32_t id_hash(const char *s) {
    uint32_t h = 2166136261u;
    for (; *s; s++) h = (h ^ (unsigned char)*s) * 16777619u;
    return h;
}

static void index_put(struct vcoll *c, int slot) {
    if ((c->live + 1) * 2 > c->hcap) {
        int cap = 1024;
        while (cap < (c->live + 1) * 4) cap *= 2;
        free(c->hidx);
        c->hidx = calloc((size_t)cap, sizeof(int));
        c->hcap = cap;
        for (int i = 0; i < c->n; i++) if (!c->dead[i] && i != slot) index_put(c, i);
    }
    uint32_t k = id_hash(c->ids[slot]) & (uint32_t)(c->hcap - 1);
    while (c->hidx[k]) {
        int s = c->hidx[k] - 1;
        if (!strcmp(c->ids[s], c->ids[slot])) break;    /* the same id: the newer slot */
        k = (k + 1) & (uint32_t)(c->hcap - 1);
    }
    c->hidx[k] = slot + 1;
}

int vec_find(struct vcoll *c, const char *id) {
    if (!c->hcap) return -1;
    uint32_t k = id_hash(id) & (uint32_t)(c->hcap - 1);
    while (c->hidx[k]) {
        int s = c->hidx[k] - 1;
        if (!strcmp(c->ids[s], id)) return c->dead[s] ? -1 : s;
        k = (k + 1) & (uint32_t)(c->hcap - 1);
    }
    return -1;
}

static void mark_dead(struct vcoll *c, int slot) {
    c->dead[slot] = true;
    c->live--;
}

static int put_slot(struct vcoll *c, const char *id, const float *v, const char *meta, const char *text) {
    int old = vec_find(c, id);
    if (old >= 0) mark_dead(c, old);
    grow(c);
    int s = c->n++;
    c->ids[s] = strdup(id);
    memcpy(c->vecs + (size_t)s * c->dim, v, sizeof(float) * c->dim);
    if (c->metric == VM_COSINE) normalize(c->vecs + (size_t)s * c->dim, c->dim);
    c->meta[s] = strdup(meta ? meta : "");
    c->texts[s] = text && text[0] ? strdup(text) : NULL;
    c->dead[s] = false;
    index_put(c, s);
    c->live++;
    return s;
}

struct vcoll *vec_create(const char *path, const char *name, uint32_t dim, int metric, bool text, char *err, size_t elen) {
    struct itx_stat st;
    if (stat(path, &st) == 0) { snprintf(err, elen, "%s exists already", name); return NULL; }
    if (dim < 1 || dim > 8192) { snprintf(err, elen, "the dimension is 1 to 8192"); return NULL; }
    char dir[ITX_PATH_MAX];
    strlcpy(dir, path, sizeof dir);
    char *s = strrchr(dir, '/');
    if (s && s != dir) { *s = 0; k_mkdirs(dir); }
    struct vcoll *c = calloc(1, sizeof *c);
    strlcpy(c->path, path, sizeof c->path);
    strlcpy(c->name, name, sizeof c->name);
    c->dim = dim;
    c->metric = (uint32_t)metric;
    c->text = text;
    c->fd = open(path, O_WRONLY | O_CREATE | O_TRUNC);
    if (c->fd < 0 || write_header(c->fd, c)) { snprintf(err, elen, "%s: cannot write it", path); vec_close(c); return NULL; }
    close(c->fd);
    c->fd = open(path, O_WRONLY | O_APPEND);
    sync();
    return c;
}

struct vcoll *vec_open(const char *path, char *err, size_t elen) {
    size_t len;
    char *t = read_file(path, &len, 1u << 30);
    if (!t) { snprintf(err, elen, "no such collection (%s)", path); return NULL; }
    if (len < 16 || memcmp(t, MAGIC, 8)) { snprintf(err, elen, "%s is not a vector collection", path); free(t); return NULL; }
    struct vcoll *c = calloc(1, sizeof *c);
    strlcpy(c->path, path, sizeof c->path);
    const char *b = strrchr(path, '/') ? strrchr(path, '/') + 1 : path;
    snprintf(c->name, sizeof c->name, "%.*s", (int)(strchr(b, '.') ? strchr(b, '.') - b : (long)strlen(b)), b);
    memcpy(&c->dim, t + 8, 4);
    uint32_t m;
    memcpy(&m, t + 12, 4);
    c->metric = m & 0xFF;
    c->text = (m & 0x100) != 0;
    if (!c->dim || c->dim > 8192) { snprintf(err, elen, "%s: a bad header", path); free(t); free(c); return NULL; }
    size_t at = 16, rec = 0;
    float *v = malloc(sizeof(float) * c->dim);
    while (at < len) {
        uint8_t op = (uint8_t)t[at];
        uint16_t il;
        if (at + 3 > len) break;
        memcpy(&il, t + at + 1, 2);
        if (at + 3 + il > len) break;
        char id[1024];
        memcpy(id, t + at + 3, il < 1023 ? il : 1023);
        id[il < 1023 ? il : 1023] = 0;
        size_t p = at + 3 + il;
        if (op == OP_DEL) { int s = vec_find(c, id); if (s >= 0) mark_dead(c, s); at = p; rec++; continue; }
        if (op != OP_ADD || p + sizeof(float) * c->dim + 8 > len) break;
        memcpy(v, t + p, sizeof(float) * c->dim);
        p += sizeof(float) * c->dim;
        uint32_t ml, tl;
        memcpy(&ml, t + p, 4);
        if (p + 4 + ml + 4 > len) break;
        char *meta = malloc(ml + 1);
        memcpy(meta, t + p + 4, ml);
        meta[ml] = 0;
        p += 4 + ml;
        memcpy(&tl, t + p, 4);
        if (p + 4 + tl > len) { free(meta); break; }
        char *text = NULL;
        if (tl) { text = malloc(tl + 1); memcpy(text, t + p + 4, tl); text[tl] = 0; }
        p += 4 + tl;
        uint32_t keep = c->metric;
        c->metric = VM_DOT;                         /* stored vectors are normalized already */
        put_slot(c, id, v, meta, text);
        c->metric = keep;
        free(meta);
        free(text);
        at = p;
        rec++;
    }
    free(v);
    if (at < len) truncate(path, (long)at);         /* a record cut off by a crash */
    free(t);
    c->fd = open(path, O_WRONLY | O_APPEND);
    if (c->fd < 0) { snprintf(err, elen, "%s: cannot write it", path); vec_close(c); return NULL; }
    (void)rec;
    return c;
}

void vec_close(struct vcoll *c) {
    if (!c) return;
    if (c->fd >= 0) close(c->fd);
    for (int i = 0; i < c->n; i++) { free(c->ids[i]); free(c->meta[i]); free(c->texts[i]); }
    free(c->ids);
    free(c->meta);
    free(c->texts);
    free(c->dead);
    free(c->vecs);
    free(c->hidx);
    free(c);
}

int vec_add(struct vcoll *c, const char *id, const float *v, const char *meta, const char *text, char *err, size_t elen) {
    if (!id[0] || strlen(id) > 1000) { snprintf(err, elen, "an id is 1 to 1000 bytes"); return -1; }
    if (meta && meta[0]) {
        char e[120];
        struct jv *j = json_parse(meta, strlen(meta), NULL, e, sizeof e);
        if (!j || j->t != J_OBJ) { snprintf(err, elen, "the metadata is a JSON object: %s", j ? "{...}" : e); json_free(j); return -1; }
        json_free(j);
    }
    int s = put_slot(c, id, v, meta, text);
    struct kbuf b = { 0 };
    rec_add(&b, c, s);
    int r = write(c->fd, b.p, b.n) == (ssize_t)b.n ? 0 : -ENOSPC;
    kb_free(&b);
    if (r) { snprintf(err, elen, "the collection's file could not be written (disk full?)"); return -1; }
    return 0;
}

int vec_del(struct vcoll *c, const char *id) {
    int s = vec_find(c, id);
    if (s < 0) return -ENOENT;
    mark_dead(c, s);
    struct kbuf b = { 0 };
    uint8_t op = OP_DEL;
    uint16_t il = (uint16_t)strlen(id);
    kb_put(&b, &op, 1);
    kb_put(&b, &il, 2);
    kb_put(&b, id, il);
    write(c->fd, b.p, b.n);
    kb_free(&b);
    return 0;
}

int vec_compact(struct vcoll *c) {
    char tmp[ITX_PATH_MAX + 8];
    snprintf(tmp, sizeof tmp, "%s.new", c->path);
    int fd = open(tmp, O_WRONLY | O_CREATE | O_TRUNC);
    if (fd < 0) return fd;
    int r = write_header(fd, c);
    struct kbuf b = { 0 };
    for (int i = 0; i < c->n && !r; i++) {
        if (c->dead[i]) continue;
        rec_add(&b, c, i);
        if (b.n > (1 << 20)) { if (write(fd, b.p, b.n) != (ssize_t)b.n) r = -ENOSPC; b.n = 0; }
    }
    if (!r && b.n && write(fd, b.p, b.n) != (ssize_t)b.n) r = -ENOSPC;
    kb_free(&b);
    close(fd);
    if (r) { remove(tmp); return r; }
    sync();
    close(c->fd);
    remove(c->path);
    r = rename(tmp, c->path);
    c->fd = open(c->path, O_WRONLY | O_APPEND);
    sync();
    return r;
}

/* ------------------------------------------------------------- search */

static float score(struct vcoll *c, const float *a, const float *q) {
    float s = 0;
    if (c->metric == VM_L2) {
        for (uint32_t i = 0; i < c->dim; i++) { float d = a[i] - q[i]; s += d * d; }
        return -vsqrt(s);                         /* nearer is higher */
    }
    for (uint32_t i = 0; i < c->dim; i++) s += a[i] * q[i];
    return s;
}

static bool meta_matches(const char *meta, const struct jv *where) {
    if (!where || where->t != J_OBJ || !where->n) return true;
    char e[80];
    struct jv *m = meta[0] ? json_parse(meta, strlen(meta), NULL, e, sizeof e) : NULL;
    bool ok = m != NULL;
    for (int i = 0; ok && i < where->n; i++) {
        struct jv *v = json_get(m, where->keys[i]);
        if (!v) { ok = false; break; }
        struct kbuf a = { 0 }, b = { 0 };
        json_write(v, &a);
        json_write(where->items[i], &b);
        /* equal, or (for a list) holds it */
        bool eq = a.n == b.n && !memcmp(a.p, b.p, a.n);
        if (!eq && v->t == J_ARR)
            for (int k = 0; k < v->n && !eq; k++) {
                struct kbuf x = { 0 };
                json_write(v->items[k], &x);
                eq = x.n == b.n && !memcmp(x.p, b.p, x.n);
                kb_free(&x);
            }
        kb_free(&a);
        kb_free(&b);
        ok = eq;
    }
    json_free(m);
    return ok;
}

int vec_search(struct vcoll *c, const float *q0, int k, const struct jv *where, struct vhit *out) {
    if (k < 1) return 0;
    float *q = malloc(sizeof(float) * c->dim);
    memcpy(q, q0, sizeof(float) * c->dim);
    if (c->metric == VM_COSINE) normalize(q, c->dim);
    int n = 0;
    for (int i = 0; i < c->n; i++) {
        if (c->dead[i] || !meta_matches(c->meta[i], where)) continue;
        float s = score(c, c->vecs + (size_t)i * c->dim, q);
        if (n == k && s <= out[n - 1].score) continue;
        int j = n < k ? n++ : k - 1;
        while (j > 0 && out[j - 1].score < s) { out[j] = out[j - 1]; j--; }
        out[j].slot = i;
        out[j].score = s;
    }
    free(q);
    return n;
}

/* --------------------------------------------------------------- text */

/* the letters of a word, lower case; Arabic without its marks, with alef,
   ta marbuta and alef maqsura forms made one */
static size_t norm_word(const char **pp, char *out, size_t max) {
    const char *p = *pp;
    size_t o = 0;
    for (;;) {
        uint32_t cp;
        int n = utf8_decode(p, 4, &cp);
        if (n <= 0 || !*p) break;
        bool letter = (cp < 128 && isalnum((int)cp)) || (cp >= 0x0620 && cp <= 0x064A) || (cp >= 0x0660 && cp <= 0x0669) ||
                      (cp >= 0x00C0 && cp <= 0x024F) || (cp >= 0x0400 && cp <= 0x04FF);
        bool mark = (cp >= 0x064B && cp <= 0x065F) || cp == 0x0670 || cp == 0x0640;
        if (!letter && !mark) break;
        p += n;
        if (mark) continue;
        if (cp < 128) cp = (uint32_t)tolower((int)cp);
        if (cp == 0x0622 || cp == 0x0623 || cp == 0x0625) cp = 0x0627;   /* آ أ إ -> ا */
        if (cp == 0x0629) cp = 0x0647;                                 /* ة -> ه */
        if (cp == 0x0649) cp = 0x064A;                                 /* ى -> ي */
        char u[4];
        int k = utf8_encode(cp, u);
        if (o + (size_t)k < max) { memcpy(out + o, u, (size_t)k); o += (size_t)k; }
    }
    *pp = p;
    out[o] = 0;
    return o;
}

static uint32_t fnv(const char *s, uint32_t seed) {
    uint32_t h = 2166136261u ^ seed;
    for (; *s; s++) h = (h ^ (unsigned char)*s) * 16777619u;
    return h;
}

void vec_embed(const char *text, uint32_t dim, float *out) {
    memset(out, 0, sizeof(float) * dim);
    char prev[128] = "", w[128];
    for (const char *p = text; *p;) {
        size_t n = norm_word(&p, w, sizeof w);
        if (!n) {
            uint32_t cp;
            int k = utf8_decode(p, 4, &cp);
            p += k > 0 ? k : 1;
            continue;
        }
        /* the word, and the pair with the word before (half the weight) */
        uint32_t h = fnv(w, 0);
        out[h % dim] += (h >> 31) ? -1.0f : 1.0f;
        if (prev[0]) {
            char pair[260];
            snprintf(pair, sizeof pair, "%s %s", prev, w);
            uint32_t g = fnv(pair, 0x9E3779B9u);
            out[g % dim] += (g >> 31) ? -0.5f : 0.5f;
        }
        strlcpy(prev, w, sizeof prev);
    }
    normalize(out, dim);
}

int vec_parse_vector(const char *s, float *out, uint32_t dim) {
    uint32_t n = 0;
    while (*s == ' ' || *s == '[') s++;
    while (*s && n < dim) {
        char *e;
        double v = json_strtod(s, &e);
        if (e == s) break;
        out[n++] = (float)v;
        s = e;
        while (*s == ' ' || *s == ',' || *s == '\t') s++;
        if (*s == ']') break;
    }
    return (int)n;
}
