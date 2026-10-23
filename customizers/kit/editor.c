/* kit: a full-screen text editor (kit.h).
 *
 * The text is kept as code points; it is wrapped into rows the width of
 * the screen at spaces, and each row is written as a line of its own, so
 * the terminal lays out every row by its first letter: Arabic rows run
 * right to left and are right aligned, as in a book. The terminal has no
 * cursor: the character under the cursor is drawn in yellow (a yellow
 * mark at the end of a row or on a space).
 *
 *   letters, Enter, Backspace, Delete    write
 *   arrows, Home, End, PgUp, PgDn        move (left and right follow the
 *                                        direction of the paragraph)
 *   Ctrl+S save   Esc leave (asks when there are changes)
 *   Ctrl+Z undo   Ctrl+T the time   Alt+Shift Arabic / English
 */
#include "kit.h"

struct ed {
    uint32_t *t;
    int len, cap, cur;
    int want_col;                /* the column up and down keep to */
    int top;                     /* first row shown */
    bool changed;
    /* undo: whole copies of the text, taken before a change */
    struct { uint32_t *t; int len, cur; } undo[40];
    int nundo;
    long last_snap;
    int last_kind;
};

struct row { int start, end; bool para_end; };

static struct row *rows;
static int nrows, caprows;

static void reserve(struct ed *e, int n) {
    if (n <= e->cap) return;
    int cap = e->cap ? e->cap : 256;
    while (cap < n) cap *= 2;
    uint32_t *t = realloc(e->t, sizeof(uint32_t) * cap);
    if (!t) { dprintf(2, "out of memory\n"); exit(1); }
    e->t = t;
    e->cap = cap;
}

static int cells(uint32_t c) {
    if ((c >= 0x0300 && c <= 0x036F) || (c >= 0x064B && c <= 0x065F) || c == 0x0670 || (c >= 0x200B && c <= 0x200F)) return 0;
    if ((c >= 0x1100 && c <= 0x115F) || (c >= 0x2E80 && c <= 0xA4CF) || (c >= 0xAC00 && c <= 0xD7A3) ||
        (c >= 0xF900 && c <= 0xFAFF) || (c >= 0xFF00 && c <= 0xFF60)) return 2;
    return 1;
}

static void add_row(int s, int e, bool pe) {
    if (nrows == caprows) {
        caprows = caprows ? caprows * 2 : 256;
        rows = realloc(rows, sizeof *rows * caprows);
        if (!rows) { dprintf(2, "out of memory\n"); exit(1); }
    }
    rows[nrows].start = s;
    rows[nrows].end = e;
    rows[nrows].para_end = pe;
    nrows++;
}

/* rows of at most width cells, broken after a space where there is one */
static void wrap(struct ed *e, int width) {
    nrows = 0;
    int s = 0;
    while (s <= e->len) {
        int w = 0, i = s, brk = -1;
        while (i < e->len && e->t[i] != '\n') {
            int c = cells(e->t[i]);
            if (w + c > width) break;
            w += c;
            if (e->t[i] == ' ') brk = i + 1;
            i++;
        }
        if (i >= e->len || e->t[i] == '\n') {
            add_row(s, i, true);
            s = i + 1;
            if (i >= e->len) break;
            continue;
        }
        if (brk > s) i = brk;
        if (i == s) i = s + 1;                          /* one character wider than the screen */
        add_row(s, i, false);
        s = i;
    }
    if (!nrows) add_row(0, 0, true);
}

static int row_of(int pos) {
    for (int r = 0; r < nrows; r++) {
        if (pos < rows[r].end) return r;
        if (pos == rows[r].end && (rows[r].para_end || r == nrows - 1)) return r;
    }
    return nrows - 1;
}

static int col_of(struct ed *e, int r, int pos) {
    int w = 0;
    for (int i = rows[r].start; i < pos && i < rows[r].end; i++) w += cells(e->t[i]);
    return w;
}

static int pos_at(struct ed *e, int r, int col) {
    int w = 0, i = rows[r].start;
    while (i < rows[r].end) {
        int c = cells(e->t[i]);
        if (w + c > col) break;
        w += c;
        i++;
    }
    if (i == rows[r].end && !rows[r].para_end && i > rows[r].start) i--;   /* stay on this row */
    return i;
}

