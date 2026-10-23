/* doc: documents in SQLite, queried the way MongoDB is (doc.h). */
#include "doc.h"

/* ------------------------------------------------------- the SQL text */

enum { B_TEXT, B_INT, B_REAL, B_NULL };
struct bind { int type; char *s; long i; double d; };

struct q {
    struct kbuf sql;
    struct bind b[512];
    int nb;
    char err[160];
};

static void qerr(struct q *q, const char *fmt, const char *arg) {
    if (!q->err[0]) snprintf(q->err, sizeof q->err, fmt, arg);
}

/* a value bound as ?N; returns N */
static int bind_text(struct q *q, const char *s, size_t n) {
    if (q->nb >= 512) { qerr(q, "%s", "the query is too long"); return 1; }
    struct bind *b = &q->b[q->nb++];
    b->type = B_TEXT;
    b->s = malloc(n + 1);
    memcpy(b->s, s, n);
    b->s[n] = 0;
    return q->nb;
}

static int bind_num(struct q *q, const struct jv *v) {
    if (q->nb >= 512) { qerr(q, "%s", "the query is too long"); return 1; }
    struct bind *b = &q->b[q->nb++];
    bool whole = !strchr(v->s, '.') && !strchr(v->s, 'e') && !strchr(v->s, 'E') && v->slen < 18;
    if (whole) { b->type = B_INT; b->i = strtol(v->s, NULL, 10); }
    else { b->type = B_REAL; b->d = v->num; }
    return q->nb;
}

static int bind_int(struct q *q, long v) {
    if (q->nb >= 512) { qerr(q, "%s", "the query is too long"); return 1; }
    struct bind *b = &q->b[q->nb++];
    b->type = B_INT;
    b->i = v;
    return q->nb;
}

/* a value's JSON text, bound */
static int bind_json(struct q *q, const struct jv *v) {
    struct kbuf t = { 0 };
    json_write(v, &t);
    int n = bind_text(q, t.p, t.n);
    kb_free(&t);
    return n;
}

static void q_free(struct q *q) {
    for (int i = 0; i < q->nb; i++) free(q->b[i].s);
    kb_free(&q->sql);
}

static int q_prepare(struct docdb *d, struct q *q, sqlite3_stmt **st, char *err, size_t elen) {
    if (q->err[0]) { snprintf(err, elen, "%s", q->err); return -1; }
    if (sqlite3_prepare_v2(d->db, q->sql.p, -1, st, NULL)) { snprintf(err, elen, "%s", sqlite3_errmsg(d->db)); return -1; }
    for (int i = 0; i < q->nb; i++) {
        struct bind *b = &q->b[i];
        if (b->type == B_TEXT) sqlite3_bind_text(*st, i + 1, b->s, -1, SQLITE_TRANSIENT);
        else if (b->type == B_INT) sqlite3_bind_int64(*st, i + 1, b->i);
        else if (b->type == B_REAL) sqlite3_bind_double(*st, i + 1, b->d);
        else sqlite3_bind_null(*st, i + 1);
    }
    return 0;
}

/* "a.b.0" -> '$."a"."b"[0]' (a SQL string; names with quotes or
   backslashes are refused) */
static bool path(struct q *q, const char *field, struct kbuf *out) {
    if (!field[0]) { qerr(q, "%s", "an empty field name"); return false; }
    for (const char *p = field; *p; p++)
        if (*p == '"' || *p == '\\' || *p == '\'' || (unsigned char)*p < 0x20) { qerr(q, "a field name with quotes: %s", field); return false; }
    kb_str(out, "'$");
    for (const char *p = field; *p;) {
        const char *e = strchr(p, '.');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        bool idx = n > 0;
        for (size_t i = 0; i < n; i++) if (!isdigit((unsigned char)p[i])) idx = false;
        if (idx && p != field) { kb_str(out, "["); kb_put(out, p, n); kb_str(out, "]"); }
        else { kb_str(out, ".\""); kb_put(out, p, n); kb_str(out, "\""); }
        p += n;
        if (*p == '.') p++;
    }
    kb_str(out, "'");
    return true;
}

