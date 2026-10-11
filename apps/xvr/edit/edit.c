/* edit: a text editor for the itelx terminal, line by line.
 *
 *   edit <file>        opens the file (a new one when it does not exist)
 *
 * The text is a list of numbered lines; commands change lines by their
 * number. Lines are typed with the terminal's own line editing, Arabic
 * too (Alt+Shift). Type h for the commands:
 *
 *   l               the next page          l <n> [m]   lines n to m
 *   <n>             line n
 *   a [text]        add a line at the end (a alone: several, end with ".")
 *   i <n> [text]    insert before line n (i <n> alone: several)
 *   r <n> <text>    replace line n          d <n>[-m]   delete lines
 *   f <text>        find                    s /old/new/  replace everywhere
 *   u               undo the last change    w [file]     save
 *   q               quit (q! without saving)
 */
#include <ui.h>

#define PAGE 20
#define MAXLINE 4096

typedef struct {
    char **v;
    int n, cap;
} lines;

static lines text, undo_text;
static bool has_undo, modified, crlf, final_nl = true;
static char path[ITX_PATH_MAX];
static int cur = 1;                  /* the current line, for "l" */

static void die(void) { printf("edit: out of memory\n"); exit(1); }

static void push_line(lines *t, int at, const char *s, size_t len) {
    if (t->n == t->cap) {
        int cap = t->cap ? t->cap * 2 : 64;
        char **v = realloc(t->v, sizeof(char *) * (size_t)cap);
        if (!v) die();
        t->v = v;
        t->cap = cap;
    }
    char *c = malloc(len + 1);
    if (!c) die();
    memcpy(c, s, len);
    c[len] = 0;
    memmove(&t->v[at + 1], &t->v[at], sizeof(char *) * (size_t)(t->n - at));
    t->v[at] = c;
    t->n++;
}

static void clear_lines(lines *t) {
    for (int i = 0; i < t->n; i++) free(t->v[i]);
    t->n = 0;
}

static void copy_lines(lines *to, const lines *from) {
    clear_lines(to);
    for (int i = 0; i < from->n; i++) push_line(to, i, from->v[i], strlen(from->v[i]));
}

/* every change starts here: the text before it is kept for "u" */
static void change(void) {
    copy_lines(&undo_text, &text);
    has_undo = true;
    modified = true;
}

static void show(int n) {
    if (n < 1 || n > text.n) return;
    printf(C_DIM "%4d" C_RESET "  %s\n", n, text.v[n - 1]);
}

static void list(int from, int to) {
    if (!text.n) { printf("(the file is empty: a <text> adds a line)\n"); return; }
    if (from < 1) from = 1;
    if (to > text.n) to = text.n;
    for (int i = from; i <= to; i++) show(i);
    cur = to + 1;
    if (to < text.n) printf(C_DIM "      ... %d more line(s): l for the next page" C_RESET "\n", text.n - to);
    else printf(C_DIM "      (end, %d line(s))" C_RESET "\n", text.n);
}

static int load(void) {
    size_t len;
    char *data = read_file(path, &len, 4u << 20);
    if (!data) {
        struct itx_stat st;
        int r = stat(path, &st);
        if (r == -ENOENT) return 0;                        /* a new file */
        if (!r && st.type == VT_DIR) { printf("edit: %s is a folder\n", path); return -1; }
        printf("edit: %s: cannot read it (larger than 4 MiB?)\n", path);
        return -1;
    }
    for (size_t i = 0; i < len; i++)
        if (!data[i]) { printf("edit: %s is not a text file\n", path); free(data); return -1; }
    final_nl = len == 0 || data[len - 1] == '\n';
    size_t start = 0;
    for (size_t i = 0; i <= len; i++) {
        if (i == len || data[i] == '\n') {
            if (i == len && start == len) break;           /* after the last newline */
            size_t e = i;
            if (e > start && data[e - 1] == '\r') { e--; crlf = true; }
            push_line(&text, text.n, data + start, e - start);
            start = i + 1;
        }
    }
    free(data);
    return 1;
}

static bool save(const char *to) {
    size_t total = 0;
    for (int i = 0; i < text.n; i++) total += strlen(text.v[i]) + 2;
    char *out = malloc(total + 1);
    if (!out) die();
    size_t o = 0;
    for (int i = 0; i < text.n; i++) {
        size_t l = strlen(text.v[i]);
        memcpy(out + o, text.v[i], l);
        o += l;
        if (i + 1 < text.n || final_nl) {
            if (crlf) out[o++] = '\r';
            out[o++] = '\n';
        }
    }
    int r = write_file(to, out, o, false);
    free(out);
    if (r) { printf(C_RED "edit: %s: %s" C_RESET "\n", to, strerror(r)); return false; }
    printf("saved %s: %d line(s), %lu bytes\n", to, text.n, (unsigned long)o);
    return true;
}

