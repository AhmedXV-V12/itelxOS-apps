/* kit: frames, text width, keys, questions and lists (kit.h). */
#include "kit.h"

int k_cols = 80, k_rows = 24;

static char *fb;
static size_t fb_len, fb_cap;
static int used;
static bool raw;
static char flash[256];

/* A row takes its direction from its first letter (the terminal's bidi):
   rows of lists start with something Latin (a date, a word), and two
   columns that may both be Arabic have a Latin word between them, or the
   terminal would lay the two out as one right-to-left run. A row stays
   one cell narrower than the screen, so the terminal never wraps it. */

/* ---------------------------------------------------------------- keys */

void k_begin(void) {
    flush();
    tty_mode(TTY_MODE_RAW);
    raw = true;
}

void k_end(void) {
    flush();
    tty_mode(TTY_MODE_COOKED);
    raw = false;
}

static int map_key(struct itx_key *k) {
    if (k->code == 3 && (k->mods & 2)) {
        k_end();
        printf("\n[stopped]\n");
        exit(130);
    }
    if (k->code == KEY_ESC) return K_ESC;
    if (k->code == '\r') return K_ENTER;
    if (k->code == 0x7F) return K_BS;
    return (int)k->code;
}

int k_key(void) {
    struct itx_key k;
    for (;;) {
        int r = tty_key(&k);
        if (r == -EINVAL) { k_begin(); continue; }   /* raw mode was lost (another program) */
        if (r < 0) return K_ESC;
        return map_key(&k);
    }
}

int k_key_wait(long ms) {
    struct itx_key k;
    int r = tty_keywait(&k, ms);
    if (r == -EAGAIN) return K_NONE;
    if (r == -EINVAL) { k_begin(); return K_NONE; }
    if (r < 0) return K_NONE;
    return map_key(&k);
}

/* --------------------------------------------------------------- frame */

static void grow(size_t need) {
    if (fb_len + need + 1 <= fb_cap) return;
    size_t cap = fb_cap ? fb_cap : 16384;
    while (cap < fb_len + need + 1) cap *= 2;
    char *n = realloc(fb, cap);
    if (!n) { dprintf(2, "out of memory\n"); exit(1); }
    fb = n;
    fb_cap = cap;
}

void k_frame(void) {
    struct itx_ttyinfo ti;
    if (tty_info(&ti) == 0 && ti.cols >= 40 && ti.rows >= 12) {
        k_cols = (int)ti.cols;
        k_rows = (int)ti.rows;
    }
    grow(8);
    memcpy(fb, "\x1b[2J", 4);
    fb_len = 4;
    used = 0;
}

void k_put(const char *s) {
    size_t n = strlen(s);
    grow(n);
    memcpy(fb + fb_len, s, n);
    fb_len += n;
}

void k_printf(const char *fmt, ...) {
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    k_put(buf);
}

void k_line(const char *fmt, ...) {
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (used >= k_rows) return;                     /* the frame is full */
    if (used) k_put("\n");
    k_put(buf);
    k_put(K_RESET);
    used++;
}

void k_text_row(const char *s) {
    if (used >= k_rows) return;
    if (used) k_put("\n");
    k_put(s);
    k_put(K_RESET);
    used++;
}

int k_used(void) { return used; }

void k_pad_to(int row) {
    while (used < row && used < k_rows) k_line("%s", "");
}

void k_show(void) {
    flush();
    size_t done = 0;
    while (done < fb_len) {
        ssize_t n = write(1, fb + done, fb_len - done);
        if (n <= 0) break;
        done += (size_t)n;
    }
}

void k_header(const char *title, const char *right) {
    char r[96], t[256], when[64], d[32], tm[16];
    if (!right) {
        long now = k_now();
        snprintf(when, sizeof when, "%s  %s", k_fmt_day(d, sizeof d, now / 86400),
                 k_fmt_time(tm, sizeof tm, (int)(now % 86400 / 60)));
        right = when;
    }
    snprintf(r, sizeof r, "%s ", right);
    int rw = k_width(r);
    k_fit(t, sizeof t, title, k_cols - rw - 2);
    k_line(K_CYAN " %s" K_RESET K_DIM "%s" K_RESET, t, r);
    k_rule();
}