static bool is_ops(const struct jv *v) { return v && v->t == J_OBJ && v->n > 0 && v->keys[0][0] == '$'; }

/* a scalar in a filter, bound: the number of its parameter (0 for null) */
static int bind_scalar(struct q *q, const struct jv *v) {
    switch (v->t) {
    case J_STR: return bind_text(q, v->s, v->slen);
    case J_NUM: return bind_num(q, v);
    case J_TRUE: return bind_int(q, 1);
    case J_FALSE: return bind_int(q, 0);
    default: return 0;
    }
}

static void eq(struct q *q, const char *P, const struct jv *v) {
    struct kbuf *s = &q->sql;
    if (v->t == J_NULL) { kb_printf(s, "(json_type(doc, %s) IS NULL OR json_type(doc, %s) = 'null')", P, P); return; }
    if (v->t == J_ARR || v->t == J_OBJ) { int n = bind_json(q, v); kb_printf(s, "(doc -> %s = json(?%d))", P, n); return; }
    /* a value, or an array that holds it */
    int n = bind_scalar(q, v);
    kb_printf(s, "(json_extract(doc, %s) = ?%d OR (json_type(doc, %s) = 'array' AND EXISTS (SELECT 1 FROM json_each(doc, %s) WHERE json_each.value = ?%d)))",
              P, n, P, P, n);
}

static void cond(struct q *q, const struct jv *filter);

static void field_cond(struct q *q, const char *field, const struct jv *v) {
    struct kbuf pb = { 0 };
    if (!path(q, field, &pb)) { kb_free(&pb); kb_str(&q->sql, "0"); return; }
    const char *P = pb.p;
    struct kbuf *s = &q->sql;
    if (!is_ops(v)) { eq(q, P, v); kb_free(&pb); return; }
    kb_str(s, "(");
    for (int i = 0; i < v->n; i++) {
        const char *op = v->keys[i];
        const struct jv *a = v->items[i];
        if (i) kb_str(s, " AND ");
        if (!strcmp(op, "$eq")) eq(q, P, a);
        else if (!strcmp(op, "$ne")) { kb_str(s, "("); eq(q, P, a); kb_str(s, ") IS NOT 1"); }
        else if (!strcmp(op, "$gt") || !strcmp(op, "$gte") || !strcmp(op, "$lt") || !strcmp(op, "$lte")) {
            const char *sym = !strcmp(op, "$gt") ? ">" : !strcmp(op, "$gte") ? ">=" : !strcmp(op, "$lt") ? "<" : "<=";
            if (a->t != J_NUM && a->t != J_STR) { qerr(q, "%s takes a number or a string", op); kb_str(s, "0"); continue; }
            int n = bind_scalar(q, a);
            kb_printf(s, "json_extract(doc, %s) %s ?%d", P, sym, n);
        } else if (!strcmp(op, "$in") || !strcmp(op, "$nin")) {
            if (a->t != J_ARR) { qerr(q, "%s takes an array", op); kb_str(s, "0"); continue; }
            kb_str(s, op[1] == 'n' ? "coalesce((" : "((");
            for (int k = 0; k < a->n; k++) { if (k) kb_str(s, " OR "); eq(q, P, a->items[k]); }
            if (!a->n) kb_str(s, "0");
            kb_str(s, op[1] == 'n' ? "), 0) = 0" : "))");
        } else if (!strcmp(op, "$exists")) {
            kb_printf(s, "json_type(doc, %s) IS %sNULL", P, a->t == J_FALSE || (a->t == J_NUM && a->num == 0) ? "" : "NOT ");
        } else if (!strcmp(op, "$size")) {
            if (a->t != J_NUM) { qerr(q, "%s", "$size takes a number"); kb_str(s, "0"); continue; }
            kb_printf(s, "(json_type(doc, %s) = 'array' AND json_array_length(doc, %s) = ?%d)", P, P, bind_num(q, a));
        } else if (!strcmp(op, "$type")) {
            if (a->t != J_STR) { qerr(q, "%s", "$type takes a name"); kb_str(s, "0"); continue; }
            const char *t = a->s;
            const char *w = !strcmp(t, "number") ? "IN ('integer','real')" : !strcmp(t, "bool") || !strcmp(t, "boolean") ? "IN ('true','false')" :
                            !strcmp(t, "string") ? "= 'text'" : !strcmp(t, "array") ? "= 'array'" : !strcmp(t, "object") ? "= 'object'" :
                            !strcmp(t, "null") ? "= 'null'" : NULL;
            if (!w) { qerr(q, "unknown $type: %s", t); kb_str(s, "0"); continue; }
            kb_printf(s, "json_type(doc, %s) %s", P, w);
        } else if (!strcmp(op, "$contains")) {
            if (a->t != J_STR) { qerr(q, "%s", "$contains takes a string"); kb_str(s, "0"); continue; }
            kb_printf(s, "instr(lower(json_extract(doc, %s)), lower(?%d)) > 0", P, bind_text(q, a->s, a->slen));
        } else if (!strcmp(op, "$like")) {
            if (a->t != J_STR) { qerr(q, "%s", "$like takes a pattern (% and _)"); kb_str(s, "0"); continue; }
            kb_printf(s, "json_extract(doc, %s) LIKE ?%d", P, bind_text(q, a->s, a->slen));
        } else if (!strcmp(op, "$not")) {
            kb_str(s, "coalesce((");
            field_cond(q, field, a);
            kb_str(s, "), 0) = 0");
        } else {
            qerr(q, "unknown operator %s", op);
            kb_str(s, "0");
        }
    }
    kb_str(s, ")");
    kb_free(&pb);
}

