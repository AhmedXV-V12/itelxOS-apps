/* doc: the document databases of data_place (common/doc/doc.h).
 *
 *   doc [DB]                    the shell (DB: /data/doc/DB.db, main by default)
 *   doc DB COMMAND ...          one command
 *
 *   collections
 *   insert COLL {json} | [{json}, ...]
 *   find COLL [FILTER] [sort {"f": -1}] [limit N] [skip N] [fields {"f": 1}]
 *   count COLL [FILTER]
 *   update COLL FILTER UPDATE [all]       (one document, or all that match)
 *   delete COLL FILTER [all]
 *   distinct COLL FIELD [FILTER]
 *   index COLL FIELD     drop COLL
 *   import COLL FILE     (JSON lines, or one JSON array)    export COLL FILE
 */
#include <doc.h>
#include <itxsqlite.h>

static struct docdb *db;

/* the next word, or the next JSON value when it starts with { or [ */
static bool next(const char **p, char *word, size_t wl, struct jv **json) {
    while (**p == ' ' || **p == '\t' || **p == '\n' || **p == '\r') (*p)++;
    if (!**p) return false;
    *json = NULL;
    word[0] = 0;
    if (**p == '{' || **p == '[') {
        char err[160];
        size_t used;
        *json = json_parse(*p, strlen(*p), &used, err, sizeof err);
        if (!*json) { printf("bad JSON: %s\n", err); *p += strlen(*p); return false; }
        *p += used;
        return true;
    }
    size_t n = 0;
    while (**p && **p != ' ' && **p != '\t' && **p != '\n' && **p != '\r' && n + 1 < wl) word[n++] = *(*p)++;
    word[n] = 0;
    return true;
}

static void print_doc(const char *json, void *ctx) {
    (*(long *)ctx)++;
    printf("%s\n", json);
}

static void insert_many(const char *coll, struct jv *v) {
    char id[64], err[256];
    long n = 0, bad = 0;
    sqlite3_exec(db->db, "BEGIN", NULL, NULL, NULL);
    if (v->t == J_ARR) {
        for (int i = 0; i < v->n; i++) { if (doc_insert(db, coll, v->items[i], id, sizeof id, err, sizeof err)) { bad++; printf("%s\n", err); } else n++; }
    } else if (!doc_insert(db, coll, v, id, sizeof id, err, sizeof err)) {
        n++;
        printf("inserted, _id \"%s\"\n", id);
    } else {
        bad++;
        printf("%s\n", err);
    }
    sqlite3_exec(db->db, "COMMIT", NULL, NULL, NULL);
    if (v->t == J_ARR) printf("%ld inserted%s\n", n, bad ? ", some refused" : "");
}

static void import_file(const char *coll, const char *file) {
    size_t len;
    char *t = read_file(file, &len, 256u << 20);
    if (!t) { printf("%s: cannot read it\n", file); return; }
    char err[200], id[64];
    long n = 0, bad = 0;
    sqlite3_exec(db->db, "BEGIN", NULL, NULL, NULL);
    size_t at = 0;
    while (at < len) {
        while (at < len && (t[at] == ' ' || t[at] == '\n' || t[at] == '\r' || t[at] == '\t' || t[at] == ',')) at++;
        if (at >= len) break;
        size_t used;
        struct jv *v = json_parse(t + at, len - at, &used, err, sizeof err);
        if (!v) { printf("%s: %s\n", file, err); bad++; break; }
        at += used;
        if (v->t == J_ARR) {
            for (int i = 0; i < v->n; i++) { if (doc_insert(db, coll, v->items[i], id, sizeof id, err, sizeof err)) bad++; else n++; }
        } else if (doc_insert(db, coll, v, id, sizeof id, err, sizeof err)) bad++;
        else n++;
        json_free(v);
    }
    sqlite3_exec(db->db, "COMMIT", NULL, NULL, NULL);
    free(t);
    printf("%ld document(s) imported into %s%s\n", n, coll, bad ? " (some were refused)" : "");
}

