/* kit: tables of records in text files (kit.h).
 *
 *   # itelx table: id title due done          the field names
 *   1<TAB>Read chapter 3<TAB>20390<TAB>0      one record per line
 *
 * A file is never written in place: the new text goes to NAME.new, is
 * read back, and only then takes the old file's place, so a power cut
 * leaves either the old or the new file. */
#include "kit.h"

static char *dup_n(const char *s, size_t n) {
    char *p = malloc(n + 1);
    if (!p) { dprintf(2, "out of memory\n"); exit(1); }
    memcpy(p, s, n);
    p[n] = 0;
    return p;
}

void k_mkdirs(const char *dir) {
    char p[ITX_PATH_MAX];
    strlcpy(p, dir, sizeof p);
    struct itx_stat st;
    for (char *s = p + 1;; s++) {
        if (*s == '/' || !*s) {
            char c = *s;
            *s = 0;
            if (stat(p, &st)) mkdir(p);
            *s = c;
            if (!c) break;
        }
    }
}

bool k_save_file(const char *path, const void *data, size_t len) {
    char tmp[ITX_PATH_MAX], old[ITX_PATH_MAX];
    snprintf(tmp, sizeof tmp, "%s.new", path);
    snprintf(old, sizeof old, "%s.old", path);
    remove(tmp);
    if (write_file(tmp, data, len, false) < 0) { remove(tmp); return false; }
    struct itx_stat st;
    if (stat(tmp, &st) || st.size != len) { remove(tmp); return false; }
    remove(old);
    bool had = stat(path, &st) == 0;
    if (had && rename(path, old)) { remove(tmp); return false; }
    if (rename(tmp, path)) { if (had) rename(old, path); return false; }
    remove(old);
    return true;
}

static void grow_rows(struct k_table *t) {
    if (t->n < t->cap) return;
    t->cap = t->cap ? t->cap * 2 : 32;
    char ***r = realloc(t->rows, sizeof(char **) * t->cap);
    if (!r) { dprintf(2, "out of memory\n"); exit(1); }
    t->rows = r;
}

/* one field of a line, \t \n \\ undone */
static char *unescape(const char *s, size_t n) {
    char *o = dup_n("", n), *q = o;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '\\' && i + 1 < n) {
            i++;
            *q++ = s[i] == 't' ? '\t' : s[i] == 'n' ? '\n' : s[i];
        } else {
            *q++ = s[i];
        }
    }
    *q = 0;
    return o;
}

bool k_table_open(struct k_table *t, const char *path, const char *const *fields, int nf) {
    memset(t, 0, sizeof *t);
    strlcpy(t->path, path, sizeof t->path);
    t->nf = nf;
    t->names = malloc(sizeof(char *) * nf);
    for (int i = 0; i < nf; i++) t->names[i] = strdup(fields[i]);
    t->next_id = 1;
    size_t len;
    char *text = read_file(path, &len, 64u << 20);
    if (!text) {
        /* a save that stopped half way leaves NAME.new or NAME.old */
        char other[ITX_PATH_MAX];
        snprintf(other, sizeof other, "%s.old", path);
        text = read_file(other, &len, 64u << 20);
        if (!text) return false;
    }
    /* the fields of the file, by name: older files may have fewer */
    int map[64];
    int nmap = 0;
    char *p = text;
    char *eol = strchr(p, '\n');
    if (!strncmp(p, "# itelx table:", 14)) {
        char *h = p + 14;
        if (eol) *eol = 0;
        while (*h && nmap < 64) {
            while (*h == ' ') h++;
            char *s = h;
            while (*h && *h != ' ') h++;
            if (h == s) break;
            int found = -1;
            for (int i = 0; i < nf; i++) if ((size_t)(h - s) == strlen(fields[i]) && !strncmp(s, fields[i], h - s)) found = i;
            map[nmap++] = found;
        }
        p = eol ? eol + 1 : p + strlen(p);
    } else {
        for (int i = 0; i < nf && i < 64; i++) map[nmap++] = i;
    }
    while (*p) {
        eol = strchr(p, '\n');
        size_t ll = eol ? (size_t)(eol - p) : strlen(p);
        if (ll) {
            grow_rows(t);
            char **row = malloc(sizeof(char *) * nf);
            for (int i = 0; i < nf; i++) row[i] = NULL;
            size_t s = 0;
            int col = 0;
            for (size_t i = 0; i <= ll; i++) {
                if (i == ll || p[i] == '\t') {
                    if (col < nmap && map[col] >= 0) row[map[col]] = unescape(p + s, i - s);
                    col++;
                    s = i + 1;
                } else if (p[i] == '\\') {
                    i++;
                }
            }
            for (int i = 0; i < nf; i++) if (!row[i]) row[i] = strdup("");
            t->rows[t->n++] = row;
            long id = atol(row[0]);
            if (id >= t->next_id) t->next_id = id + 1;
        }
        if (!eol) break;
        p = eol + 1;
    }
    free(text);
    return true;
}

