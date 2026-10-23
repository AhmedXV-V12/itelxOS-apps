/* dbserve: data_place's databases to the network, as an HTTP API with JSON.
 *
 *   dbserve [PORT] [TOKEN]      (8086 by default; without a token one is made)
 *
 * Every request carries the token: "Authorization: Bearer TOKEN" (or
 * ?token=TOKEN). Answers are JSON; errors {"error": "..."}.
 *
 *   GET  /                              the databases
 *   POST /sql/DB                        SQL in the body -> {"columns", "rows", "changes"}
 *   POST /kv/DB                         a command as a JSON array: ["SET", "k", "v"] -> {"result"}
 *   POST /doc/DB/COLL/find              {"filter", "sort", "fields", "limit", "skip"} -> {"documents"}
 *   POST /doc/DB/COLL/insert            a document, or a list of them -> {"inserted"}
 *   POST /doc/DB/COLL/update            {"filter", "update", "many"} -> {"updated"}
 *   POST /doc/DB/COLL/delete            {"filter", "many"} -> {"deleted"}
 *   POST /vec/COLL/search               {"vector": [...] | "text": "...", "k", "where"} -> {"results"}
 *   POST /vec/COLL/add                  {"id", "vector" | "text", "meta"}
 *
 * One request at a time, in turn: enough for a few clients, simple to
 * trust. Ctrl+C stops it. */
#include <libnet.h>
#include <sqlite3.h>
#include <itxsqlite.h>
#include <doc.h>
#include <vec.h>

static char token[64];

static bool name_ok(const char *n) {
    if (!n[0] || strlen(n) > 60) return false;
    for (const unsigned char *p = (const unsigned char *)n; *p; p++) if (!(isalnum(*p) || *p == '_' || *p == '-' || *p >= 0x80)) return false;
    return true;
}

static void reply(int c, int code, const struct kbuf *body) {
    char h[256];
    const char *what = code == 200 ? "OK" : code == 400 ? "Bad Request" : code == 401 ? "Unauthorized" : code == 404 ? "Not Found" : "Error";
    int n = snprintf(h, sizeof h, "HTTP/1.0 %d %s\r\nContent-Type: application/json; charset=utf-8\r\nContent-Length: %lu\r\nServer: data_place\r\n"
                     "Connection: close\r\n\r\n", code, what, (unsigned long)body->n);
    sock_send_all(c, h, (size_t)n);
    sock_send_all(c, body->p, body->n);
}

static void error(int c, int code, const char *msg) {
    struct kbuf b = { 0 };
    kb_str(&b, "{\"error\":");
    json_write_str(msg, strlen(msg), &b);
    kb_str(&b, "}\n");
    reply(c, code, &b);
    kb_free(&b);
}

/* ---------------------------------------------------------------- SQL */

static void do_sql(int c, const char *db, const char *body) {
    char path[ITX_PATH_MAX];
    snprintf(path, sizeof path, "/data/sql/%s.db", db);
    k_mkdirs("/data/sql");
    sqlite3 *s;
    if (sqlite3_open_v2(path, &s, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL)) { error(c, 500, "cannot open the database"); sqlite3_close(s); return; }
    sqlite3_busy_timeout(s, 5000);
    struct kbuf b = { 0 };
    const char *tail = body;
    bool any = false;
    while (tail && *tail) {
        sqlite3_stmt *st = NULL;
        if (sqlite3_prepare_v2(s, tail, -1, &st, &tail)) { error(c, 400, sqlite3_errmsg(s)); kb_free(&b); sqlite3_close(s); return; }
        if (!st) continue;
        int cols = sqlite3_column_count(st);
        b.n = 0;
        kb_str(&b, "{\"columns\":[");
        for (int i = 0; i < cols; i++) { if (i) kb_str(&b, ","); json_write_str(sqlite3_column_name(st, i), strlen(sqlite3_column_name(st, i)), &b); }
        kb_str(&b, "],\"rows\":[");
        int r, rows = 0;
        while ((r = sqlite3_step(st)) == SQLITE_ROW) {
            kb_str(&b, rows++ ? ",[" : "[");
            for (int i = 0; i < cols; i++) {
                if (i) kb_str(&b, ",");
                int t = sqlite3_column_type(st, i);
                const char *v = (const char *)sqlite3_column_text(st, i);
                if (t == SQLITE_NULL) kb_str(&b, "null");
                else if (t == SQLITE_INTEGER || t == SQLITE_FLOAT) kb_str(&b, v);
                else json_write_str(v ? v : "", v ? strlen(v) : 0, &b);
            }
            kb_str(&b, "]");
        }
        sqlite3_finalize(st);
        if (r != SQLITE_DONE) { error(c, 400, sqlite3_errmsg(s)); kb_free(&b); sqlite3_close(s); return; }
        kb_printf(&b, "],\"changes\":%d}\n", sqlite3_changes(s));
        any = true;
    }
    if (!any) kb_str(&b, "{\"columns\":[],\"rows\":[],\"changes\":0}\n");
    reply(c, 200, &b);
    kb_free(&b);
    sqlite3_close(s);
}

