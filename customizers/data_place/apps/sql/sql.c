/* sql: SQL databases on itelx, with SQLite 3.
 *
 *   sql                      the database /data/sql/main.db
 *   sql NAME                 /data/sql/NAME.db (or a path: sql /media/STICK/x.db)
 *   sql NAME "SELECT ..."    one statement, then back to the terminal
 *
 * Statements end with ";" (they may go over several lines). Commands:
 *   .tables .schema [T] .indexes [T] .databases .open NAME
 *   .mode box|list|csv|json|line   .headers on|off   .timer on|off
 *   .import FILE.csv TABLE   .output [FILE]   .read FILE.sql   .dump [T]
 *   .backup FILE   .stats   .help   .quit
 */
#include <kit.h>
#include <sqlite3.h>
#include <itxsqlite.h>

enum { M_BOX, M_LIST, M_CSV, M_JSON, M_LINE };
static int mode = M_BOX;
static bool headers = true, timer;
static int out = 1;                       /* where results go: the terminal or a file */
static sqlite3 *db;
static char dbname[ITX_PATH_MAX];

static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char *fmt, ...) {
    char b[4096];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    if (out == 1) { printf("%s", b); return; }
    write(out, b, (size_t)(n < (int)sizeof b ? n : (int)sizeof b - 1));
}

static void put(const char *s) {
    if (out == 1) { printf("%s", s); return; }
    write(out, s, strlen(s));
}

static bool open_db(const char *name) {
    char path[ITX_PATH_MAX];
    if (strchr(name, '/') || ends_with(name, ".db") || ends_with(name, ".sqlite") || ends_with(name, ".sqlite3"))
        strlcpy(path, name, sizeof path);
    else {
        k_mkdirs("/data/sql");
        snprintf(path, sizeof path, "/data/sql/%s.db", name);
    }
    sqlite3 *n;
    int r = sqlite3_open_v2(path, &n, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
    if (r) {
        printf("%s: %s\n", path, n ? sqlite3_errmsg(n) : sqlite3_errstr(r));
        sqlite3_close(n);
        return false;
    }
    sqlite3_busy_timeout(n, 5000);
    if (db) sqlite3_close(db);
    db = n;
    strlcpy(dbname, path, sizeof dbname);
    return true;
}

/* ------------------------------------------------------------- results */

static int width_of(const char *s) {
    int w = 0;
    for (; s && *s; s++) if (((unsigned char)*s & 0xC0) != 0x80) w++;
    return w;
}

static void csv_cell(const char *s) {
    bool q = s && (strchr(s, ',') || strchr(s, '"') || strchr(s, '\n'));
    if (!s) return;
    if (!q) { put(s); return; }
    put("\"");
    for (const char *p = s; *p; p++) { char c[3] = { *p, *p == '"' ? '"' : 0, 0 }; put(c); }
    put("\"");
}

static void json_str(const char *s) {
    put("\"");
    char b[8];
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (*p == '"' || *p == '\\') { b[0] = '\\'; b[1] = (char)*p; b[2] = 0; put(b); }
        else if (*p == '\n') put("\\n");
        else if (*p == '\t') put("\\t");
        else if (*p < 0x20) { snprintf(b, sizeof b, "\\u%04x", *p); put(b); }
        else { b[0] = (char)*p; b[1] = 0; put(b); }
    }
    put("\"");
}