static void cond(struct q *q, const struct jv *f) {
    struct kbuf *s = &q->sql;
    if (!f || f->t != J_OBJ || !f->n) { kb_str(s, "1"); return; }
    kb_str(s, "(");
    for (int i = 0; i < f->n; i++) {
        const char *k = f->keys[i];
        const struct jv *v = f->items[i];
        if (i) kb_str(s, " AND ");
        if (!strcmp(k, "$and") || !strcmp(k, "$or") || !strcmp(k, "$nor")) {
            if (v->t != J_ARR || !v->n) { qerr(q, "%s takes a list of filters", k); kb_str(s, "0"); continue; }
            kb_str(s, k[1] == 'n' ? "coalesce((" : "(");
            for (int j = 0; j < v->n; j++) {
                if (j) kb_str(s, k[1] == 'a' ? " AND " : " OR ");
                cond(q, v->items[j]);
            }
            kb_str(s, k[1] == 'n' ? "), 0) = 0" : ")");
        } else if (k[0] == '$') {
            qerr(q, "unknown operator %s", k);
            kb_str(s, "0");
        } else {
            field_cond(q, k, v);
        }
    }
    kb_str(s, ")");
}

/* ----------------------------------------------------------- the API */

bool doc_valid_name(const char *n) {
    if (!n[0] || strlen(n) > 60) return false;
    for (const unsigned char *p = (const unsigned char *)n; *p; p++)
        if (!(isalnum(*p) || *p == '_' || *p == '-' || *p >= 0x80)) return false;
    return true;
}

static bool table_exists(struct docdb *d, const char *coll) {
    sqlite3_stmt *st;
    char t[80];
    snprintf(t, sizeof t, "c_%s", coll);
    sqlite3_prepare_v2(d->db, "SELECT 1 FROM sqlite_schema WHERE type = 'table' AND name = ?1", -1, &st, NULL);
    sqlite3_bind_text(st, 1, t, -1, SQLITE_STATIC);
    bool r = sqlite3_step(st) == SQLITE_ROW;
    sqlite3_finalize(st);
    return r;
}