/* ----------------------------------------------------------------- kv */

static size_t resp_json(const char *p, size_t n, struct kbuf *o) {
    if (!n) return 0;
    const char *e = memchr(p, '\r', n);
    if (!e) return n;
    size_t hl = (size_t)(e - p), at = hl + 2;
    char head[64];
    snprintf(head, sizeof head, "%.*s", (int)(hl - 1 < 63 ? hl - 1 : 63), p + 1);
    switch (p[0]) {
    case '+': json_write_str(p + 1, hl - 1, o); return at;
    case '-': kb_str(o, "{\"error\":"); json_write_str(p + 1, hl - 1, o); kb_str(o, "}"); return at;
    case ':': kb_put(o, p + 1, hl - 1); return at;
    case '$': {
        long l = atol(head);
        if (l < 0) { kb_str(o, "null"); return at; }
        json_write_str(p + at, (size_t)l, o);
        return at + (size_t)l + 2;
    }
    case '*': {
        long cnt = atol(head);
        if (cnt < 0) { kb_str(o, "null"); return at; }
        kb_str(o, "[");
        for (long i = 0; i < cnt && at < n; i++) { if (i) kb_str(o, ","); at += resp_json(p + at, n - at, o); }
        kb_str(o, "]");
        return at;
    }
    }
    return n;
}

static void do_kv(int c, const char *db, const char *body) {
    char err[200], path[ITX_PATH_MAX];
    struct jv *cmd = json_parse(body, strlen(body), NULL, err, sizeof err);
    if (!cmd || cmd->t != J_ARR || !cmd->n) { json_free(cmd); error(c, 400, "the body is a command as a JSON array: [\"GET\", \"key\"]"); return; }
    int argc = cmd->n;
    char **argv = malloc(sizeof(char *) * (size_t)argc);
    size_t *lens = malloc(sizeof(size_t) * (size_t)argc);
    for (int i = 0; i < argc; i++) {
        struct jv *a = cmd->items[i];
        struct kbuf t = { 0 };
        if (a->t == J_STR) kb_put(&t, a->s, a->slen);
        else json_write(a, &t);
        if (!t.p) kb_put(&t, "", 0);
        argv[i] = t.p;
        lens[i] = t.n;
    }
    snprintf(path, sizeof path, "/data/kv/%s.aof", db);
    struct kvdb *k = kv_open(path, err, sizeof err);
    if (!k) { error(c, 500, err); resp_free(argc, argv, lens); json_free(cmd); return; }
    struct kbuf out = { 0 }, b = { 0 };
    kv_exec(k, argc, argv, lens, &out);
    kv_close(k);
    kb_str(&b, "{\"result\":");
    resp_json(out.p, out.n, &b);
    kb_str(&b, "}\n");
    reply(c, 200, &b);
    kb_free(&b);
    kb_free(&out);
    resp_free(argc, argv, lens);
    json_free(cmd);
}

/* --------------------------------------------------------------- doc */

static void add_doc(const char *json, void *vctx) {
    struct kbuf *b = vctx;
    if (b->p[b->n - 1] != '[') kb_str(b, ",");
    kb_str(b, json);
}