/* lines typed until a line with only "."; inserted from position at */
static void type_lines(int at) {
    printf(C_DIM "type the lines; a line with only . ends" C_RESET "\n");
    bool first = true;
    char line[MAXLINE];
    for (;;) {
        printf(C_DIM "%4d" C_RESET "  ", at + 1);
        flush();
        ssize_t n = read(0, line, sizeof line - 1);
        if (n <= 0) break;
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) n--;
        line[n] = 0;
        if (!strcmp(line, ".")) break;
        if (first) { change(); first = false; }
        push_line(&text, at++, line, (size_t)n);
    }
    cur = at;
}

static bool number(const char *s, int *n) {
    char *end;
    long v = strtol(s, &end, 10);
    if (end == s) return false;
    *n = (int)v;
    return true;
}

static bool in_range(int n) {
    if (n >= 1 && n <= text.n) return true;
    if (!text.n) printf("the file is empty\n");
    else printf("there is no line %d (the lines are 1 to %d)\n", n, text.n);
    return false;
}

/* the text after the first word (and spaces) of a command line */
static const char *after_word(const char *s) {
    while (*s && *s != ' ') s++;
    if (*s == ' ') s++;
    return s;
}

static void substitute(const char *arg) {
    /* s /old/new/  - any character after s works as the separator */
    while (*arg == ' ') arg++;
    char sep = *arg;
    if (!sep) { printf("use: s /old/new/\n"); return; }
    const char *a = arg + 1, *m = strchr(a, sep);
    if (!m) { printf("use: s /old/new/\n"); return; }
    const char *b = m + 1, *e = strchr(b, sep);
    if (!e) e = b + strlen(b);
    size_t la = (size_t)(m - a), lb = (size_t)(e - b);
    if (!la) { printf("the old text is empty\n"); return; }
    char old[MAXLINE], new_[MAXLINE];
    if (la >= sizeof old || lb >= sizeof new_) { printf("too long\n"); return; }
    memcpy(old, a, la); old[la] = 0;
    memcpy(new_, b, lb); new_[lb] = 0;
    int count = 0, nlines = 0;
    bool changed = false;
    for (int i = 0; i < text.n; i++) {
        if (!strstr(text.v[i], old)) continue;
        if (!changed) { change(); changed = true; }
        char out[MAXLINE * 2];
        size_t o = 0;
        const char *p = text.v[i];
        for (const char *q; (q = strstr(p, old)) && o + (size_t)(q - p) + lb < sizeof out - 1; p = q + la) {
            memcpy(out + o, p, (size_t)(q - p));
            o += (size_t)(q - p);
            memcpy(out + o, new_, lb);
            o += lb;
            count++;
        }
        size_t rest = strlen(p);
        if (o + rest >= sizeof out) rest = sizeof out - 1 - o;
        memcpy(out + o, p, rest);
        o += rest;
        free(text.v[i]);
        text.v[i] = malloc(o + 1);
        if (!text.v[i]) die();
        memcpy(text.v[i], out, o);
        text.v[i][o] = 0;
        nlines++;
        show(i + 1);
    }
    printf("%d replacement(s) in %d line(s)\n", count, nlines);
}

static void help(void) {
    printf("commands (n, m are line numbers):\n"
           "  l               the next page            l <n> [m]     lines n to m\n"
           "  <n>             show line n              l all         every line\n"
           "  a <text>        add a line at the end    a             add several (end: .)\n"
           "  i <n> <text>    insert before line n     i <n>         insert several\n"
           "  r <n> <text>    replace line n           d <n>[-m]     delete line(s)\n"
           "  f <text>        find the lines with it   s /old/new/   replace everywhere\n"
           "  u               undo the last change     w [file]      save (as)\n"
           "  q               quit                     q!            quit without saving\n");
}

