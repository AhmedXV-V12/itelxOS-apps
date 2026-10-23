/* vec: the vector databases of data_place (common/vec/vec.h).
 *
 *   vec                                      the shell
 *   vec COMMAND ...                          one command
 *
 *   list
 *   create NAME DIM [cosine|l2|dot] [text]   (text: it turns text into vectors itself)
 *   add NAME ID v1,v2,... [META]             add-text NAME ID "text" [META]
 *   get NAME ID     del NAME ID     info NAME     drop NAME     compact NAME
 *   search NAME v1,v2,... [k N] [where {META}]
 *   search-text NAME "words" [k N] [where {META}]
 *   import NAME FILE   (.csv: id,v1,...,vn   .jsonl: {"id":..,"vector":[..],"meta":{..}}
 *                       or, for text collections, {"id":..,"text":"..","meta":{..}})
 *   export NAME FILE   (JSON lines)
 */
#include <vec.h>

static void path_of(const char *name, char *out, size_t len) { snprintf(out, len, "/data/vec/%s.vec", name); }

static bool valid(const char *n) {
    if (!n[0] || strlen(n) > 60) return false;
    for (const unsigned char *p = (const unsigned char *)n; *p; p++) if (!(isalnum(*p) || *p == '_' || *p == '-' || *p >= 0x80)) return false;
    return true;
}

static struct vcoll *open_named(const char *name) {
    char path[ITX_PATH_MAX], err[200];
    if (!valid(name)) { printf("not a collection name: %s\n", name); return NULL; }
    path_of(name, path, sizeof path);
    struct vcoll *c = vec_open(path, err, sizeof err);
    if (!c) printf("%s\n", err);
    return c;
}

/* the words of a line: "quoted" ones, {json} and [json] stay whole */
static int words(char *line, char **w, int max) {
    int n = 0;
    char *p = line;
    while (*p && n < max) {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
        if (!*p) break;
        if (*p == '"') {
            w[n++] = ++p;
            while (*p && *p != '"') p++;
            if (*p) *p++ = 0;
        } else if (*p == '{' || *p == '[') {
            char open = *p, close = open == '{' ? '}' : ']';
            int depth = 0;
            bool str = false;
            w[n++] = p;
            for (; *p; p++) {
                if (str) { if (*p == '\\' && p[1]) p++; else if (*p == '"') str = false; continue; }
                if (*p == '"') str = true;
                else if (*p == open) depth++;
                else if (*p == close && --depth == 0) { p++; break; }
            }
            if (*p) *p++ = 0;
        } else {
            w[n++] = p;
            while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') p++;
            if (*p) *p++ = 0;
        }
    }
    return n;
}

/* "%.4f" is not in libitx's printf: a float as text, by hand */
static void ftoa(char *out, size_t len, double v) {
    if (v != v) { snprintf(out, len, "nan"); return; }
    bool neg = v < 0;
    if (neg) v = -v;
    long whole = (long)v;
    long frac = (long)((v - (double)whole) * 1000000.0 + 0.5);
    if (frac >= 1000000) { whole++; frac -= 1000000; }
    snprintf(out, len, "%s%ld.%06ld", neg ? "-" : "", whole, frac);
}

static void search(struct vcoll *c, const float *q, int k, const char *where) {
    struct jv *w = NULL;
    if (where) {
        char err[120];
        w = json_parse(where, strlen(where), NULL, err, sizeof err);
        if (!w) { printf("where: %s\n", err); return; }
    }
    struct vhit *h = malloc(sizeof *h * (size_t)k);
    long t0 = uptime_ms();
    int n = vec_search(c, q, k, w, h);
    long ms = uptime_ms() - t0;
    for (int i = 0; i < n; i++) {
        int s = h[i].slot;
        char sc[32];
        ftoa(sc, sizeof sc, c->metric == VM_L2 ? -h[i].score : h[i].score);
        printf("%2d. %-24s %s %s%s%s\n", i + 1, c->ids[s], c->metric == VM_L2 ? "distance" : "score", sc, c->meta[s][0] ? "  " : "", c->meta[s]);
        if (c->texts[s]) {
            char t[200];
            k_fit(t, sizeof t, c->texts[s], 72);
            printf("    %s\n", t);
        }
    }
    printf("%d of %ld compared in %ld ms\n", n, c->live, ms);
    free(h);
    json_free(w);
}