/* a box: the rows are read first for the widths (up to 2000) */
static int box(sqlite3_stmt *st, int cols) {
    int cap = 64, n = 0, r = SQLITE_ROW;
    char ***rows = malloc(sizeof(char **) * cap);
    int *w = calloc((size_t)cols, sizeof(int));
    for (int c = 0; c < cols; c++) w[c] = width_of(sqlite3_column_name(st, c));
    while ((r = sqlite3_step(st)) == SQLITE_ROW && n < 2000) {
        if (n == cap) { cap *= 2; rows = realloc(rows, sizeof(char **) * cap); }
        char **row = malloc(sizeof(char *) * cols);
        for (int c = 0; c < cols; c++) {
            const char *t = (const char *)sqlite3_column_text(st, c);
            row[c] = strdup(t ? t : sqlite3_column_type(st, c) == SQLITE_NULL ? "NULL" : "");
            int cw = width_of(row[c]);
            if (cw > 60) cw = 60;
            if (cw > w[c]) w[c] = cw;
        }
        rows[n++] = row;
    }
    char cell[512];
    if (headers) {
        put("\xc7\x80 ");
        for (int c = 0; c < cols; c++) { k_fit(cell, sizeof cell, sqlite3_column_name(st, c), w[c]); put(cell); put(" \xc7\x80 "); }
        put("\n");
        for (int c = 0; c < cols; c++) { put(c ? "---" : "\xc7\x80-"); for (int i = 0; i < w[c]; i++) put("-"); }
        put("-\xc7\x80\n");
    }
    for (int i = 0; i < n; i++) {
        put("\xc7\x80 ");
        for (int c = 0; c < cols; c++) {
            int t = sqlite3_column_type(st, c);
            (void)t;
            k_fit(cell, sizeof cell, rows[i][c], w[c]);
            put(cell);
            put(" \xc7\x80 ");
            free(rows[i][c]);
        }
        put("\n");
        free(rows[i]);
    }
    int more = 0;
    while (r == SQLITE_ROW) { more++; r = sqlite3_step(st); }
    if (more) say("(%d more rows not shown: use LIMIT, or .mode list)\n", more);
    free(rows);
    free(w);
    say("%d row%s\n", n + more, n + more == 1 ? "" : "s");
    return r;
}

static int run_stmt(sqlite3_stmt *st) {
    int cols = sqlite3_column_count(st);
    if (!cols) { int r = sqlite3_step(st); while (r == SQLITE_ROW) r = sqlite3_step(st); return r; }
    if (mode == M_BOX) return box(st, cols);
    int r, n = 0;
    if (headers && (mode == M_LIST || mode == M_CSV)) {
        for (int c = 0; c < cols; c++) {
            if (c) put(mode == M_CSV ? "," : "|");
            if (mode == M_CSV) csv_cell(sqlite3_column_name(st, c)); else put(sqlite3_column_name(st, c));
        }
        put("\n");
    }
    if (mode == M_JSON) put("[");
    while ((r = sqlite3_step(st)) == SQLITE_ROW) {
        if (mode == M_JSON) {
            put(n ? ",\n{" : "\n{");
            for (int c = 0; c < cols; c++) {
                if (c) put(",");
                json_str(sqlite3_column_name(st, c));
                put(":");
                int t = sqlite3_column_type(st, c);
                const char *v = (const char *)sqlite3_column_text(st, c);
                if (t == SQLITE_NULL) put("null");
                else if (t == SQLITE_INTEGER || t == SQLITE_FLOAT) put(v);
                else json_str(v ? v : "");
            }
            put("}");
        } else if (mode == M_LINE) {
            if (n) put("\n");
            for (int c = 0; c < cols; c++) {
                const char *v = (const char *)sqlite3_column_text(st, c);
                say("%20s = %s\n", sqlite3_column_name(st, c), v ? v : "NULL");
            }
        } else {
            for (int c = 0; c < cols; c++) {
                if (c) put(mode == M_CSV ? "," : "|");
                const char *v = (const char *)sqlite3_column_text(st, c);
                if (mode == M_CSV) csv_cell(v); else put(v ? v : "");
            }
            put("\n");
        }
        n++;
    }
    if (mode == M_JSON) put("\n]\n");
    return r;
}

/* every statement of a text; false on the first error */
static bool exec_text(const char *sql) {
    const char *tail = sql;
    while (tail && *tail) {
        sqlite3_stmt *st = NULL;
        long t0 = uptime_ms();
        int r = sqlite3_prepare_v2(db, tail, -1, &st, &tail);
        if (r) { printf("\x1b[91merror: %s\x1b[0m\n", sqlite3_errmsg(db)); return false; }
        if (!st) continue;                           /* only spaces or a comment */
        /* INSERT, UPDATE, DELETE say how many rows they changed */
        const char *k = sqlite3_sql(st);
        while (*k == ' ' || *k == '\n' || *k == '\t' || *k == '\r') k++;
        bool dml = !strncasecmp(k, "insert", 6) || !strncasecmp(k, "update", 6) || !strncasecmp(k, "delete", 6) ||
                   !strncasecmp(k, "replace", 7);
        bool rows = sqlite3_column_count(st) > 0;
        r = run_stmt(st);
        sqlite3_finalize(st);
        if (r != SQLITE_DONE) { printf("\x1b[91merror: %s\x1b[0m\n", sqlite3_errmsg(db)); return false; }
        long changed = sqlite3_changes(db);
        if (dml && !rows && out == 1) printf("%ld row%s changed\n", changed, changed == 1 ? "" : "s");
        if (timer) printf("\x1b[90m(%ld ms)\x1b[0m\n", uptime_ms() - t0);
    }
    return true;
}