int main(int argc, char **argv) {
    if (argc != 2) {
        printf("use: edit <file>\n");
        return 2;
    }
    strlcpy(path, argv[1], sizeof path);
    int r = load();
    if (r < 0) return 1;
    printf(C_CYAN "edit" C_RESET " %s: %s, %d line(s) - h for help\n", path, r ? "opened" : "a new file", text.n);
    if (text.n) list(1, PAGE);
    char line[MAXLINE];
    int eofs = 0;
    for (;;) {
        printf(C_YELLOW "edit%s>" C_RESET " ", modified ? "*" : "");
        flush();
        ssize_t n = read(0, line, sizeof line - 1);
        if (n <= 0) {
            if (++eofs == 3) break;                        /* the input is gone */
            strcpy(line, "q");
            n = 1;
        } else {
            eofs = 0;
        }
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) n--;
        line[n] = 0;
        char *c = line;
        while (*c == ' ') c++;
        if (!*c) continue;
        char cmd = c[0];
        const char *arg = after_word(c);
        int a, b;
        bool one = c[1] == ' ' || c[1] == 0;
        if (number(c, &a) && (*after_word(c) == 0) && c[0] >= '0' && c[0] <= '9') {
            if (in_range(a)) { show(a); cur = a + 1; }
        } else if (!strcmp(c, "h") || !strcmp(c, "help") || !strcmp(c, "?")) {
            help();
        } else if (cmd == 'l' && one) {
            if (!strcmp(arg, "all")) list(1, text.n);
            else if (number(arg, &a)) list(a, number(after_word(arg), &b) ? b : a + PAGE - 1);
            else {
                if (cur > text.n) cur = 1;
                list(cur, cur + PAGE - 1);
            }
        } else if (cmd == 'a' && one) {
            if (*arg) {
                change();
                push_line(&text, text.n, arg, strlen(arg));
                show(text.n);
                cur = text.n + 1;
            } else {
                type_lines(text.n);
            }
        } else if (cmd == 'i' && one) {
            if (!number(arg, &a) || a < 1 || a > text.n + 1) { printf("use: i <n> [text]  (n from 1 to %d)\n", text.n + 1); continue; }
            const char *t = after_word(arg);
            if (*t) {
                change();
                push_line(&text, a - 1, t, strlen(t));
                show(a);
            } else {
                type_lines(a - 1);
            }
        } else if (cmd == 'r' && one) {
            if (!number(arg, &a) || !*after_word(arg)) { printf("use: r <n> <text>\n"); continue; }
            if (!in_range(a)) continue;
            const char *t = after_word(arg);
            change();
            free(text.v[a - 1]);
            text.v[a - 1] = strdup(t);
            if (!text.v[a - 1]) die();
            show(a);
        } else if (cmd == 'd' && one) {
            if (!number(arg, &a)) { printf("use: d <n> or d <n>-<m>\n"); continue; }
            b = a;
            const char *dash = strchr(arg, '-');
            if (dash && !number(dash + 1, &b)) { printf("use: d <n>-<m>\n"); continue; }
            if (!in_range(a) || !in_range(b) || b < a) { if (b < a) printf("the first line comes first\n"); continue; }
            change();
            for (int i = a; i <= b; i++) free(text.v[i - 1]);
            memmove(&text.v[a - 1], &text.v[b], sizeof(char *) * (size_t)(text.n - b));
            text.n -= b - a + 1;
            printf("deleted %d line(s); now %d line(s)\n", b - a + 1, text.n);
            cur = a;
        } else if (cmd == 'f' && one) {
            if (!*arg) { printf("use: f <text>\n"); continue; }
            int found = 0;
            for (int i = 0; i < text.n; i++)
                if (strstr(text.v[i], arg)) { show(i + 1); found++; }
            printf("%d line(s) with \"%s\"\n", found, arg);
        } else if (cmd == 's' && (c[1] == ' ' || c[1] == '/')) {
            substitute(c + 1);
        } else if (cmd == 'u' && one) {
            if (!has_undo) { printf("nothing to undo\n"); continue; }
            lines t = text;
            text = undo_text;
            undo_text = t;
            modified = true;
            printf("undone: %d line(s) now (u again redoes)\n", text.n);
        } else if (cmd == 'w' && one) {
            const char *to = *arg ? arg : path;
            if (save(to)) {
                if (*arg) strlcpy(path, arg, sizeof path);
                modified = false;
            }
        } else if (!strcmp(c, "q!")) {
            break;
        } else if (cmd == 'q' && one) {
            if (modified) { printf("there are unsaved changes: w saves them, q! quits without saving\n"); continue; }
            break;
        } else {
            printf("unknown command (h for help)\n");
        }
    }
    return 0;
}
