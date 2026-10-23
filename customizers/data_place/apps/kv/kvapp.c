/* kv: the key-value databases of data_place (common/db/kv.h).
 *
 *   kv [DB]                          the shell (DB: /data/kv/DB.aof, main by default)
 *   kv DB COMMAND [ARG ...]          one command
 *   kv serve [DB] [PORT] [PASSWORD]  the database to the network, in the protocol
 *                                    of Redis (6379 by default): redis-cli and
 *                                    Redis clients connect to it. Without a
 *                                    password one is made and shown: clients
 *                                    send AUTH (redis-cli -a PASSWORD).
 */
#include <kv.h>
#include <libnet.h>

static void path_of(const char *name, char *out, size_t len) {
    if (strchr(name, '/')) strlcpy(out, name, len);
    else snprintf(out, len, "/data/kv/%s.aof", name);
}

static void show(struct kbuf *resp) {
    struct kbuf t = { 0 };
    resp_print(resp->p, resp->n, &t);
    if (t.n) { flush(); write(1, t.p, t.n); }
    kb_free(&t);
}

static int shell(struct kvdb *db, const char *name) {
    printf("kv %s  -  %ld keys  -  HELP for the commands, QUIT to leave\n", name, kv_size(db));
    for (;;) {
        printf("kv:%s> ", name);
        flush();
        char line[8192];
        ssize_t n = read(0, line, sizeof line - 1);
        if (n <= 0) break;
        line[n] = 0;
        char **argv;
        size_t *lens;
        int argc = kv_split(line, &argv, &lens);
        if (!argc) { resp_free(argc, argv, lens); continue; }
        if (!strcasecmp(argv[0], "QUIT") || !strcasecmp(argv[0], "EXIT")) { resp_free(argc, argv, lens); break; }
        struct kbuf out = { 0 };
        kv_exec(db, argc, argv, lens, &out);
        show(&out);
        kb_free(&out);
        resp_free(argc, argv, lens);
    }
    return 0;
}

/* ---------------------------------------------------------- the server */

#define MAXC 16

struct client { int s; bool auth; struct kbuf in; char ip[20]; };