static void import_file(struct vcoll *c, const char *file) {
    size_t len;
    char *t = read_file(file, &len, 512u << 20);
    if (!t) { printf("%s: cannot read it\n", file); return; }
    float *v = malloc(sizeof(float) * c->dim);
    long n = 0, bad = 0;
    char err[200];
    bool jsonl = ends_with(file, ".jsonl") || ends_with(file, ".json") || t[0] == '{';
    for (char *line = t; *line;) {
        char *e = strchr(line, '\n');
        if (e) *e = 0;
        size_t l = strlen(line);
        if (l && line[l - 1] == '\r') line[--l] = 0;
        if (l && jsonl) {
            struct jv *j = json_parse(line, l, NULL, err, sizeof err);
            struct jv *id = json_get(j, "id"), *vec = json_get(j, "vector"), *meta = json_get(j, "meta"), *text = json_get(j, "text");
            char ids[256] = "";
            if (id && id->t == J_STR) snprintf(ids, sizeof ids, "%.*s", (int)id->slen, id->s);
            else if (id && id->t == J_NUM) snprintf(ids, sizeof ids, "%s", id->s);
            struct kbuf m = { 0 };
            if (meta && meta->t == J_OBJ) json_write(meta, &m);
            bool ok = ids[0];
            if (ok && vec && vec->t == J_ARR && (uint32_t)vec->n == c->dim) {
                for (uint32_t i = 0; i < c->dim; i++) v[i] = (float)vec->items[i]->num;
            } else if (ok && text && text->t == J_STR && c->text) {
                vec_embed(text->s, c->dim, v);
            } else ok = false;
            if (ok && !vec_add(c, ids, v, m.p, text && text->t == J_STR ? text->s : NULL, err, sizeof err)) n++;
            else bad++;
            kb_free(&m);
            json_free(j);
        } else if (l) {
            /* CSV: id,v1,...,vn (a first line with a name for the id is skipped) */
            char *comma = strchr(line, ',');
            if (comma) {
                *comma = 0;
                if ((uint32_t)vec_parse_vector(comma + 1, v, c->dim) == c->dim && !vec_add(c, line, v, NULL, NULL, err, sizeof err)) n++;
                else if (n || bad) bad++;
            }
        }
        if (!e) break;
        line = e + 1;
    }
    free(v);
    free(t);
    sync();
    printf("%ld vector(s) imported into %s%s\n", n, c->name, bad ? " (some lines were not right for this collection)" : "");
}

static void export_file(struct vcoll *c, const char *file) {
    int fd = open(file, O_WRONLY | O_CREATE | O_TRUNC);
    if (fd < 0) { printf("%s: %s\n", file, strerror(fd)); return; }
    long n = 0;
    struct kbuf b = { 0 };
    for (int i = 0; i < c->n; i++) {
        if (c->dead[i]) continue;
        b.n = 0;
        kb_str(&b, "{\"id\":");
        json_write_str(c->ids[i], strlen(c->ids[i]), &b);
        kb_str(&b, ",\"vector\":[");
        for (uint32_t k = 0; k < c->dim; k++) {
            char f[32];
            ftoa(f, sizeof f, c->vecs[(size_t)i * c->dim + k]);
            if (k) kb_str(&b, ",");
            kb_str(&b, f);
        }
        kb_str(&b, "]");
        if (c->meta[i][0]) { kb_str(&b, ",\"meta\":"); kb_str(&b, c->meta[i]); }
        if (c->texts[i]) { kb_str(&b, ",\"text\":"); json_write_str(c->texts[i], strlen(c->texts[i]), &b); }
        kb_str(&b, "}\n");
        write(fd, b.p, b.n);
        n++;
    }
    kb_free(&b);
    close(fd);
    printf("%ld vector(s) written to %s\n", n, file);
}