bool k_table_save(struct k_table *t) {
    size_t cap = 4096, o = 0;
    char *b = malloc(cap);
#define PUT(c) do { if (o + 2 >= cap) { cap *= 2; b = realloc(b, cap); } b[o++] = (c); } while (0)
    const char *h = "# itelx table:";
    for (const char *q = h; *q; q++) PUT(*q);
    for (int i = 0; i < t->nf; i++) {
        PUT(' ');
        for (const char *q = t->names[i]; *q; q++) PUT(*q);
    }
    PUT('\n');
    for (int r = 0; r < t->n; r++) {
        for (int i = 0; i < t->nf; i++) {
            if (i) PUT('\t');
            for (const char *q = t->rows[r][i]; *q; q++) {
                if (*q == '\t') { PUT('\\'); PUT('t'); }
                else if (*q == '\n') { PUT('\\'); PUT('n'); }
                else if (*q == '\\') { PUT('\\'); PUT('\\'); }
                else PUT(*q);
            }
        }
        PUT('\n');
    }
#undef PUT
    char dir[ITX_PATH_MAX];
    strlcpy(dir, t->path, sizeof dir);
    char *slash = strrchr(dir, '/');
    if (slash && slash != dir) { *slash = 0; k_mkdirs(dir); }
    bool ok = k_save_file(t->path, b, o);
    free(b);
    if (ok) t->dirty = false;
    return ok;
}

int k_table_add(struct k_table *t) {
    grow_rows(t);
    char **row = malloc(sizeof(char *) * t->nf);
    for (int i = 0; i < t->nf; i++) row[i] = strdup("");
    t->rows[t->n++] = row;
    k_seti(t, t->n - 1, 0, t->next_id++);
    t->dirty = true;
    return t->n - 1;
}

void k_table_del(struct k_table *t, int row) {
    if (row < 0 || row >= t->n) return;
    for (int i = 0; i < t->nf; i++) free(t->rows[row][i]);
    free(t->rows[row]);
    memmove(&t->rows[row], &t->rows[row + 1], sizeof(char **) * (t->n - row - 1));
    t->n--;
    t->dirty = true;
}

int k_table_row(struct k_table *t, long id) {
    for (int i = 0; i < t->n; i++) if (atol(t->rows[i][0]) == id) return i;
    return -1;
}

const char *k_get(const struct k_table *t, int row, int f) {
    if (row < 0 || row >= t->n || f < 0 || f >= t->nf) return "";
    return t->rows[row][f];
}

long k_geti(const struct k_table *t, int row, int f) { return atol(k_get(t, row, f)); }

void k_set(struct k_table *t, int row, int f, const char *v) {
    if (row < 0 || row >= t->n || f < 0 || f >= t->nf) return;
    char *n = strdup(v ? v : "");
    free(t->rows[row][f]);
    t->rows[row][f] = n;
    t->dirty = true;
}

void k_seti(struct k_table *t, int row, int f, long v) {
    char b[32];
    snprintf(b, sizeof b, "%ld", v);
    k_set(t, row, f, b);
}

/* a stable merge sort of the rows */
void k_table_sort(struct k_table *t, int (*cmp)(char *const *a, char *const *b)) {
    if (t->n < 2) return;
    char ***tmp = malloc(sizeof(char **) * t->n);
    if (!tmp) return;
    for (int w = 1; w < t->n; w *= 2) {
        for (int lo = 0; lo < t->n; lo += 2 * w) {
            int mid = lo + w < t->n ? lo + w : t->n, hi = lo + 2 * w < t->n ? lo + 2 * w : t->n;
            int i = lo, j = mid, k = lo;
            while (i < mid && j < hi) tmp[k++] = cmp(t->rows[j], t->rows[i]) < 0 ? t->rows[j++] : t->rows[i++];
            while (i < mid) tmp[k++] = t->rows[i++];
            while (j < hi) tmp[k++] = t->rows[j++];
        }
        memcpy(t->rows, tmp, sizeof(char **) * t->n);
    }
    free(tmp);
}

void k_table_free(struct k_table *t) {
    for (int r = 0; r < t->n; r++) {
        for (int i = 0; i < t->nf; i++) free(t->rows[r][i]);
        free(t->rows[r]);
    }
    for (int i = 0; i < t->nf; i++) free(t->names[i]);
    free(t->names);
    free(t->rows);
    memset(t, 0, sizeof *t);
}