static void do_doc(int c, const char *db, const char *coll, const char *op, const char *body) {
    char err[256], path[ITX_PATH_MAX];
    struct jv *req = body[0] ? json_parse(body, strlen(body), NULL, err, sizeof err) : NULL;
    if (body[0] && !req) { error(c, 400, err); return; }
    snprintf(path, sizeof path, "/data/doc/%s.db", db);
    struct docdb *d = doc_open(path, err, sizeof err);
    if (!d) { json_free(req); error(c, 500, err); return; }
    struct kbuf b = { 0 };
    long n = -1;
    struct jv *many = json_get(req, "many");
    bool all = many && many->t == J_TRUE;
    if (!strcmp(op, "find")) {
        struct jv *lim = json_get(req, "limit"), *skip = json_get(req, "skip");
        kb_str(&b, "{\"documents\":[");
        n = doc_find(d, coll, json_get(req, "filter"), json_get(req, "sort"), json_get(req, "fields"), lim ? (long)lim->num : 0,
                     skip ? (long)skip->num : 0, add_doc, &b, err, sizeof err);
        kb_printf(&b, "],\"count\":%ld}\n", n);
    } else if (!strcmp(op, "insert")) {
        if (!req) { error(c, 400, "the body is a document or a list of documents"); doc_close(d); return; }
        kb_str(&b, "{\"inserted\":[");
        n = 0;
        sqlite3_exec(d->db, "BEGIN", NULL, NULL, NULL);
        int cnt = req->t == J_ARR ? req->n : 1;
        for (int i = 0; i < cnt; i++) {
            char id[64];
            if (doc_insert(d, coll, req->t == J_ARR ? req->items[i] : req, id, sizeof id, err, sizeof err)) { n = -1; break; }
            if (i) kb_str(&b, ",");
            json_write_str(id, strlen(id), &b);
            n++;
        }
        sqlite3_exec(d->db, n < 0 ? "ROLLBACK" : "COMMIT", NULL, NULL, NULL);
        kb_str(&b, "]}\n");
    } else if (!strcmp(op, "update")) {
        n = doc_update(d, coll, json_get(req, "filter"), json_get(req, "update"), all, err, sizeof err);
        kb_printf(&b, "{\"updated\":%ld}\n", n);
    } else if (!strcmp(op, "delete")) {
        if (!json_get(req, "filter")) { error(c, 400, "delete needs a filter ({} with many: true deletes all)"); doc_close(d); json_free(req); return; }
        n = doc_delete(d, coll, json_get(req, "filter"), all, err, sizeof err);
        kb_printf(&b, "{\"deleted\":%ld}\n", n);
    } else {
        snprintf(err, sizeof err, "unknown: %s (find, insert, update, delete)", op);
    }
    if (n < 0) error(c, 400, err);
    else reply(c, 200, &b);
    kb_free(&b);
    doc_close(d);
    json_free(req);
}

/* --------------------------------------------------------------- vec */

static void ftoa(char *out, size_t len, double v) {
    bool neg = v < 0;
    if (neg) v = -v;
    long whole = (long)v, frac = (long)((v - (double)whole) * 1000000.0 + 0.5);
    if (frac >= 1000000) { whole++; frac -= 1000000; }
    snprintf(out, len, "%s%ld.%06ld", neg ? "-" : "", whole, frac);
}