static void help(void) {
    printf("list\n"
           "create NAME DIM [cosine|l2|dot] [text]  a collection; text: it makes vectors from text itself\n"
           "add NAME ID v1,v2,... [{meta}]          add-text NAME ID \"text\" [{meta}]\n"
           "get NAME ID   del NAME ID   info NAME   drop NAME   compact NAME\n"
           "search NAME v1,v2,... [k N] [where {meta}]\n"
           "search-text NAME \"words\" [k N] [where {meta}]\n"
           "import NAME FILE   .csv (id,v1,...,vn) or .jsonl ({\"id\",\"vector\",\"meta\"} or {\"id\",\"text\",\"meta\"})\n"
           "export NAME FILE   JSON lines\n"
           "Search compares every vector (exact). Text collections hash words into the dimensions:\n"
           "they find texts that share words; for meaning, import vectors from an embedding model.\n");
}

static int run(char *line) {
    char *w[16];
    int n = words(line, w, 16);
    if (!n) return 0;
    const char *c = w[0];
    if (!strcmp(c, "quit") || !strcmp(c, "exit")) return 1;
    if (!strcmp(c, "help")) { help(); return 0; }
    if (!strcmp(c, "list")) {
        int fd = open("/data/vec", O_RDONLY);
        struct itx_dirent de;
        int k = 0;
        while (fd >= 0 && readdir(fd, &de) == 1) {
            if (!ends_with(de.name, ".vec")) continue;
            char p[ITX_PATH_MAX], err[100];
            snprintf(p, sizeof p, "/data/vec/%s", de.name);
            struct vcoll *v = vec_open(p, err, sizeof err);
            if (!v) continue;
            printf("  %-24s %6u dims  %-6s %s%ld vector(s)\n", v->name, v->dim, vec_metric_name((int)v->metric), v->text ? "text  " : "", v->live);
            vec_close(v);
            k++;
        }
        if (fd >= 0) close(fd);
        if (!k) printf("no collections yet (create NAME DIM)\n");
        return 0;
    }
    if (n < 2) { printf("%s needs a collection name (help)\n", c); return 0; }
    if (!strcmp(c, "create")) {
        if (n < 3) { printf("use: create NAME DIM [cosine|l2|dot] [text]\n"); return 0; }
        int metric = VM_COSINE;
        bool text = false;
        for (int i = 3; i < n; i++) {
            if (!strcmp(w[i], "l2")) metric = VM_L2;
            else if (!strcmp(w[i], "dot")) metric = VM_DOT;
            else if (!strcmp(w[i], "cosine")) metric = VM_COSINE;
            else if (!strcmp(w[i], "text")) text = true;
        }
        char path[ITX_PATH_MAX], err[200];
        if (!valid(w[1])) { printf("not a collection name: %s\n", w[1]); return 0; }
        path_of(w[1], path, sizeof path);
        struct vcoll *v = vec_create(path, w[1], (uint32_t)atoi(w[2]), metric, text, err, sizeof err);
        if (!v) printf("%s\n", err);
        else printf("collection %s: %u dimensions, %s%s\n", w[1], v->dim, vec_metric_name(metric), text ? ", vectors made from text" : "");
        vec_close(v);
        return 0;
    }
    if (!strcmp(c, "drop")) {
        char path[ITX_PATH_MAX];
        if (!valid(w[1])) return 0;
        path_of(w[1], path, sizeof path);
        printf(remove(path) ? "no such collection: %s\n" : "%s is dropped\n", w[1]);
        return 0;
    }
    struct vcoll *v = open_named(w[1]);
    if (!v) return 0;
    char err[200];
    int k = 5;
    const char *where = NULL;
    for (int i = 2; i + 1 < n; i++) {
        if (!strcmp(w[i], "k")) k = atoi(w[i + 1]);
        else if (!strcmp(w[i], "where")) where = w[i + 1];
    }
    if (k < 1) k = 1;
    if (k > 1000) k = 1000;
    float *q = malloc(sizeof(float) * v->dim);
    if (!strcmp(c, "info")) {
        printf("%s: %u dimensions, %s, %ld vector(s)%s; %s\n", v->name, v->dim, vec_metric_name((int)v->metric), v->live,
               v->text ? ", made from text" : "", v->path);
    } else if (!strcmp(c, "add") || !strcmp(c, "add-text")) {
        if (n < 4) printf("use: %s NAME ID %s [{meta}]\n", c, c[3] ? "\"text\"" : "v1,v2,...");
        else {
            bool text = c[3] == '-';
            if (text) vec_embed(w[3], v->dim, q);
            int got = text ? (int)v->dim : vec_parse_vector(w[3], q, v->dim);
            if ((uint32_t)got != v->dim) printf("%s has %u dimensions; %d numbers given\n", v->name, v->dim, got);
            else if (vec_add(v, w[2], q, n > 4 && w[4][0] == '{' ? w[4] : NULL, text ? w[3] : NULL, err, sizeof err)) printf("%s\n", err);
            else { sync(); printf("added %s (%ld in %s)\n", w[2], v->live, v->name); }
        }
    } else if (!strcmp(c, "get")) {
        int s = n > 2 ? vec_find(v, w[2]) : -1;
        if (s < 0) printf("no %s in %s\n", n > 2 ? w[2] : "id", v->name);
        else {
            printf("%s  [", v->ids[s]);
            for (uint32_t i = 0; i < v->dim && i < 16; i++) { char f[32]; ftoa(f, sizeof f, v->vecs[(size_t)s * v->dim + i]); printf("%s%s", i ? ", " : "", f); }
            printf("%s]  %s\n", v->dim > 16 ? ", ..." : "", v->meta[s]);
            if (v->texts[s]) printf("%s\n", v->texts[s]);
        }
    } else if (!strcmp(c, "del")) {
        if (n < 3 || vec_del(v, w[2])) printf("no %s in %s\n", n > 2 ? w[2] : "id", v->name);
        else { sync(); printf("deleted %s\n", w[2]); }
    } else if (!strcmp(c, "search")) {
        if (n < 3 || (uint32_t)vec_parse_vector(w[2], q, v->dim) != v->dim) printf("use: search NAME v1,...,v%u [k N] [where {..}]\n", v->dim);
        else search(v, q, k, where);
    } else if (!strcmp(c, "search-text")) {
        if (n < 3) printf("use: search-text NAME \"words\" [k N]\n");
        else if (!v->text) printf("%s is not a text collection: its vectors come from elsewhere\n", v->name);
        else { vec_embed(w[2], v->dim, q); search(v, q, k, where); }
    } else if (!strcmp(c, "import")) {
        if (n < 3) printf("use: import NAME FILE\n"); else import_file(v, w[2]);
    } else if (!strcmp(c, "export")) {
        if (n < 3) printf("use: export NAME FILE\n"); else export_file(v, w[2]);
    } else if (!strcmp(c, "compact")) {
        int r = vec_compact(v);
        printf(r ? "could not compact: %s\n" : "%s is written again with only what is alive\n", r ? strerror(r) : v->name);
    } else {
        printf("unknown command: %s (help)\n", c);
    }
    free(q);
    vec_close(v);
    return 0;
}

int main(int argc, char **argv) {
    k_mkdirs("/data/vec");
    if (argc > 1) {
        struct kbuf l = { 0 };
        for (int i = 1; i < argc; i++) {
            if (i > 1) kb_str(&l, " ");
            /* an argument with spaces was quoted on the command line: quoted again */
            bool sp = strchr(argv[i], ' ') && argv[i][0] != '{' && argv[i][0] != '[';
            if (sp) kb_str(&l, "\"");
            kb_str(&l, argv[i]);
            if (sp) kb_str(&l, "\"");
        }
        run(l.p);
        kb_free(&l);
        return 0;
    }
    printf("vec  -  vector collections for AI (/data/vec)  -  help for the commands, quit to leave\n");
    for (;;) {
        printf("vec> ");
        flush();
        char line[65536];
        ssize_t n = read(0, line, sizeof line - 1);
        if (n <= 0) break;
        line[n] = 0;
        if (run(line)) break;
    }
    return 0;
}