/* is the paragraph around pos right to left (its first strong letter)? */
static bool para_rtl(struct ed *e, int pos) {
    int s = pos;
    while (s > 0 && e->t[s - 1] != '\n') s--;
    for (int i = s; i < e->len && e->t[i] != '\n'; i++) {
        uint32_t c = e->t[i];
        if ((c >= 0x0590 && c <= 0x08FF) || (c >= 0xFB1D && c <= 0xFDFF) || (c >= 0xFE70 && c <= 0xFEFF)) return true;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= 0xC0 && c < 0x590)) return false;
    }
    return false;
}

static void snapshot(struct ed *e, int kind) {
    long now = uptime_ms();
    if (kind == e->last_kind && kind == 1 && now - e->last_snap < 1500) return;   /* typing goes together */
    e->last_kind = kind;
    e->last_snap = now;
    if (e->nundo == 40) {
        free(e->undo[0].t);
        memmove(&e->undo[0], &e->undo[1], sizeof e->undo[0] * 39);
        e->nundo--;
    }
    uint32_t *c = malloc(sizeof(uint32_t) * (e->len ? e->len : 1));
    if (!c) return;
    memcpy(c, e->t, sizeof(uint32_t) * e->len);
    e->undo[e->nundo].t = c;
    e->undo[e->nundo].len = e->len;
    e->undo[e->nundo].cur = e->cur;
    e->nundo++;
}

static void insert(struct ed *e, uint32_t c) {
    reserve(e, e->len + 1);
    memmove(&e->t[e->cur + 1], &e->t[e->cur], sizeof(uint32_t) * (e->len - e->cur));
    e->t[e->cur++] = c;
    e->len++;
    e->changed = true;
}

static void insert_str(struct ed *e, const char *s) {
    while (*s) {
        uint32_t cp;
        int n = utf8_decode(s, 4, &cp);
        if (n <= 0) { s++; continue; }
        insert(e, cp);
        s += n;
    }
}

static char *to_utf8(struct ed *e) {
    char *o = malloc((size_t)e->len * 4 + 1);
    if (!o) return NULL;
    size_t n = 0;
    for (int i = 0; i < e->len; i++) n += (size_t)utf8_encode(e->t[i], o + n);
    o[n] = 0;
    return o;
}

int k_words(const char *text) {
    int n = 0;
    bool in = false;
    for (const char *p = text; p && *p; p++) {
        bool sp = *p == ' ' || *p == '\n' || *p == '\t';
        if (!sp && !in) n++;
        in = !sp;
    }
    return n;
}

static int count_words(struct ed *e) {
    int n = 0;
    bool in = false;
    for (int i = 0; i < e->len; i++) {
        bool sp = e->t[i] == ' ' || e->t[i] == '\n' || e->t[i] == '\t';
        if (!sp && !in) n++;
        in = !sp;
    }
    return n;
}

static void draw(struct ed *e, const char *title, const char *info, int width, int view) {
    char right[96];
    snprintf(right, sizeof right, "%s%s%d words%s", info ? info : "", info ? "   " : "", count_words(e), e->changed ? "  *" : "");
    k_frame();
    k_header(title, right);
    int cr = row_of(e->cur);
    if (cr < e->top) e->top = cr;
    if (cr >= e->top + view) e->top = cr - view + 1;
    for (int r = e->top; r < nrows && r < e->top + view; r++) {
        char line[4096];
        size_t o = 0;
        o += (size_t)snprintf(line, sizeof line, "  ");
        for (int i = rows[r].start; i <= rows[r].end && o < sizeof line - 40; i++) {
            bool at = i == e->cur;
            if (i == rows[r].end) {
                if (at && r == cr) o += (size_t)snprintf(line + o, sizeof line - o, K_YEL "\xe2\x96\x81" K_RESET);
                break;
            }
            uint32_t c = e->t[i];
            if (at && r == cr) {
                if (c == ' ') o += (size_t)snprintf(line + o, sizeof line - o, K_YEL "\xe2\x96\x81" K_RESET);
                else {
                    o += (size_t)snprintf(line + o, sizeof line - o, K_YEL);
                    o += (size_t)utf8_encode(c, line + o);
                    o += (size_t)snprintf(line + o, sizeof line - o, K_RESET);
                }
            } else {
                o += (size_t)utf8_encode(c, line + o);
            }
        }
        line[o] = 0;
        k_text_row(line);
    }
    char pos[64];
    snprintf(pos, sizeof pos, "row %d/%d", cr + 1, nrows);
    char keys[256];
    snprintf(keys, sizeof keys, "%s   Ctrl+S save   Esc leave   Ctrl+Z undo   Ctrl+T time   Alt+Shift Arabic/English", pos);
    (void)width;
    k_footer(keys);
    k_show();
}