void k_rule(void) {
    char buf[1024];
    int n = k_cols - 1 < 300 ? k_cols - 1 : 300, o = 0;
    for (int i = 0; i < n && o < (int)sizeof buf - 4; i++) o += snprintf(buf + o, sizeof buf - o, "\xe2\x94\x80");
    k_line(K_DIM "%s", buf);
}

void k_flash(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(flash, sizeof flash, fmt, ap);
    va_end(ap);
}

void k_footer(const char *keys) {
    k_pad_to(k_rows - 1);
    char a[512], b[512];
    if (flash[0]) {
        int w = k_width(flash);
        if (w > k_cols - 3) w = k_cols - 3;
        k_fit(a, sizeof a, flash, w);
        int rest = k_cols - 4 - w;
        if (rest > 8) k_fit(b, sizeof b, keys, rest);
        else b[0] = 0;
        k_line(K_YEL " %s " K_RESET K_DIM "%s", a, b);
        flash[0] = 0;
    } else {
        k_fit(a, sizeof a, keys, k_cols - 2);
        k_line(K_DIM " %s", a);
    }
}

/* -------------------------------------------------------------- widths */

static bool wide(uint32_t c) {
    return (c >= 0x1100 && c <= 0x115F) || (c >= 0x2E80 && c <= 0xA4CF) || (c >= 0xAC00 && c <= 0xD7A3) ||
           (c >= 0xF900 && c <= 0xFAFF) || (c >= 0xFF00 && c <= 0xFF60);
}

static bool zero(uint32_t c) {
    return (c >= 0x0300 && c <= 0x036F) || (c >= 0x064B && c <= 0x065F) || c == 0x0670 ||
           (c >= 0x06D6 && c <= 0x06ED) || (c >= 0x200B && c <= 0x200F) || (c >= 0x202A && c <= 0x202E) ||
           (c >= 0x2066 && c <= 0x2069) || c == 0xFEFF;
}

/* the next character of s: its length in bytes, its code point, its cells */
static int next_char(const char *s, uint32_t *cp, int *cells) {
    if (s[0] == 0x1B && s[1] == '[') {
        int i = 2;
        while (s[i] && !((s[i] >= 'A' && s[i] <= 'Z') || (s[i] >= 'a' && s[i] <= 'z'))) i++;
        if (s[i]) i++;
        *cp = 0x1B;
        *cells = 0;
        return i;
    }
    int n = utf8_decode(s, 4, cp);
    if (n <= 0) { *cp = (unsigned char)s[0]; n = 1; }
    *cells = zero(*cp) ? 0 : wide(*cp) ? 2 : 1;
    return n;
}

int k_width(const char *s) {
    int w = 0;
    while (*s) {
        uint32_t cp;
        int c;
        s += next_char(s, &cp, &c);
        w += c;
    }
    return w;
}

char *k_fit(char *out, size_t len, const char *s, int cells) {
    size_t o = 0;
    int w = 0;
    bool cut = false;
    if (cells < 0) cells = 0;
    int total = k_width(s);
    while (*s) {
        uint32_t cp;
        int c, n = next_char(s, &cp, &c);
        if (cp == '\n' || cp == '\t') { cp = ' '; }
        if (total > cells && w + c > cells - 1) { cut = true; break; }
        if (w + c > cells) { cut = true; break; }
        if (o + n + 8 >= len) break;
        if (*s == '\n' || *s == '\t') out[o++] = ' ';
        else { memcpy(out + o, s, n); o += n; }
        w += c;
        s += n;
    }
    if (cut && w < cells && o + 4 < len) {
        memcpy(out + o, "\xe2\x80\xa6", 3);
        o += 3;
        w++;
    }
    while (w < cells && o + 1 < len) { out[o++] = ' '; w++; }
    out[o] = 0;
    return out;
}