static int ensure(struct docdb *d, const char *coll, char *err, size_t elen) {
    if (!doc_valid_name(coll)) { snprintf(err, elen, "not a name for a collection: %s (letters, digits, _ and -)", coll); return -1; }
    char sql[256];
    snprintf(sql, sizeof sql, "CREATE TABLE IF NOT EXISTS \"c_%s\" (id TEXT PRIMARY KEY, doc TEXT NOT NULL CHECK (json_valid(doc)))", coll);
    if (sqlite3_exec(d->db, sql, NULL, NULL, NULL)) { snprintf(err, elen, "%s", sqlite3_errmsg(d->db)); return -1; }
    return 0;
}

struct docdb *doc_open(const char *path, char *err, size_t elen) {
    char dir[ITX_PATH_MAX];
    strlcpy(dir, path, sizeof dir);
    char *s = strrchr(dir, '/');
    if (s && s != dir) { *s = 0; k_mkdirs(dir); }
    struct docdb *d = calloc(1, sizeof *d);
    strlcpy(d->path, path, sizeof d->path);
    if (sqlite3_open_v2(path, &d->db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL)) {
        snprintf(err, elen, "%s: %s", path, d->db ? sqlite3_errmsg(d->db) : "cannot open it");
        doc_close(d);
        return NULL;
    }
    sqlite3_busy_timeout(d->db, 5000);
    return d;
}

void doc_close(struct docdb *d) {
    if (!d) return;
    sqlite3_close(d->db);
    free(d);
}

int doc_collections(struct docdb *d, char names[][64], long counts[], int max) {
    sqlite3_stmt *st;
    sqlite3_prepare_v2(d->db, "SELECT substr(name, 3) FROM sqlite_schema WHERE type = 'table' AND name LIKE 'c\\_%' ESCAPE '\\' ORDER BY 1",
                       -1, &st, NULL);
    int n = 0;
    while (n < max && sqlite3_step(st) == SQLITE_ROW) strlcpy(names[n++], (const char *)sqlite3_column_text(st, 0), 64);
    sqlite3_finalize(st);
    for (int i = 0; i < n; i++) {
        char sql[128];
        snprintf(sql, sizeof sql, "SELECT count(*) FROM \"c_%s\"", names[i]);
        sqlite3_prepare_v2(d->db, sql, -1, &st, NULL);
        counts[i] = sqlite3_step(st) == SQLITE_ROW ? (long)sqlite3_column_int64(st, 0) : 0;
        sqlite3_finalize(st);
    }
    return n;
}

int doc_insert(struct docdb *d, const char *coll, const struct jv *doc, char *id, size_t idlen, char *err, size_t elen) {
    if (!doc || doc->t != J_OBJ) { snprintf(err, elen, "a document is a JSON object: {...}"); return -1; }
    if (ensure(d, coll, err, elen)) return -1;
    const struct jv *given = json_get(doc, "_id");
    if (given && given->t == J_STR) snprintf(id, idlen, "%.*s", (int)given->slen, given->s);
    else if (given && given->t == J_NUM) snprintf(id, idlen, "%s", given->s);
    else {
        /* like an ObjectId: the time, then random */
        uint8_t r[8];
        k_random(r, sizeof r);
        char h[20];
        k_hex(h, r, 8);
        snprintf(id, idlen, "%08lx%s", (unsigned long)unix_time(), h);
    }
    struct kbuf t = { 0 };
    json_write(doc, &t);
    char sql[200];
    snprintf(sql, sizeof sql, "INSERT INTO \"c_%s\" (id, doc) VALUES (?1, json_set(json(?2), '$._id', %s))", coll,
             given && given->t == J_NUM ? "CAST(?1 AS NUMERIC)" : "?1");
    sqlite3_stmt *st;
    int r = sqlite3_prepare_v2(d->db, sql, -1, &st, NULL);
    if (!r) {
        sqlite3_bind_text(st, 1, id, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 2, t.p, (int)t.n, SQLITE_TRANSIENT);
        r = sqlite3_step(st) == SQLITE_DONE ? 0 : sqlite3_errcode(d->db);
        sqlite3_finalize(st);
    }
    kb_free(&t);
    if (r) {
        snprintf(err, elen, r == SQLITE_CONSTRAINT ? "there is a document with _id %s already" : "%s", r == SQLITE_CONSTRAINT ? id : sqlite3_errmsg(d->db));
        return -1;
    }
    return 0;
}