static void do_vec(int c, const char *coll, const char *op, const char *body) {
    char err[200], path[ITX_PATH_MAX];
    struct jv *req = json_parse(body, strlen(body), NULL, err, sizeof err);
    if (!req || req->t != J_OBJ) { json_free(req); error(c, 400, "the body is a JSON object"); return; }
    snprintf(path, sizeof path, "/data/vec/%s.vec", coll);
    struct vcoll *v = vec_open(path, err, sizeof err);
    if (!v) { json_free(req); error(c, 404, err); return; }
    float *q = calloc(v->dim, sizeof(float));
    struct jv *vec = json_get(req, "vector"), *text = json_get(req, "text");
    bool have = false;
    if (vec && vec->t == J_ARR && (uint32_t)vec->n == v->dim) { for (uint32_t i = 0; i < v->dim; i++) q[i] = (float)vec->items[i]->num; have = true; }
    else if (text && text->t == J_STR) { vec_embed(text->s, v->dim, q); have = true; }
    struct kbuf b = { 0 };
    if (!have) { snprintf(err, sizeof err, "give \"vector\" (%u numbers) or \"text\"", v->dim); error(c, 400, err); }
    else if (!strcmp(op, "search")) {
        struct jv *kk = json_get(req, "k");
        int k = kk ? (int)kk->num : 5;
        if (k < 1) k = 1;
        if (k > 1000) k = 1000;
        struct vhit *h = malloc(sizeof *h * (size_t)k);
        int n = vec_search(v, q, k, json_get(req, "where"), h);
        kb_str(&b, "{\"results\":[");
        for (int i = 0; i < n; i++) {
            int s = h[i].slot;
            char sc[32];
            ftoa(sc, sizeof sc, v->metric == VM_L2 ? -h[i].score : h[i].score);
            kb_str(&b, i ? ",{\"id\":" : "{\"id\":");
            json_write_str(v->ids[s], strlen(v->ids[s]), &b);
            kb_printf(&b, ",\"%s\":%s", v->metric == VM_L2 ? "distance" : "score", sc);
            if (v->meta[s][0]) { kb_str(&b, ",\"meta\":"); kb_str(&b, v->meta[s]); }
            if (v->texts[s]) { kb_str(&b, ",\"text\":"); json_write_str(v->texts[s], strlen(v->texts[s]), &b); }
            kb_str(&b, "}");
        }
        kb_str(&b, "]}\n");
        free(h);
        reply(c, 200, &b);
    } else if (!strcmp(op, "add")) {
        struct jv *id = json_get(req, "id"), *meta = json_get(req, "meta");
        struct kbuf m = { 0 };
        if (meta && meta->t == J_OBJ) json_write(meta, &m);
        if (!id || id->t != J_STR) error(c, 400, "\"id\" (a string) is needed");
        else if (vec_add(v, id->s, q, m.p, text && text->t == J_STR ? text->s : NULL, err, sizeof err)) error(c, 400, err);
        else { sync(); kb_str(&b, "{\"added\":"); json_write_str(id->s, id->slen, &b); kb_str(&b, "}\n"); reply(c, 200, &b); }
        kb_free(&m);
    } else {
        error(c, 404, "unknown: search or add");
    }
    kb_free(&b);
    free(q);
    vec_close(v);
    json_free(req);
}

/* ------------------------------------------------------------ the list */

static void list_into(struct kbuf *b, const char *key, const char *dir, const char *ext) {
    kb_printf(b, "\"%s\":[", key);
    int fd = open(dir, O_RDONLY), n = 0;
    struct itx_dirent de;
    while (fd >= 0 && readdir(fd, &de) == 1) {
        if (!ends_with(de.name, ext)) continue;
        if (n++) kb_str(b, ",");
        json_write_str(de.name, strlen(de.name) - strlen(ext), b);
    }
    if (fd >= 0) close(fd);
    kb_str(b, "]");
}

static void do_root(int c) {
    struct kbuf b = { 0 };
    kb_str(&b, "{\"server\":\"data_place dbserve 1.0\",");
    list_into(&b, "sql", "/data/sql", ".db");
    kb_str(&b, ",");
    list_into(&b, "doc", "/data/doc", ".db");
    kb_str(&b, ",");
    list_into(&b, "kv", "/data/kv", ".aof");
    kb_str(&b, ",");
    list_into(&b, "vec", "/data/vec", ".vec");
    kb_str(&b, "}\n");
    reply(c, 200, &b);
    kb_free(&b);
}

/* ----------------------------------------------------------- requests */