struct exp_ctx { int fd; long n; };
static void export_one(const char *json, void *vctx) {
    struct exp_ctx *c = vctx;
    write(c->fd, json, strlen(json));
    write(c->fd, "\n", 1);
    c->n++;
}

static void help(void) {
    printf("collections\n"
           "insert COLL {json}            or a list: [{...}, {...}]\n"
           "find COLL [FILTER] [sort {\"age\": -1}] [limit N] [skip N] [fields {\"name\": 1}]\n"
           "count COLL [FILTER]           distinct COLL FIELD [FILTER]\n"
           "update COLL FILTER UPDATE [all]   e.g. update people {\"name\": \"Ahmed\"} {\"$inc\": {\"age\": 1}}\n"
           "delete COLL FILTER [all]      index COLL FIELD      drop COLL\n"
           "import COLL FILE              JSON lines or a JSON array\n"
           "export COLL FILE              JSON lines\n"
           "Filters: {\"age\": {\"$gte\": 18}}, {\"$or\": [{...}, {...}]}, {\"tags\": \"x\"} (in an array),\n"
           "  $eq $ne $gt $gte $lt $lte $in $nin $exists $size $type $contains $like $not $and $or $nor\n"
           "Updates: $set $unset $inc $mul $push $pull $rename, or a whole new document.\n"
           "The database is an SQLite file: sql %s opens it with SQL (tables c_COLLECTION).\n", db->path);
}