static int serve(struct kvdb *db, const char *name, int port, const char *password) {
    int ls = sock_open(SOCK_TCP);
    if (ls < 0) { printf("kv serve: %s\n", net_error(ls)); return 1; }
    int r = sock_bind(ls, port);
    if (r < 0) { printf("kv serve: port %d: %s\n", port, net_error(r)); return 1; }
    sock_listen(ls, 8);
    if (!net_ready(8000)) printf("(no address yet: see net)\n");
    struct itx_netinfo ni[4];
    int n = net_info(ni, 4);
    char ip[20] = "?";
    for (int i = 0; i < n; i++) if (ni[i].flags & NETF_UP) { ip_str(ip, sizeof ip, ni[i].ip); break; }
    printf("kv %s on %s port %d (the Redis protocol), %ld keys\n"
           "password: %s\n"
           "from another computer:  redis-cli -h %s -p %d -a %s\n"
           "Ctrl+C stops it.\n", name, ip, port, kv_size(db), password, ip, port, password);
    flush();
    struct client cl[MAXC];
    memset(cl, 0, sizeof cl);
    for (int i = 0; i < MAXC; i++) cl[i].s = -1;
    long last_sweep = uptime_ms();
    for (;;) {
        bool busy = false;
        struct itx_sockaddr peer;
        int c = sock_accept(ls, &peer, 0);
        if (c >= 0) {
            int k = 0;
            while (k < MAXC && cl[k].s >= 0) k++;
            if (k == MAXC) { sock_send_all(c, "-ERR too many clients\r\n", 23); sock_close(c); }
            else {
                cl[k].s = c;
                cl[k].auth = false;
                cl[k].in.n = 0;
                ip_str(cl[k].ip, sizeof cl[k].ip, peer.ip);
                printf("%s connected\n", cl[k].ip);
                flush();
            }
            busy = true;
        }
        for (int k = 0; k < MAXC; k++) {
            if (cl[k].s < 0) continue;
            char buf[16384];
            long got = sock_recv(cl[k].s, buf, sizeof buf, 0);
            if (got == -EAGAIN) continue;
            if (got <= 0) {
                printf("%s left\n", cl[k].ip);
                flush();
                sock_close(cl[k].s);
                cl[k].s = -1;
                kb_free(&cl[k].in);
                continue;
            }
            busy = true;
            kb_put(&cl[k].in, buf, (size_t)got);
            size_t at = 0;
            struct kbuf out = { 0 };
            bool quit = false;
            for (;;) {
                int argc;
                char **argv;
                size_t *lens;
                long used = resp_parse(cl[k].in.p + at, cl[k].in.n - at, &argc, &argv, &lens);
                if (used == 0) break;
                if (used < 0) { kb_str(&out, "-ERR protocol error\r\n"); quit = true; break; }
                at += (size_t)used;
                if (!argc) { resp_free(argc, argv, lens); continue; }
                if (!strcasecmp(argv[0], "AUTH")) {
                    const char *pw = argc > 2 ? argv[2] : argc > 1 ? argv[1] : "";
                    cl[k].auth = !strcmp(pw, password);
                    kb_str(&out, cl[k].auth ? "+OK\r\n" : "-WRONGPASS invalid password\r\n");
                } else if (!strcasecmp(argv[0], "QUIT")) {
                    kb_str(&out, "+OK\r\n");
                    quit = true;
                } else if (!cl[k].auth && strcasecmp(argv[0], "PING") && strcasecmp(argv[0], "HELLO") && strcasecmp(argv[0], "COMMAND")) {
                    kb_str(&out, "-NOAUTH Authentication required.\r\n");
                } else if (!strcasecmp(argv[0], "HELLO")) {
                    kb_str(&out, "-NOPROTO this server speaks RESP2\r\n");
                } else {
                    kv_exec(db, argc, argv, lens, &out);
                }
                resp_free(argc, argv, lens);
                if (quit) break;
            }
            if (at) { memmove(cl[k].in.p, cl[k].in.p + at, cl[k].in.n - at); cl[k].in.n -= at; }
            if (out.n) sock_send_all(cl[k].s, out.p, out.n);
            kb_free(&out);
            if (quit) { sock_close(cl[k].s); cl[k].s = -1; kb_free(&cl[k].in); }
        }
        if (uptime_ms() - last_sweep > 1000) { kv_expire_some(db); last_sweep = uptime_ms(); }
        if (!busy) sleep_ms(10);
    }
}

int main(int argc, char **argv) {
    char path[ITX_PATH_MAX], err[256];
    bool server = argc > 1 && !strcmp(argv[1], "serve");
    int base = server ? 2 : 1;
    const char *name = argc > base ? argv[base] : "main";
    path_of(name, path, sizeof path);
    struct kvdb *db = kv_open(path, err, sizeof err);
    if (!db) { printf("kv: %s\n", err); return 1; }
    int r;
    if (server) {
        int port = argc > base + 1 ? atoi(argv[base + 1]) : 6379;
        char pw[40];
        if (argc > base + 2) strlcpy(pw, argv[base + 2], sizeof pw);
        else {
            /* a password made here: 12 letters and digits from the random source */
            uint8_t b[12];
            k_random(b, sizeof b);
            static const char al[] = "abcdefghjkmnpqrstuvwxyz23456789";
            for (int i = 0; i < 12; i++) pw[i] = al[b[i] % (sizeof al - 1)];
            pw[12] = 0;
        }
        r = serve(db, name, port, pw);
    } else if (argc > 2) {
        /* one command: kv DB SET k v */
        size_t *lens = malloc(sizeof(size_t) * (size_t)argc);
        for (int i = 2; i < argc; i++) lens[i] = strlen(argv[i]);
        struct kbuf out = { 0 };
        kv_exec(db, argc - 2, argv + 2, lens + 2, &out);
        show(&out);
        r = out.n && out.p[0] == '-';
        kb_free(&out);
        free(lens);
    } else {
        r = shell(db, name);
    }
    kv_close(db);
    return r;
}