char *k_bar(char *out, size_t len, long value, long max, int cells) {
    static const char *const part[] = { "", "\xe2\x96\x8f", "\xe2\x96\x8e", "\xe2\x96\x8d", "\xe2\x96\x8c",
                                        "\xe2\x96\x8b", "\xe2\x96\x8a", "\xe2\x96\x89" };
    size_t o = 0;
    if (max <= 0) max = 1;
    if (value < 0) value = 0;
    if (value > max) value = max;
    long eighths = value * cells * 8 / max;
    int w = 0;
    for (; eighths >= 8 && o + 4 < len; eighths -= 8, w++) { memcpy(out + o, "\xe2\x96\x88", 3); o += 3; }
    if (eighths > 0 && o + 4 < len) { memcpy(out + o, part[eighths], 3); o += 3; w++; }
    while (w < cells && o + 1 < len) { out[o++] = ' '; w++; }
    out[o] = 0;
    return out;
}

bool k_has_rtl(const char *s) {
    while (*s) {
        uint32_t cp;
        int n = utf8_decode(s, 4, &cp);
        if (n <= 0) { s++; continue; }
        if ((cp >= 0x0590 && cp <= 0x08FF) || (cp >= 0xFB1D && cp <= 0xFDFF) || (cp >= 0xFE70 && cp <= 0xFEFF)) return true;
        s += n;
    }
    return false;
}

int k_wrap(const char *text, int width, char ***out) {
    int n = 0, cap = 32;
    char **rows = malloc(sizeof(char *) * cap);
    if (width < 4) width = 4;
    const char *p = text ? text : "";
    for (;;) {
        /* one row: up to width cells, ended at the last space that fits */
        const char *s = p, *q = p, *brk = NULL;
        int w = 0;
        while (*q && *q != '\n') {
            uint32_t cp;
            int c, len = next_char(q, &cp, &c);
            if (w + c > width) break;
            w += c;
            q += len;
            if (cp == ' ') brk = q;
        }
        const char *end = q;
        if (*q && *q != '\n' && brk) end = brk;
        if (end == s && *q && *q != '\n') {          /* nothing fits: take one character */
            uint32_t cp;
            int c;
            end = s + next_char(s, &cp, &c);
        }
        if (n == cap) { cap *= 2; rows = realloc(rows, sizeof(char *) * cap); }
        size_t l = (size_t)(end - s);
        rows[n] = malloc(l + 1);
        memcpy(rows[n], s, l);
        rows[n][l] = 0;
        n++;
        p = end;
        if (*p == '\n') {
            p++;
            if (!*p) break;                          /* a newline at the very end adds no row */
        } else if (!*p) {
            break;
        }
    }
    *out = rows;
    return n;
}

void k_wrap_free(char **rows, int n) {
    for (int i = 0; i < n; i++) free(rows[i]);
    free(rows);
}

/* ----------------------------------------------------------- questions */

bool k_ask(const char *prompt, char *buf, size_t len) {
    bool was = raw;
    k_show();
    /* the question takes the place of the bottom row */
    printf("\r\x1b[K" K_YEL "%s" K_RESET, prompt);
    if (was) k_end();
    flush();
    ssize_t n = read(0, buf, len - 1);
    if (was) k_begin();
    if (n <= 0) { buf[0] = 0; return false; }
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) n--;
    buf[n] = 0;
    /* spaces at both ends go */
    size_t s = 0;
    while (buf[s] == ' ') s++;
    if (s) memmove(buf, buf + s, strlen(buf + s) + 1);
    n = (ssize_t)strlen(buf);
    while (n > 0 && buf[n - 1] == ' ') buf[--n] = 0;
    return n > 0;
}

bool k_ask_long(const char *prompt, long *v) {
    char b[32];
    if (!k_ask(prompt, b, sizeof b)) return false;
    char *end;
    long x = strtol(b, &end, 10);
    if (end == b || *end) { k_flash("not a number: %s", b); return false; }
    *v = x;
    return true;
}