static int run(const char *line) {
    const char *p = line;
    char cmd[32], coll[96], w[64];
    struct jv *j = NULL;
    if (!next(&p, cmd, sizeof cmd, &j) || j) { json_free(j); return 0; }
    char err[256] = "";
    if (!strcmp(cmd, "help")) { help(); return 0; }
    if (!strcmp(cmd, "quit") || !strcmp(cmd, "exit")) return 1;
    if (!strcmp(cmd, "collections") || !strcmp(cmd, "show")) {
        char names[128][64];
        long counts[128];
        int n = doc_collections(db, names, counts, 128);
        for (int i = 0; i < n; i++) printf("  %-30s %ld document(s)\n", names[i], counts[i]);
        if (!n) printf("no collections yet (insert makes one)\n");
        return 0;
    }
    if (!next(&p, coll, sizeof coll, &j) || j) { json_free(j); printf("%s needs a collection name (help)\n", cmd); return 0; }
    if (!strcmp(cmd, "drop")) { printf(doc_drop(db, coll) ? "could not drop %s\n" : "%s is dropped\n", coll); return 0; }
    if (!strcmp(cmd, "import") || !strcmp(cmd, "export")) {
        char file[ITX_PATH_MAX];
        if (!next(&p, file, sizeof file, &j) || j) { json_free(j); printf("use: %s COLL FILE\n", cmd); return 0; }
        if (cmd[0] == 'i') { import_file(coll, file); return 0; }
        int fd = open(file, O_WRONLY | O_CREATE | O_TRUNC);
        if (fd < 0) { printf("%s: %s\n", file, strerror(fd)); return 0; }
        struct exp_ctx c = { fd, 0 };
        long r = doc_find(db, coll, NULL, NULL, NULL, 0, 0, export_one, &c, err, sizeof err);
        close(fd);
        if (r < 0) printf("%s\n", err); else printf("%ld document(s) written to %s\n", c.n, file);
        return 0;
    }
    if (!strcmp(cmd, "index") || !strcmp(cmd, "distinct")) {
        if (!next(&p, w, sizeof w, &j) || j) { json_free(j); printf("use: %s COLL FIELD\n", cmd); return 0; }
        if (cmd[0] == 'i') { if (doc_index(db, coll, w, err, sizeof err)) printf("%s\n", err); else printf("index on %s.%s\n", coll, w); return 0; }
        struct jv *f = NULL;
        next(&p, err, sizeof err, &f);
        long n = 0;
        long r = doc_distinct(db, coll, w, f, print_doc, &n, err, sizeof err);
        if (r < 0) printf("%s\n", err); else printf("%ld value(s)\n", r);
        json_free(f);
        return 0;
    }
    if (!strcmp(cmd, "insert")) {
        if (!next(&p, w, sizeof w, &j) || !j) { printf("use: insert COLL {json}\n"); return 0; }
        insert_many(coll, j);
        json_free(j);
        return 0;
    }
    /* find, count, update, delete: a filter and the rest */
    struct jv *filter = NULL, *second = NULL, *sort = NULL, *fields = NULL;
    long limit = 0, skip = 0;
    bool all = false;
    while (next(&p, w, sizeof w, &j)) {
        if (j) {
            if (!filter) filter = j;
            else if (!second) second = j;
            else json_free(j);
            continue;
        }
        if (!strcmp(w, "all")) all = true;
        else if (!strcmp(w, "limit") || !strcmp(w, "skip")) {
            char n[32];
            struct jv *x = NULL;
            if (next(&p, n, sizeof n, &x) && !x) { if (w[0] == 'l') limit = atol(n); else skip = atol(n); }
            json_free(x);
        } else if (!strcmp(w, "sort") || !strcmp(w, "fields")) {
            struct jv *x = NULL;
            char dummy[8];
            if (next(&p, dummy, sizeof dummy, &x) && x) { if (w[0] == 's') sort = x; else fields = x; }
        } else {
            printf("not understood: %s (help)\n", w);
        }
    }
    long r;
    if (!strcmp(cmd, "find")) {
        long n = 0;
        r = doc_find(db, coll, filter, sort, fields, limit, skip, print_doc, &n, err, sizeof err);
        if (r >= 0) printf("%ld document(s)\n", r);
    } else if (!strcmp(cmd, "count")) {
        r = doc_count(db, coll, filter, err, sizeof err);
        if (r >= 0) printf("%ld\n", r);
    } else if (!strcmp(cmd, "update")) {
        if (!second) { printf("use: update COLL FILTER UPDATE [all]\n"); r = 0; }
        else { r = doc_update(db, coll, filter, second, all, err, sizeof err); if (r >= 0) printf("%ld document(s) updated\n", r); }
    } else if (!strcmp(cmd, "delete")) {
        if (!filter) { printf("delete needs a filter ({} with all: every document)\n"); r = 0; }
        else { r = doc_delete(db, coll, filter, all, err, sizeof err); if (r >= 0) printf("%ld document(s) deleted\n", r); }
    } else {
        printf("unknown command: %s (help)\n", cmd);
        r = 0;
    }
    if (r < 0) printf("\x1b[91m%s\x1b[0m\n", err);
    json_free(filter);
    json_free(second);
    json_free(sort);
    json_free(fields);
    return 0;
}

int main(int argc, char **argv) {
    if (itx_sqlite_init(64)) { printf("SQLite could not start\n"); return 1; }
    const char *name = argc > 1 ? argv[1] : "main";
    char path[ITX_PATH_MAX], err[256];
    if (strchr(name, '/')) strlcpy(path, name, sizeof path);
    else snprintf(path, sizeof path, "/data/doc/%s.db", name);
    db = doc_open(path, err, sizeof err);
    if (!db) { printf("doc: %s\n", err); return 1; }
    if (argc > 2) {
        /* one command: the rest of the arguments, as one line */
        struct kbuf l = { 0 };
        for (int i = 2; i < argc; i++) { if (i > 2) kb_str(&l, " "); kb_str(&l, argv[i]); }
        run(l.p);
        kb_free(&l);
        doc_close(db);
        return 0;
    }
    printf("doc %s  -  documents in collections  -  help for the commands, quit to leave\n", path);
    const char *base = strrchr(path, '/') + 1;
    for (;;) {
        printf("doc:%.*s> ", (int)(strchr(base, '.') ? strchr(base, '.') - base : (long)strlen(base)), base);
        flush();
        char line[16384];
        ssize_t n = read(0, line, sizeof line - 1);
        if (n <= 0) break;
        line[n] = 0;
        if (run(line)) break;
    }
    doc_close(db);
    return 0;
}
