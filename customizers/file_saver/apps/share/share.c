/* share: a folder to the network, read only, over HTTP: another computer
 * opens http://<this computer's address>:<port>/ in a browser and
 * downloads files. Folders whose name starts with a dot (the vault's own)
 * are not shown. Ctrl+C stops it.
 *
 *   share [FOLDER] [PORT]      (in File saver's terminal; 8080 by default)
 */
#include <libnet.h>
#include <kit.h>

static char root[ITX_PATH_MAX];

static void url_decode(char *s) {
    char *o = s;
    for (; *s; s++) {
        if (*s == '%' && isxdigit((unsigned char)s[1]) && isxdigit((unsigned char)s[2])) {
            int a = isdigit(s[1]) ? s[1] - '0' : (s[1] | 32) - 'a' + 10, b = isdigit(s[2]) ? s[2] - '0' : (s[2] | 32) - 'a' + 10;
            *o++ = (char)(a << 4 | b);
            s += 2;
        } else if (*s == '+') *o++ = ' ';
        else *o++ = *s;
    }
    *o = 0;
}

static void url_encode(char *out, size_t len, const char *s) {
    static const char hex[] = "0123456789ABCDEF";
    size_t o = 0;
    for (; *s && o + 4 < len; s++) {
        unsigned char c = (unsigned char)*s;
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c == '/') out[o++] = (char)c;
        else { out[o++] = '%'; out[o++] = hex[c >> 4]; out[o++] = hex[c & 15]; }
    }
    out[o] = 0;
}

static void html_escape(char *out, size_t len, const char *s) {
    size_t o = 0;
    for (; *s && o + 7 < len; s++) {
        const char *r = *s == '<' ? "&lt;" : *s == '>' ? "&gt;" : *s == '&' ? "&amp;" : *s == '"' ? "&quot;" : NULL;
        if (r) { size_t l = strlen(r); memcpy(out + o, r, l); o += l; }
        else out[o++] = *s;
    }
    out[o] = 0;
}

static const char *mime(const char *name) {
    static const struct { const char *ext, *type; } m[] = {
        { ".html", "text/html; charset=utf-8" }, { ".htm", "text/html; charset=utf-8" }, { ".txt", "text/plain; charset=utf-8" },
        { ".md", "text/plain; charset=utf-8" }, { ".csv", "text/csv; charset=utf-8" }, { ".json", "application/json" },
        { ".png", "image/png" }, { ".jpg", "image/jpeg" }, { ".jpeg", "image/jpeg" }, { ".gif", "image/gif" },
        { ".pdf", "application/pdf" }, { ".mp3", "audio/mpeg" }, { ".mp4", "video/mp4" }, { ".zip", "application/zip" },
    };
    for (size_t i = 0; i < sizeof m / sizeof m[0]; i++) if (ends_with(name, m[i].ext)) return m[i].type;
    return "application/octet-stream";
}

static void reply(int c, int code, const char *what, const char *type, const char *body) {
    char h[512];
    int n = snprintf(h, sizeof h, "HTTP/1.0 %d %s\r\nContent-Type: %s\r\nContent-Length: %lu\r\nServer: itelx-share\r\nConnection: close\r\n\r\n",
                     code, what, type, (unsigned long)strlen(body));
    sock_send_all(c, h, (size_t)n);
    sock_send_all(c, body, strlen(body));
}

static long send_dir(int c, const char *path, const char *url) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) { reply(c, 404, "Not Found", "text/plain", "not found\n"); return -1; }
    size_t cap = 16384, o = 0;
    char *b = malloc(cap), esc[1024], enc[1024];
    html_escape(esc, sizeof esc, url);
    o += (size_t)snprintf(b + o, cap - o, "<!doctype html><meta charset=utf-8><title>%s</title><style>body{font:15px system-ui,sans-serif;"
                          "margin:2em}a{text-decoration:none}td{padding:2px 14px 2px 0}</style><h2>itelx File saver: %s</h2><table>", esc, esc);
    if (strcmp(url, "/")) o += (size_t)snprintf(b + o, cap - o, "<tr><td><a href=\"..\">..</a></td></tr>");
    struct itx_dirent de;
    while (readdir(fd, &de) == 1) {
        if (de.name[0] == '.') continue;
        if (o + 2048 > cap) { cap *= 2; b = realloc(b, cap); }
        char h[32] = "";
        html_escape(esc, sizeof esc, de.name);
        url_encode(enc, sizeof enc, de.name);
        if (de.type != VT_DIR) {
            if (de.size < 1024) snprintf(h, sizeof h, "%lu B", (unsigned long)de.size);
            else if (de.size < 1048576) snprintf(h, sizeof h, "%lu KiB", (unsigned long)(de.size / 1024));
            else snprintf(h, sizeof h, "%lu MiB", (unsigned long)(de.size >> 20));
        }
        o += (size_t)snprintf(b + o, cap - o, "<tr><td><a href=\"%s%s\">%s%s</a></td><td>%s</td></tr>", enc, de.type == VT_DIR ? "/" : "",
                              esc, de.type == VT_DIR ? "/" : "", h);
    }
    close(fd);
    o += (size_t)snprintf(b + o, cap - o, "</table>");
    reply(c, 200, "OK", "text/html; charset=utf-8", b);
    long n = (long)o;
    free(b);
    return n;
}

