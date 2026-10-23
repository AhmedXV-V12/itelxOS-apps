/* dbstat: every database of data_place at a glance: SQL, documents,
 * key-value, vectors; their sizes and contents, the disk, the memory and
 * the network address the servers listen on. */
#include <kit.h>
#include <sqlite3.h>
#include <itxsqlite.h>
#include <kv.h>
#include <vec.h>
#include <libnet.h>

static void human(uint64_t n, char *out, size_t len) {
    if (n < 1024) snprintf(out, len, "%lu B", (unsigned long)n);
    else if (n < 1048576) snprintf(out, len, "%lu KiB", (unsigned long)(n / 1024));
    else snprintf(out, len, "%lu MiB", (unsigned long)(n >> 20));
}

static uint64_t total;

static int list(const char *dir, const char *ext, char names[][128], uint64_t sizes[], int max) {
    int fd = open(dir, O_RDONLY);
    if (fd < 0) return 0;
    struct itx_dirent de;
    int n = 0;
    while (readdir(fd, &de) == 1 && n < max) {
        if (de.type != VT_FILE || !ends_with(de.name, ext)) continue;
        snprintf(names[n], 128, "%s/%s", dir, de.name);
        sizes[n++] = de.size;
        total += de.size;
    }
    close(fd);
    return n;
}

static long sql_count(sqlite3 *db, const char *q) {
    sqlite3_stmt *st;
    long n = -1;
    if (!sqlite3_prepare_v2(db, q, -1, &st, NULL) && sqlite3_step(st) == SQLITE_ROW) n = (long)sqlite3_column_int64(st, 0);
    sqlite3_finalize(st);
    return n;
}

int main(int argc, char **argv) {
    itx_sqlite_init(32);
    char names[64][128], h[32];
    uint64_t sizes[64];
    printf("\x1b[96mdata_place\x1b[0m  -  the databases in /data\n\n");

    printf("\x1b[93mSQL\x1b[0m (sql NAME)\n");
    int n = list("/data/sql", ".db", names, sizes, 64);
    for (int i = 0; i < n; i++) {
        sqlite3 *db;
        long t = -1, rows = 0;
        if (!sqlite3_open_v2(names[i], &db, SQLITE_OPEN_READONLY, NULL)) {
            t = sql_count(db, "SELECT count(*) FROM sqlite_schema WHERE type = 'table'");
            sqlite3_stmt *st;
            if (!sqlite3_prepare_v2(db, "SELECT name FROM sqlite_schema WHERE type = 'table' AND name NOT LIKE 'sqlite_%'", -1, &st, NULL)) {
                while (sqlite3_step(st) == SQLITE_ROW) {
                    char q[300];
                    snprintf(q, sizeof q, "SELECT count(*) FROM \"%s\"", sqlite3_column_text(st, 0));
                    long r = sql_count(db, q);
                    if (r > 0) rows += r;
                }
                sqlite3_finalize(st);
            }
        }
        sqlite3_close(db);
        human(sizes[i], h, sizeof h);
        printf("  %-36s %9s  %ld table(s), %ld row(s)\n", names[i], h, t, rows);
    }
    if (!n) printf("  none yet\n");

    printf("\x1b[93mDocuments\x1b[0m (doc NAME)\n");
    n = list("/data/doc", ".db", names, sizes, 64);
    for (int i = 0; i < n; i++) {
        sqlite3 *db;
        long c = -1;
        if (!sqlite3_open_v2(names[i], &db, SQLITE_OPEN_READONLY, NULL))
            c = sql_count(db, "SELECT count(*) FROM sqlite_schema WHERE type = 'table' AND name LIKE 'c\\_%' ESCAPE '\\'");
        sqlite3_close(db);
        human(sizes[i], h, sizeof h);
        printf("  %-36s %9s  %ld collection(s)\n", names[i], h, c);
    }
    if (!n) printf("  none yet\n");

    printf("\x1b[93mKey-value\x1b[0m (kv NAME)\n");
    n = list("/data/kv", ".aof", names, sizes, 64);
    for (int i = 0; i < n; i++) {
        char err[100];
        struct kvdb *db = kv_open(names[i], err, sizeof err);
        human(sizes[i], h, sizeof h);
        printf("  %-36s %9s  %ld key(s)\n", names[i], h, db ? kv_size(db) : -1);
        kv_close(db);
    }
    if (!n) printf("  none yet\n");

    printf("\x1b[93mVectors\x1b[0m (vec)\n");
    n = list("/data/vec", ".vec", names, sizes, 64);
    for (int i = 0; i < n; i++) {
        char err[100];
        struct vcoll *c = vec_open(names[i], err, sizeof err);
        human(sizes[i], h, sizeof h);
        if (c) printf("  %-36s %9s  %ld vector(s) of %u dims, %s%s\n", names[i], h, c->live, c->dim, vec_metric_name((int)c->metric), c->text ? ", text" : "");
        vec_close(c);
    }
    if (!n) printf("  none yet\n");

    struct itx_fsinfo fi;
    struct itx_sysinfo si;
    sysinfo(&si);
    human(total, h, sizeof h);
    printf("\nall databases: %s", h);
    if (fsinfo("/", &fi) == 0 && fi.free_bytes != ~0ULL) { human(fi.free_bytes, h, sizeof h); printf("   disk free: %s", h); }
    printf("   memory free: %lu MiB of %lu\n", (unsigned long)(si.free_kib / 1024), (unsigned long)(si.total_kib / 1024));
    struct itx_netinfo ni[4];
    int k = net_info(ni, 4);
    char ip[20];
    for (int i = 0; i < k; i++)
        if (ni[i].flags & NETF_UP) printf("network: %s %s (servers: dbserve, kv serve)\n", ni[i].name, ip_str(ip, sizeof ip, ni[i].ip));
    return 0;
}