static void serve(int c, const struct itx_sockaddr *peer) {
    struct kbuf in = { 0 };
    char *end = NULL;
    long clen = 0;
    while (!end || (long)(in.n - (size_t)(end + 4 - in.p)) < clen) {
        char buf[16384];
        long k = sock_recv(c, buf, sizeof buf, 15000);
        if (k <= 0) { kb_free(&in); return; }
        kb_put(&in, buf, (size_t)k);
        if (in.n > (64u << 20)) { error(c, 400, "the request is too big"); kb_free(&in); return; }
        if (!end && (end = strstr(in.p, "\r\n\r\n"))) {
            const char *cl = k_strcasestr(in.p, "\r\nContent-Length:");
            if (cl && cl < end) clen = atol(cl + 17);
        }
    }
    *end = 0;
    char *body = end + 4;
    body[clen] = 0;
    char method[8] = "", url[1024] = "";
    char *sp = strchr(in.p, ' '), *sp2 = sp ? strchr(sp + 1, ' ') : NULL;
    if (!sp || !sp2 || sp - in.p >= 8 || sp2 - sp - 1 >= (long)sizeof url) { error(c, 400, "bad request"); kb_free(&in); return; }
    memcpy(method, in.p, (size_t)(sp - in.p));
    memcpy(url, sp + 1, (size_t)(sp2 - sp - 1));
    /* the token */
    char *q = strchr(url, '?');
    bool ok = false;
    if (q) { *q++ = 0; char *t = strstr(q, "token="); if (t && !strncmp(t + 6, token, strlen(token)) && (!t[6 + strlen(token)] || t[6 + strlen(token)] == '&')) ok = true; }
    const char *auth = k_strcasestr(in.p, "\r\nAuthorization: Bearer ");
    if (auth) { auth += 24; ok = ok || (!strncmp(auth, token, strlen(token)) && (auth[strlen(token)] == '\r' || !auth[strlen(token)])); }
    char ip[20];
    ip_str(ip, sizeof ip, peer->ip);
    printf("%s %s %s%s\n", ip, method, url, ok ? "" : "  (no token: refused)");
    flush();
    if (!ok) { error(c, 401, "a token is needed: Authorization: Bearer TOKEN"); kb_free(&in); return; }
    /* the parts of the path */
    char *part[6];
    int np = 0;
    for (char *p = url + 1; *p && np < 6;) {
        part[np++] = p;
        char *s = strchr(p, '/');
        if (!s) break;
        *s = 0;
        p = s + 1;
    }
    if (np >= 1 && !part[np - 1][0]) np--;
    bool post = !strcmp(method, "POST") || !strcmp(method, "PUT");
    for (int i = 0; i < np; i++) if (!name_ok(part[i])) { error(c, 400, "names are letters, digits, _ and -"); kb_free(&in); return; }
    if (np == 0) do_root(c);
    else if (!strcmp(part[0], "sql") && np == 2 && post) do_sql(c, part[1], body);
    else if (!strcmp(part[0], "kv") && np == 2 && post) do_kv(c, part[1], body);
    else if (!strcmp(part[0], "doc") && np == 4 && post) do_doc(c, part[1], part[2], part[3], body);
    else if (!strcmp(part[0], "vec") && np == 3 && post) do_vec(c, part[1], part[2], body);
    else error(c, 404, "unknown: see GET / and the list in dbserve's help");
    kb_free(&in);
}

int main(int argc, char **argv) {
    itx_sqlite_init(96);
    int port = argc > 1 ? atoi(argv[1]) : 8086;
    if (argc > 2) strlcpy(token, argv[2], sizeof token);
    else {
        uint8_t r[12];
        k_random(r, sizeof r);
        k_hex(token, r, sizeof r);
    }
    int s = sock_open(SOCK_TCP);
    if (s < 0) { printf("dbserve: %s\n", net_error(s)); return 1; }
    int r = sock_bind(s, port);
    if (r < 0) { printf("dbserve: port %d: %s\n", port, net_error(r)); return 1; }
    sock_listen(s, 8);
    if (!net_ready(8000)) printf("(no address yet: see net)\n");
    struct itx_netinfo ni[4];
    int n = net_info(ni, 4);
    char ip[20] = "?";
    for (int i = 0; i < n; i++) if (ni[i].flags & NETF_UP) { ip_str(ip, sizeof ip, ni[i].ip); break; }
    printf("data_place API on http://%s:%d/\ntoken: %s\n"
           "example:  curl -H \"Authorization: Bearer %s\" -d \"SELECT 1\" http://%s:%d/sql/main\n"
           "Ctrl+C stops it.\n", ip, port, token, token, ip, port);
    flush();
    for (;;) {
        struct itx_sockaddr peer;
        int c = sock_accept(s, &peer, 60000);
        if (c == -EAGAIN) continue;
        if (c < 0) { sleep_ms(200); continue; }
        serve(c, &peer);
        sock_close(c);
    }
}