static void query_print(const char *sql) {
    int old = mode;
    if (mode == M_BOX) mode = M_LIST;
    bool h = headers;
    headers = false;
    exec_text(sql);
    mode = old;
    headers = h;
}

/* ------------------------------------------------------- CSV import */

/* one CSV record of the text at *p; the fields go into f (n of them) */
static int csv_record(char **p, char **f, int max) {
    int n = 0;
    char *s = *p;
    if (!*s) return 0;
    while (n < max) {
        char *o = s, *start = s;
        if (*s == '"') {
            s++;
            start = o = s;
            for (;;) {
                if (!*s) break;
                if (*s == '"') { if (s[1] == '"') { *o++ = '"'; s += 2; continue; } s++; break; }
                *o++ = *s++;
            }
            while (*s && *s != ',' && *s != '\n' && *s != '\r') s++;
        } else {
            while (*s && *s != ',' && *s != '\n' && *s != '\r') s++;
            o = s;
        }
        char end = *s;
        *o = 0;
        f[n++] = start;
        if (end == ',') { s++; continue; }
        if (end == '\r') s++;
        if (*s == '\n') s++;
        break;
    }
    *p = s;
    return n;
}

static void import_csv(const char *file, const char *table) {
    size_t len;
    char *t = read_file(file, &len, 256u << 20);
    if (!t) { printf("%s: cannot read it\n", file); return; }
    char *p = t;
    if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) p += 3;   /* a BOM */
    char *head[256];
    int nc = csv_record(&p, head, 256);
    if (!nc) { printf("%s is empty\n", file); free(t); return; }
    /* the table, when it is not there: one TEXT column per header field */
    char *sql = malloc(65536);
    int o = snprintf(sql, 65536, "CREATE TABLE IF NOT EXISTS \"%s\" (", table);
    for (int i = 0; i < nc; i++) o += snprintf(sql + o, 65536 - o, "%s\"%s\"", i ? ", " : "", head[i][0] ? head[i] : "col");
    snprintf(sql + o, 65536 - o, ")");
    if (sqlite3_exec(db, sql, NULL, NULL, NULL)) { printf("error: %s\n", sqlite3_errmsg(db)); free(sql); free(t); return; }
    o = snprintf(sql, 65536, "INSERT INTO \"%s\" VALUES (", table);
    for (int i = 0; i < nc; i++) o += snprintf(sql + o, 65536 - o, "%s?", i ? "," : "");
    snprintf(sql + o, 65536 - o, ")");
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL)) { printf("error: %s\n", sqlite3_errmsg(db)); free(sql); free(t); return; }
    sqlite3_exec(db, "BEGIN", NULL, NULL, NULL);
    long rows = 0, bad = 0;
    char *f[256];
    while (*p) {
        int n = csv_record(&p, f, 256);
        if (!n || (n == 1 && !f[0][0])) continue;
        for (int i = 0; i < nc; i++) {
            if (i >= n) { sqlite3_bind_null(st, i + 1); continue; }
            /* numbers as numbers, so that sums and comparisons work */
            char *e;
            long v = strtol(f[i], &e, 10);
            if (f[i][0] && !*e && strlen(f[i]) < 19) sqlite3_bind_int64(st, i + 1, v);
            else sqlite3_bind_text(st, i + 1, f[i], -1, SQLITE_TRANSIENT);
        }
        if (sqlite3_step(st) != SQLITE_DONE) bad++;
        else rows++;
        sqlite3_reset(st);
    }
    sqlite3_finalize(st);
    sqlite3_exec(db, "COMMIT", NULL, NULL, NULL);
    printf("%ld row(s) imported into %s%s\n", rows, table, bad ? " (some rows were refused)" : "");
    free(sql);
    free(t);
}

/* ------------------------------------------------------------ commands */