static long run_rows(struct docdb *d, struct q *q, void (*fn)(const char *, void *), void *ctx, char *err, size_t elen) {
    sqlite3_stmt *st;
    if (q_prepare(d, q, &st, err, elen)) return -1;
    long n = 0;
    int r;
    while ((r = sqlite3_step(st)) == SQLITE_ROW) {
        if (fn) fn((const char *)sqlite3_column_text(st, 0), ctx);
        n++;
    }
    if (r != SQLITE_DONE) { snprintf(err, elen, "%s", sqlite3_errmsg(d->db)); n = -1; }
    sqlite3_finalize(st);
    return n;
}

long doc_find(struct docdb *d, const char *coll, const struct jv *filter, const struct jv *sort, const struct jv *fields,
              long limit, long skip, void (*fn)(const char *, void *), void *ctx, char *err, size_t elen) {
    if (!doc_valid_name(coll)) { snprintf(err, elen, "not a name for a collection: %s", coll); return -1; }
    if (!table_exists(d, coll)) return 0;
    struct q q;
    memset(&q, 0, sizeof q);
    kb_str(&q.sql, "SELECT ");
    if (fields && fields->t == J_OBJ && fields->n) {
        kb_str(&q.sql, "json_object('_id', json_extract(doc, '$._id')");
        for (int i = 0; i < fields->n; i++) {
            if (!strcmp(fields->keys[i], "_id")) continue;
            struct kbuf p = { 0 };
            if (!path(&q, fields->keys[i], &p)) { kb_free(&p); break; }
            kb_printf(&q.sql, ", ?%d, json(doc -> %s)", bind_text(&q, fields->keys[i], strlen(fields->keys[i])), p.p);
            kb_free(&p);
        }
        kb_str(&q.sql, ")");
    } else {
        kb_str(&q.sql, "doc");
    }
    kb_printf(&q.sql, " FROM \"c_%s\" WHERE ", coll);
    cond(&q, filter);
    if (sort && sort->t == J_OBJ && sort->n) {
        kb_str(&q.sql, " ORDER BY ");
        for (int i = 0; i < sort->n; i++) {
            struct kbuf p = { 0 };
            if (!path(&q, sort->keys[i], &p)) { kb_free(&p); break; }
            bool desc = sort->items[i]->t == J_NUM && sort->items[i]->num < 0;
            kb_printf(&q.sql, "%sjson_extract(doc, %s) %s", i ? ", " : "", p.p, desc ? "DESC" : "ASC");
            kb_free(&p);
        }
    }
    if (limit > 0 || skip > 0) kb_printf(&q.sql, " LIMIT %ld OFFSET %ld", limit > 0 ? limit : -1L, skip > 0 ? skip : 0L);
    long n = run_rows(d, &q, fn, ctx, err, elen);
    q_free(&q);
    return n;
}

long doc_count(struct docdb *d, const char *coll, const struct jv *filter, char *err, size_t elen) {
    if (!doc_valid_name(coll)) { snprintf(err, elen, "not a name for a collection: %s", coll); return -1; }
    if (!table_exists(d, coll)) return 0;
    struct q q;
    memset(&q, 0, sizeof q);
    kb_printf(&q.sql, "SELECT count(*) FROM \"c_%s\" WHERE ", coll);
    cond(&q, filter);
    sqlite3_stmt *st;
    long n = -1;
    if (!q_prepare(d, &q, &st, err, elen)) {
        if (sqlite3_step(st) == SQLITE_ROW) n = (long)sqlite3_column_int64(st, 0);
        else snprintf(err, elen, "%s", sqlite3_errmsg(d->db));
        sqlite3_finalize(st);
    }
    q_free(&q);
    return n;
}

