/* The terminal of data_place: the itelx terminal (term.h) on a black
 * screen with white text (the "itelx 1.4 data" kernel), with the network
 * and the commands of the databases. The databases' own programs are in
 * /dex: sql, kv, doc, vec, dbscan, dbstat, dbserve.
 *
 *   dbbackup [STICK]     /data to a USB stick (data-backup/), SQLite files
 *                        copied consistently, every file checked by SHA-256
 *   dbrestore [STICK]    /data back from the stick
 *   net, ping, dns, fetch
 */
#include <term.h>
#include <libnet.h>

void term_start(void) {
    k_mkdirs("/data/sql");
    k_mkdirs("/data/doc");
    k_mkdirs("/data/kv");
    k_mkdirs("/data/vec");
}

void term_banner(void) {
    struct itx_sysinfo si;
    sysinfo(&si);
    printf("\ndata_place 1.0  -  %s.  The databases live in /data.\n"
           "  sql       SQL (SQLite %s)               doc     documents (JSON, like MongoDB)\n"
           "  kv        key-value (like Redis)          vec     vectors for AI (nearest search)\n"
           "  dbstat    every database at a glance      dbscan  what a data file is\n"
           "  dbserve   the databases as an HTTP API    kv serve: the Redis protocol\n"
           "  dbbackup  /data to a USB stick            help    every command\n", si.version, "3.54.1");
    struct itx_netinfo ni[4];
    int n = net_info(ni, 4);
    if (n > 0) printf("network: %s (%s)%s\n", ni[0].name, ni[0].model, ni[0].flags & NETF_LINK ? "" : ", no cable");
    else if (n == 0) printf("network: no card this kernel has a driver for (type net)\n");
}

void term_prompt(const char *cwd) { printf("data:%s> ", cwd); }

void term_help(void) {
    printf("\ndata_place:\n"
           "  sql [NAME] [\"SQL\"]       SQL databases (/data/sql/NAME.db), SQLite 3.54.1\n"
           "  doc [NAME] [COMMAND]     document databases (/data/doc): insert, find, update ...\n"
           "  kv [NAME] [COMMAND]      key-value databases (/data/kv): SET, GET, HSET, LPUSH ...\n"
           "  kv serve [NAME] [PORT]   a key-value database to the network (redis-cli connects)\n"
           "  vec [COMMAND]            vector collections (/data/vec): create, add, search ...\n"
           "  dbscan [PATH]            what each data file is (about 40 kinds of databases and AI files)\n"
           "  dbstat                   every database, its size and contents\n"
           "  dbserve [PORT]           SQL, documents, key-value and vectors as an HTTP API (JSON)\n"
           "  dbbackup [STICK]         /data to a USB stick, checked; dbrestore [STICK] back\n");
    net_help();
}

/* ----------------------------------------------------------- backups */

static int copy_checked(const char *from, const char *to, uint64_t *bytes) {
    int in = open(from, O_RDONLY);
    if (in < 0) return in;
    char tmp[ITX_PATH_MAX];
    snprintf(tmp, sizeof tmp, "%s.part", to);
    int out = open(tmp, O_WRONLY | O_CREATE | O_TRUNC);
    if (out < 0) { close(in); return out; }
    static char buf[65536];
    struct k_sha256 a, b;
    k_sha256_init(&a);
    int r = 0;
    for (;;) {
        ssize_t n = read(in, buf, sizeof buf);
        if (n == 0) break;
        if (n < 0) { r = (int)n; break; }
        k_sha256_add(&a, buf, (size_t)n);
        if (write(out, buf, (size_t)n) != n) { r = -ENOSPC; break; }
        *bytes += (uint64_t)n;
    }
    close(in);
    close(out);
    if (r) { remove(tmp); return r; }
    sync();
    /* read back: the copy has to be the same */
    in = open(tmp, O_RDONLY);
    k_sha256_init(&b);
    for (ssize_t n; in >= 0 && (n = read(in, buf, sizeof buf)) > 0;) k_sha256_add(&b, buf, (size_t)n);
    if (in >= 0) close(in);
    uint8_t ha[32], hb[32];
    k_sha256_end(&a, ha);
    k_sha256_end(&b, hb);
    if (memcmp(ha, hb, 32)) { remove(tmp); return -EIO; }
    remove(to);
    return rename(tmp, to);
}