static void help(void) {
    printf("SQL statements end with ; and may go over several lines. SQLite %s.\n"
           "  .tables              the tables          .schema [TABLE]   how they are made\n"
           "  .indexes [TABLE]     the indexes         .databases        the attached databases\n"
           "  .open NAME           another database (/data/sql/NAME.db, or a path)\n"
           "  .mode box|list|csv|json|line             .headers on|off   .timer on|off\n"
           "  .import FILE TABLE   a CSV file (first line: the column names) into a table\n"
           "  .output [FILE]       results into a file (.output alone: the screen again)\n"
           "  .read FILE           the statements of a file    .dump [TABLE]   the database as SQL\n"
           "  .backup FILE         a copy of the database      .stats          size and pages\n"
           "  .help | .quit\n"
           "Also: full-text search (CREATE VIRTUAL TABLE t USING fts5(...)), JSON (json_extract ...),\n"
           "math (sqrt, pow, ln, ...), window functions, CTEs.\n", sqlite3_libversion());
}

static void dump(const char *only) {
    say("BEGIN TRANSACTION;\n");
    sqlite3_stmt *st;
    sqlite3_prepare_v2(db, "SELECT name, type, sql FROM sqlite_schema WHERE sql NOT NULL AND name NOT LIKE 'sqlite_%' "
                           "AND (?1 IS NULL OR tbl_name = ?1) ORDER BY type = 'table' DESC, name", -1, &st, NULL);
    if (only) sqlite3_bind_text(st, 1, only, -1, SQLITE_STATIC);
    while (sqlite3_step(st) == SQLITE_ROW) {
        const char *name = (const char *)sqlite3_column_text(st, 0), *type = (const char *)sqlite3_column_text(st, 1);
        say("%s;\n", sqlite3_column_text(st, 2));
        if (strcmp(type, "table")) continue;
        char q[1024];
        snprintf(q, sizeof q, "SELECT 'INSERT INTO \"%s\" VALUES(' || ", name);
        sqlite3_stmt *cols;
        char pq[512];
        snprintf(pq, sizeof pq, "SELECT name FROM pragma_table_info('%s')", name);
        sqlite3_prepare_v2(db, pq, -1, &cols, NULL);
        int n = 0;
        char *expr = malloc(65536);
        int o = 0;
        expr[0] = 0;
        while (sqlite3_step(cols) == SQLITE_ROW)
            o += snprintf(expr + o, 65536 - o, "%squote(\"%s\")", n++ ? " || ',' || " : "", sqlite3_column_text(cols, 0));
        sqlite3_finalize(cols);
        char *full = malloc(65536 + 2048);
        snprintf(full, 65536 + 2048, "%s%s || ');' FROM \"%s\"", q, n ? expr : "''", name);
        sqlite3_stmt *rows;
        if (!sqlite3_prepare_v2(db, full, -1, &rows, NULL)) {
            while (sqlite3_step(rows) == SQLITE_ROW) { put((const char *)sqlite3_column_text(rows, 0)); put("\n"); }
            sqlite3_finalize(rows);
        }
        free(expr);
        free(full);
    }
    sqlite3_finalize(st);
    say("COMMIT;\n");
}