long doc_update(struct docdb *d, const char *coll, const struct jv *filter, const struct jv *u, bool many, char *err, size_t elen) {
    if (!u || u->t != J_OBJ) { snprintf(err, elen, "the update is a JSON object: {\"$set\": {...}} or a new document"); return -1; }
    if (!doc_valid_name(coll)) { snprintf(err, elen, "not a name for a collection: %s", coll); return -1; }
    if (!table_exists(d, coll)) return 0;
    struct q q;
    memset(&q, 0, sizeof q);
    /* the new document as an expression, built from the inside out */
    struct kbuf x = { 0 };
    kb_str(&x, "doc");
    if (!is_ops(u)) {
        int n = bind_json(&q, u);
        x.n = 0;
        kb_printf(&x, "json(?%d)", n);
    } else {
        for (int i = 0; i < u->n; i++) {
            const char *op = u->keys[i];
            const struct jv *a = u->items[i];
            if (a->t != J_OBJ) { qerr(&q, "%s takes an object: {\"field\": value}", op); break; }
            for (int k = 0; k < a->n; k++) {
                struct kbuf p = { 0 }, nx = { 0 };
                if (!path(&q, a->keys[k], &p)) { kb_free(&p); break; }
                const struct jv *v = a->items[k];
                if (!strcmp(op, "$set")) kb_printf(&nx, "json_set(%s, %s, json(?%d))", x.p, p.p, bind_json(&q, v));
                else if (!strcmp(op, "$unset")) kb_printf(&nx, "json_remove(%s, %s)", x.p, p.p);
                else if (!strcmp(op, "$inc") || !strcmp(op, "$mul")) {
                    if (v->t != J_NUM) { qerr(&q, "%s takes numbers", op); kb_free(&p); break; }
                    kb_printf(&nx, "json_set(%s, %s, coalesce(json_extract(%s, %s), 0) %s ?%d)", x.p, p.p, x.p, p.p,
                              op[1] == 'i' ? "+" : "*", bind_num(&q, v));
                } else if (!strcmp(op, "$push")) {
                    kb_printf(&nx, "json_set(%s, %s, json_insert(coalesce(json_extract(%s, %s), '[]'), '$[#]', json(?%d)))", x.p, p.p, x.p, p.p,
                              bind_json(&q, v));
                } else if (!strcmp(op, "$pull")) {
                    int n = bind_scalar(&q, v);
                    /* json() around the array: a subquery's value is plain text to json_set otherwise */
                    kb_printf(&nx, "json_set(%s, %s, json((SELECT coalesce(json_group_array(CASE WHEN json_each.type IN ('object','array') "
                                   "THEN json(json_each.value) ELSE json_each.value END), '[]') FROM json_each(%s, %s) WHERE json_each.value IS NOT ?%d)))",
                              x.p, p.p, x.p, p.p, n);
                } else if (!strcmp(op, "$rename")) {
                    struct kbuf to = { 0 };
                    if (v->t != J_STR || !path(&q, v->s, &to)) { qerr(&q, "%s", "$rename takes {\"old\": \"new\"}"); kb_free(&to); kb_free(&p); break; }
                    kb_printf(&nx, "json_set(json_remove(%s, %s), %s, %s -> %s)", x.p, p.p, to.p, x.p, p.p);
                    kb_free(&to);
                } else {
                    qerr(&q, "unknown update operator %s", op);
                    kb_free(&p);
                    break;
                }
                kb_free(&x);
                x = nx;
                kb_free(&p);
            }
        }
    }
    /* the _id stays */
    kb_printf(&q.sql, "UPDATE \"c_%s\" SET doc = json_set(%s, '$._id', json_extract(doc, '$._id')) WHERE rowid IN (SELECT rowid FROM \"c_%s\" WHERE ",
              coll, x.p, coll);
    kb_free(&x);
    cond(&q, filter);
    kb_str(&q.sql, many ? ")" : " LIMIT 1)");
    sqlite3_stmt *st;
    long n = -1;
    if (!q_prepare(d, &q, &st, err, elen)) {
        if (sqlite3_step(st) == SQLITE_DONE) n = sqlite3_changes(d->db);
        else snprintf(err, elen, "%s", sqlite3_errmsg(d->db));
        sqlite3_finalize(st);
    }
    q_free(&q);
    return n;
}