bool k_edit(char **text, const char *title, const char *info) {
    struct ed e;
    memset(&e, 0, sizeof e);
    e.last_kind = -1;
    reserve(&e, 256);
    if (*text) {
        const char *s = *text;
        while (*s) {
            uint32_t cp;
            int n = utf8_decode(s, 4, &cp);
            if (n <= 0) { s++; continue; }
            if (cp == '\r') { s += n; continue; }
            if (cp == '\t') cp = ' ';
            reserve(&e, e.len + 1);
            e.t[e.len++] = cp;
            s += n;
        }
    }
    e.cur = e.len;
    bool saved = false, done = false;
    k_begin();
    while (!done) {
        k_frame();                                       /* reads the screen size */
        int width = k_cols - 5, view = k_rows - 3;
        if (width < 10) width = 10;
        wrap(&e, width);
        draw(&e, title, info, width, view);
        int k = k_key();
        int r = row_of(e.cur);
        bool rtl = para_rtl(&e, e.cur);
        if (k == KEY_LEFT || k == KEY_RIGHT) {
            bool back = (k == KEY_LEFT) != rtl;
            if (back && e.cur > 0) e.cur--;
            else if (!back && e.cur < e.len) e.cur++;
            e.want_col = -1;
        } else if (k == KEY_UP || k == KEY_DOWN || k == KEY_PGUP || k == KEY_PGDN) {
            if (e.want_col < 0) e.want_col = col_of(&e, r, e.cur);
            int step = k == KEY_UP ? -1 : k == KEY_DOWN ? 1 : k == KEY_PGUP ? -view : view;
            int nr = r + step;
            if (nr < 0) nr = 0;
            if (nr > nrows - 1) nr = nrows - 1;
            e.cur = pos_at(&e, nr, e.want_col);
            continue;
        } else if (k == KEY_HOME) {
            e.cur = rows[r].start;
        } else if (k == KEY_END) {
            e.cur = rows[r].end;
            if (!rows[r].para_end && e.cur > rows[r].start) e.cur--;
        } else if (k == K_BS) {
            if (e.cur > 0) {
                snapshot(&e, 2);
                memmove(&e.t[e.cur - 1], &e.t[e.cur], sizeof(uint32_t) * (e.len - e.cur));
                e.cur--;
                e.len--;
                e.changed = true;
            }
        } else if (k == KEY_DELETE) {
            if (e.cur < e.len) {
                snapshot(&e, 2);
                memmove(&e.t[e.cur], &e.t[e.cur + 1], sizeof(uint32_t) * (e.len - e.cur - 1));
                e.len--;
                e.changed = true;
            }
        } else if (k == K_ENTER) {
            snapshot(&e, 3);
            insert(&e, '\n');
        } else if (k == K_TAB) {
            snapshot(&e, 1);
            insert(&e, ' ');
            insert(&e, ' ');
        } else if (k == K_CTRL('s')) {
            saved = true;
            done = true;
        } else if (k == K_CTRL('z')) {
            if (e.nundo) {
                e.nundo--;
                free(e.t);
                e.t = e.undo[e.nundo].t;
                e.len = e.undo[e.nundo].len;
                e.cap = e.len ? e.len : 1;
                e.cur = e.undo[e.nundo].cur;
                e.changed = true;
                e.last_kind = -1;
            } else {
                k_flash("nothing to undo");
            }
        } else if (k == K_CTRL('t')) {
            char tm[16], s[24];
            snprintf(s, sizeof s, "%s ", k_fmt_time(tm, sizeof tm, k_minute_of_day()));
            snapshot(&e, 3);
            insert_str(&e, s);
        } else if (k == K_ESC) {
            if (!e.changed) { done = true; break; }
            k_frame();
            wrap(&e, width);
            k_flash("there are changes:  s save   d throw them away   any other key: go on writing");
            draw(&e, title, info, width, view);
            int a = k_key();
            if (a == 's' || a == 'S' || a == 0x0633) { saved = true; done = true; }
            else if (a == 'd' || a == 'D' || a == 0x064A) done = true;
        } else if (k >= 0x20 && k < 0x110000 && k != 0x7F) {
            snapshot(&e, 1);
            insert(&e, (uint32_t)k);
        }
        if (k != KEY_UP && k != KEY_DOWN) e.want_col = -1;
    }
    for (int i = 0; i < e.nundo; i++) free(e.undo[i].t);
    if (saved) {
        char *n = to_utf8(&e);
        if (n) { free(*text); *text = n; }
        else saved = false;
    }
    free(e.t);
    return saved;
}