static bool dot(char *line) {
    char *argv[8];
    int argc = 0;
    for (char *p = line; *p && argc < 8;) {
        while (*p == ' ' || *p == '\t' || *p == '\n') *p++ = 0;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n') p++;
    }
    if (!argc) return true;
    const char *c = argv[0];
    if (!strcmp(c, ".quit") || !strcmp(c, ".exit") || !strcmp(c, ".q")) return false;
    if (!strcmp(c, ".help")) help();
    else if (!strcmp(c, ".tables")) query_print("SELECT name FROM sqlite_schema WHERE type IN ('table','view') AND name NOT LIKE 'sqlite_%' ORDER BY 1");
    else if (!strcmp(c, ".schema")) {
        char q[512];
        snprintf(q, sizeof q, "SELECT sql || ';' FROM sqlite_schema WHERE sql NOT NULL AND name NOT LIKE 'sqlite_%%'%s%s%s ORDER BY tbl_name, type DESC",
                 argc > 1 ? " AND tbl_name = '" : "", argc > 1 ? argv[1] : "", argc > 1 ? "'" : "");
        query_print(q);
    } else if (!strcmp(c, ".indexes")) {
        char q[512];
        snprintf(q, sizeof q, "SELECT name FROM sqlite_schema WHERE type = 'index'%s%s%s ORDER BY 1", argc > 1 ? " AND tbl_name = '" : "",
                 argc > 1 ? argv[1] : "", argc > 1 ? "'" : "");
        query_print(q);
    } else if (!strcmp(c, ".databases")) query_print("SELECT name || ': ' || file FROM pragma_database_list");
    else if (!strcmp(c, ".open")) { if (argc > 1 && open_db(argv[1])) printf("%s\n", dbname); }
    else if (!strcmp(c, ".mode")) {
        static const char *const m[] = { "box", "list", "csv", "json", "line" };
        bool ok = false;
        for (int i = 0; i < 5 && argc > 1; i++) if (!strcmp(argv[1], m[i])) { mode = i; ok = true; }
        if (!ok) printf("modes: box list csv json line\n");
    } else if (!strcmp(c, ".headers")) headers = argc < 2 || !strcmp(argv[1], "on");
    else if (!strcmp(c, ".timer")) timer = argc < 2 || !strcmp(argv[1], "on");
    else if (!strcmp(c, ".import")) { if (argc > 2) import_csv(argv[1], argv[2]); else printf("use: .import FILE.csv TABLE\n"); }
    else if (!strcmp(c, ".output")) {
        if (out != 1) close(out);
        out = 1;
        if (argc > 1) {
            int fd = open(argv[1], O_WRONLY | O_CREATE | O_TRUNC);
            if (fd < 0) printf("%s: %s\n", argv[1], strerror(fd));
            else { out = fd; printf("results go to %s (.output alone: the screen)\n", argv[1]); }
        }
    } else if (!strcmp(c, ".read")) {
        size_t len;
        char *t = argc > 1 ? read_file(argv[1], &len, 64u << 20) : NULL;
        if (!t) printf("use: .read FILE.sql\n");
        else { exec_text(t); free(t); }
    } else if (!strcmp(c, ".dump")) dump(argc > 1 ? argv[1] : NULL);
    else if (!strcmp(c, ".backup")) {
        if (argc < 2) { printf("use: .backup FILE\n"); return true; }
        char q[ITX_PATH_MAX + 32];
        snprintf(q, sizeof q, "VACUUM INTO '%s'", argv[1]);
        remove(argv[1]);
        if (sqlite3_exec(db, q, NULL, NULL, NULL)) printf("error: %s\n", sqlite3_errmsg(db));
        else printf("a copy of %s is in %s\n", dbname, argv[1]);
    } else if (!strcmp(c, ".stats")) {
        query_print("SELECT 'pages: ' || (SELECT page_count FROM pragma_page_count) || ' of ' || "
                    "(SELECT page_size FROM pragma_page_size) || ' bytes, free pages: ' || (SELECT freelist_count FROM pragma_freelist_count)");
        query_print("SELECT 'tables: ' || count(*) FROM sqlite_schema WHERE type = 'table'");
    } else printf("unknown: %s (.help)\n", c);
    return true;
}

int main(int argc, char **argv) {
    int r = itx_sqlite_init(96);
    if (r) { printf("SQLite could not start: %s\n", sqlite3_errstr(r)); return 1; }
    if (!open_db(argc > 1 ? argv[1] : "main")) return 1;
    if (argc > 2) {
        /* one statement, or a dot command */
        char *line = argv[2];
        if (line[0] == '.') { dot(line); return 0; }
        return exec_text(line) ? 0 : 1;
    }
    printf("SQLite %s  -  %s  -  .help for the commands, .quit to leave\n", sqlite3_libversion(), dbname);
    const char *base = strrchr(dbname, '/') ? strrchr(dbname, '/') + 1 : dbname;
    size_t cap = 65536, len = 0;
    char *buf = malloc(cap);
    buf[0] = 0;
    for (;;) {
        printf(len ? "   ...> " : "sql:%.*s> ", (int)(strchr(base, '.') ? strchr(base, '.') - base : (long)strlen(base)), base);
        flush();
        char line[4096];
        ssize_t n = read(0, line, sizeof line - 1);
        if (n <= 0) break;
        line[n] = 0;
        if (!len && line[0] == '.') { if (!dot(line)) break; continue; }
        if (len + (size_t)n + 2 >= cap) { cap = (len + (size_t)n + 2) * 2; buf = realloc(buf, cap); }
        memcpy(buf + len, line, (size_t)n + 1);
        len += (size_t)n;
        if (sqlite3_complete(buf)) {
            exec_text(buf);
            len = 0;
            buf[0] = 0;
        }
    }
    if (out != 1) close(out);
    sqlite3_close(db);
    return 0;
}