static int copy_dir(const char *from, const char *to, int *files, uint64_t *bytes, bool sqlite_consistent) {
    k_mkdirs(to);
    int fd = open(from, O_RDONLY);
    if (fd < 0) return fd;
    char (*names)[256] = malloc(1024 * 256);
    uint32_t *types = malloc(1024 * sizeof(uint32_t));
    int n = 0;
    struct itx_dirent de;
    while (readdir(fd, &de) == 1 && n < 1024) { strlcpy(names[n], de.name, 256); types[n++] = de.type; }
    close(fd);
    int r = 0;
    for (int i = 0; i < n && !r; i++) {
        char a[ITX_PATH_MAX], b[ITX_PATH_MAX];
        snprintf(a, sizeof a, "%s/%s", from, names[i]);
        snprintf(b, sizeof b, "%s/%s", to, names[i]);
        if (types[i] == VT_DIR) { r = copy_dir(a, b, files, bytes, sqlite_consistent); continue; }
        if (ends_with(names[i], "-lock") || ends_with(names[i], "-journal") || ends_with(names[i], ".part") || !strncmp(names[i], ".tmp", 4)) continue;
        if (sqlite_consistent && ends_with(names[i], ".db")) {
            /* a database another program may be writing: SQLite copies it
               as it is between two transactions (VACUUM INTO), then that copy goes */
            char snap[ITX_PATH_MAX], q[ITX_PATH_MAX + 64];
            snprintf(snap, sizeof snap, "/data/.backup-%d.db", getpid());
            remove(snap);
            snprintf(q, sizeof q, "VACUUM INTO '%s'", snap);
            char *argv[] = { "/dex/sql.xvr", a, q, NULL };
            term_exec("/dex/sql.xvr", 3, argv);
            struct itx_stat st;
            r = stat(snap, &st) == 0 ? copy_checked(snap, b, bytes) : copy_checked(a, b, bytes);
            remove(snap);
        } else {
            r = copy_checked(a, b, bytes);
        }
        if (!r) { (*files)++; printf("  %s\n", b); flush(); }
        else printf("\x1b[91m  %s: %s\x1b[0m\n", a, strerror(r));
    }
    free(names);
    free(types);
    return r;
}

static bool stick_of(const char *given, char *out, size_t len) {
    if (given) { strlcpy(out, given, len); return true; }
    char vols[8][64];
    int n = k_sticks(vols, 8);
    if (!n) { printf("plug in a USB stick (FAT32), or name it: dbbackup /media/NAME\n"); return false; }
    strlcpy(out, vols[0], len);
    return true;
}

bool term_command(int argc, char **argv) {
    const char *c = argv[0];
    if (!strcmp(c, "net")) { cmd_net(argc, argv); return true; }
    if (!strcmp(c, "ping")) { cmd_ping(argc, argv); return true; }
    if (!strcmp(c, "dns")) { cmd_dns(argc, argv); return true; }
    if (!strcmp(c, "fetch")) { cmd_fetch(argc, argv); return true; }
    if (!strcmp(c, "dbbackup") || !strcmp(c, "dbrestore")) {
        char stick[ITX_PATH_MAX], where[ITX_PATH_MAX];
        if (!stick_of(argc > 1 ? argv[1] : NULL, stick, sizeof stick)) return true;
        snprintf(where, sizeof where, "%s/data-backup", stick);
        int files = 0;
        uint64_t bytes = 0;
        long t0 = uptime_ms();
        int r;
        if (c[2] == 'b') r = copy_dir("/data", where, &files, &bytes, true);
        else {
            struct itx_stat st;
            if (stat(where, &st)) { printf("%s has no data-backup folder\n", stick); return true; }
            r = copy_dir(where, "/data", &files, &bytes, false);
        }
        sync();
        char h[32];
        term_human(bytes, h, sizeof h);
        printf("%s: %d file(s), %s, every one checked by SHA-256, in %ld s%s\n", c[2] == 'b' ? "backup" : "restore", files, h,
               (uptime_ms() - t0) / 1000, r ? " (stopped by an error)" : "");
        if (c[2] == 'b' && !r) printf("then: eject %s before unplugging it\n", stick);
        return true;
    }
    return false;
}