long doc_delete(struct docdb *d, const char *coll, const struct jv *filter, bool many, char *err, size_t elen) {
    if (!doc_valid_name(coll)) { snprintf(err, elen, "not a name for a collection: %s", coll); return -1; }
    if (!table_exists(d, coll)) return 0;
    struct q q;
    memset(&q, 0, sizeof q);
    kb_printf(&q.sql, "DELETE FROM \"c_%s\" WHERE rowid IN (SELECT rowid FROM \"c_%s\" WHERE ", coll, coll);
    cond(&q, filter);
    kb_str(&q.sql, many ? ")" : " LIMIT 1)");
    sqlite3_stmt *st;
    long n = -1;
    if (!q_prepare(d, &q, &st, err, elen)) {
        if (sqlite3_step(st) == SQLITE_DONE) n = sqlite3_changes(d->db);
        else snprintf(err, elen, "%s", sqlite3_errmsg(d->db));
        sqlite3_finalize(st);
    }
    q_free(&q);
    return n;
}

long doc_distinct(struct docdb *d, const char *coll, const char *field, const struct jv *filter,
                  void (*fn)(const char *, void *), void *ctx, char *err, size_t elen) {
    if (!doc_valid_name(coll)) { snprintf(err, elen, "not a name for a collection: %s", coll); return -1; }
    if (!table_exists(d, coll)) return 0;
    struct q q;
    memset(&q, 0, sizeof q);
    struct kbuf p = { 0 };
    if (!path(&q, field, &p)) { snprintf(err, elen, "%s", q.err); kb_free(&p); return -1; }
    kb_printf(&q.sql, "SELECT DISTINCT doc -> %s AS v FROM \"c_%s\" WHERE v IS NOT NULL AND ", p.p, coll);
    kb_free(&p);
    cond(&q, filter);
    kb_str(&q.sql, " ORDER BY 1");
    long n = run_rows(d, &q, fn, ctx, err, elen);
    q_free(&q);
    return n;
}

int doc_index(struct docdb *d, const char *coll, const char *field, char *err, size_t elen) {
    if (ensure(d, coll, err, elen)) return -1;
    struct q q;
    memset(&q, 0, sizeof q);
    struct kbuf p = { 0 };
    if (!path(&q, field, &p)) { snprintf(err, elen, "%s", q.err); kb_free(&p); return -1; }
    char name[96];
    uint32_t h = 2166136261u;
    for (const char *s = field; *s; s++) h = (h ^ (unsigned char)*s) * 16777619u;
    snprintf(name, sizeof name, "ix_%s_%08x", coll, h);
    char sql[512];
    snprintf(sql, sizeof sql, "CREATE INDEX IF NOT EXISTS \"%s\" ON \"c_%s\" (json_extract(doc, %s))", name, coll, p.p);
    kb_free(&p);
    if (sqlite3_exec(d->db, sql, NULL, NULL, NULL)) { snprintf(err, elen, "%s", sqlite3_errmsg(d->db)); return -1; }
    return 0;
}

int doc_drop(struct docdb *d, const char *coll) {
    if (!doc_valid_name(coll)) return -1;
    char sql[128];
    snprintf(sql, sizeof sql, "DROP TABLE IF EXISTS \"c_%s\"", coll);
    return sqlite3_exec(d->db, sql, NULL, NULL, NULL) ? -1 : 0;
}