bool k_confirm(const char *question) {
    k_show();
    printf("\r\x1b[K" K_YEL "%s (y/n)" K_RESET, question);
    flush();
    for (;;) {
        int k = k_key();
        /* y, or the key under y in the Arabic layout */
        if (k == 'y' || k == 'Y' || k == 0x063A) return true;
        if (k == 'n' || k == 'N' || k == 0x0649 || k == K_ESC || k == K_ENTER) return false;
    }
}

int k_menu(const char *title, const char *const *items, int n, int sel) {
    struct k_list l = { sel < 0 ? 0 : sel, 0, n };
    for (;;) {
        k_frame();
        k_header(title, "");
        int rows = k_rows - 4;
        k_list_fit(&l, rows);
        for (int i = l.top; i < n && i < l.top + rows; i++) {
            char t[512];
            k_fit(t, sizeof t, items[i], k_cols - 6);
            if (i == l.sel) k_line(K_YEL " \xe2\x96\xb6 %s", t);
            else k_line("   %s", t);
        }
        k_footer("arrows choose   Enter take   Esc back");
        k_show();
        int k = k_key();
        if (k_list_key(&l, k, rows)) continue;
        if (k == K_ENTER) return l.sel;
        if (k == K_ESC || k == 'q') return -1;
        if (k >= '1' && k <= '9' && k - '1' < n) return k - '1';
    }
}

void k_message(const char *title, const char *text) {
    int top = 0;
    for (;;) {
        k_frame();
        k_header(title, "");
        int rows = k_rows - 4, line = 0;
        const char *p = text;
        while (*p) {
            const char *e = strchr(p, '\n');
            size_t n = e ? (size_t)(e - p) : strlen(p);
            if (line >= top && line < top + rows) {
                char buf[1024], t[1024];
                if (n >= sizeof buf) n = sizeof buf - 1;
                memcpy(buf, p, n);
                buf[n] = 0;
                k_fit(t, sizeof t, buf, k_cols - 2);
                k_line(" %s", t);
            }
            line++;
            p = e ? e + 1 : p + n;
        }
        k_footer(line > rows ? "arrows scroll   any other key: back" : "any key: back");
        k_show();
        int k = k_key();
        if (k == KEY_DOWN && top + rows < line) top++;
        else if (k == KEY_UP && top > 0) top--;
        else if (k == KEY_PGDN) top = top + rows < line - rows ? top + rows : (line > rows ? line - rows : 0);
        else if (k == KEY_PGUP) top = top > rows ? top - rows : 0;
        else return;
    }
}

/* --------------------------------------------------------------- lists */

bool k_list_key(struct k_list *l, int key, int page) {
    int old = l->sel;
    if (l->n <= 0) { l->sel = 0; return key == KEY_UP || key == KEY_DOWN; }
    switch (key) {
    case KEY_UP: if (l->sel > 0) l->sel--; break;
    case KEY_DOWN: if (l->sel < l->n - 1) l->sel++; break;
    case KEY_PGUP: l->sel -= page; if (l->sel < 0) l->sel = 0; break;
    case KEY_PGDN: l->sel += page; if (l->sel > l->n - 1) l->sel = l->n - 1; break;
    case KEY_HOME: l->sel = 0; break;
    case KEY_END: l->sel = l->n - 1; break;
    default: return false;
    }
    (void)old;
    return true;
}

void k_list_fit(struct k_list *l, int rows) {
    if (l->n <= 0) { l->sel = l->top = 0; return; }
    if (l->sel >= l->n) l->sel = l->n - 1;
    if (l->sel < 0) l->sel = 0;
    if (rows < 1) rows = 1;
    if (l->sel < l->top) l->top = l->sel;
    if (l->sel >= l->top + rows) l->top = l->sel - rows + 1;
    if (l->top > l->n - rows) l->top = l->n - rows > 0 ? l->n - rows : 0;
    if (l->top < 0) l->top = 0;
}