static long send_file(int c, const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) { reply(c, 404, "Not Found", "text/plain", "not found\n"); return -1; }
    struct itx_stat st;
    fstat(fd, &st);
    char h[512];
    int n = snprintf(h, sizeof h, "HTTP/1.0 200 OK\r\nContent-Type: %s\r\nContent-Length: %lu\r\nServer: itelx-share\r\nConnection: close\r\n\r\n",
                     mime(path), (unsigned long)st.size);
    if (!sock_send_all(c, h, (size_t)n)) { close(fd); return -1; }
    static char buf[32768];
    long total = 0;
    for (;;) {
        ssize_t k = read(fd, buf, sizeof buf);
        if (k <= 0) break;
        if (!sock_send_all(c, buf, (size_t)k)) break;
        total += k;
    }
    close(fd);
    return total;
}

static void serve(int c, const struct itx_sockaddr *peer) {
    char req[8192];
    size_t have = 0;
    while (have < sizeof req - 1) {
        long k = sock_recv(c, req + have, sizeof req - 1 - have, 10000);
        if (k <= 0) return;
        have += (size_t)k;
        req[have] = 0;
        if (strstr(req, "\r\n\r\n")) break;
    }
    char method[8] = "", url[2048] = "";
    char *sp = strchr(req, ' '), *sp2 = sp ? strchr(sp + 1, ' ') : NULL;
    if (!sp || !sp2 || sp - req >= (long)sizeof method || sp2 - sp - 1 >= (long)sizeof url) { reply(c, 400, "Bad Request", "text/plain", "bad request\n"); return; }
    memcpy(method, req, (size_t)(sp - req));
    memcpy(url, sp + 1, (size_t)(sp2 - sp - 1));
    char *q = strchr(url, '?');
    if (q) *q = 0;
    url_decode(url);
    char ip[20];
    ip_str(ip, sizeof ip, peer->ip);
    if (strcmp(method, "GET") && strcmp(method, "HEAD")) { reply(c, 405, "Method Not Allowed", "text/plain", "read only\n"); return; }
    /* nothing outside the shared folder, nothing hidden */
    if (url[0] != '/' || strstr(url, "/..") || strstr(url, "/.")) {
        reply(c, 403, "Forbidden", "text/plain", "not shared\n");
        printf("%s GET %s 403\n", ip, url);
        return;
    }
    char path[ITX_PATH_MAX];
    snprintf(path, sizeof path, "%s%s", strcmp(root, "/") ? root : "", url);
    size_t pl = strlen(path);
    while (pl > 1 && path[pl - 1] == '/') path[--pl] = 0;
    struct itx_stat st;
    long n;
    if (stat(path, &st)) { reply(c, 404, "Not Found", "text/plain", "not found\n"); n = -1; }
    else if (st.type == VT_DIR) {
        if (url[strlen(url) - 1] != '/') {
            char loc[2200], h[2400];
            url_encode(loc, sizeof loc, url);
            int k = snprintf(h, sizeof h, "HTTP/1.0 301 Moved\r\nLocation: %s/\r\nContent-Length: 0\r\nConnection: close\r\n\r\n", loc);
            sock_send_all(c, h, (size_t)k);
            return;
        }
        n = send_dir(c, path, url);
    } else n = send_file(c, path);
    printf("%s GET %s %s %ld bytes\n", ip, url, n >= 0 ? "200" : "404", n < 0 ? 0 : n);
    flush();
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : "/vault";
    int port = argc > 2 ? atoi(argv[2]) : 8080;
    if (dir[0] != '/') {
        char cwd[ITX_PATH_MAX];
        getcwd(cwd, sizeof cwd);
        snprintf(root, sizeof root, "%s/%s", strcmp(cwd, "/") ? cwd : "", dir);
    } else strlcpy(root, dir, sizeof root);
    struct itx_stat st;
    if (stat(root, &st) || st.type != VT_DIR) { printf("%s is not a folder\n", root); return 1; }
    if (port < 1 || port > 65535) { printf("not a port: %d\n", port); return 1; }
    int s = sock_open(SOCK_TCP);
    if (s < 0) { printf("share: %s\n", net_error(s)); return 1; }
    int r = sock_bind(s, port);
    if (r < 0) { printf("share: port %d: %s\n", port, net_error(r)); return 1; }
    sock_listen(s, 8);
    if (!net_ready(8000)) printf("(no address yet: see net)\n");
    struct itx_netinfo ni[4];
    int n = net_info(ni, 4);
    char ip[20] = "?";
    for (int i = 0; i < n; i++) if (ni[i].flags & NETF_UP) { ip_str(ip, sizeof ip, ni[i].ip); break; }
    printf("sharing %s, read only, at \x1b[93mhttp://%s:%d/\x1b[0m\n"
           "Open that address in a browser on another computer of the network. Ctrl+C stops it.\n", root, ip, port);
    flush();
    for (;;) {
        struct itx_sockaddr peer;
        int c = sock_accept(s, &peer, 60000);
        if (c == -EAGAIN) continue;
        if (c < 0) { printf("share: %s\n", net_error(c)); sleep_ms(500); continue; }
        serve(c, &peer);
        sock_close(c);
    }
}
