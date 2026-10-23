/* Noter: entries on disk, journals and settings.
 *
 * Each entry is a text file of its own, /home/noter/entries/000123.txt:
 *
 *   date: 2026-10-23
 *   time: 14:32
 *   journal: Personal
 *   mood: 4
 *   star: 1
 *   tags: #extra             (tags added by hand; #words in the text count too)
 *   prompt: What made you smile today?
 *                            (an empty line)
 *   the text, as written
 *
 * Plain text on purpose: the files can be read on any computer, and a
 * damaged file costs one entry, not the journal. */
#include "noter.h"

struct entry *ents;
int nents;
static int cap;

char journals[16][32];
int njournals;
char cur_journal[32];
struct settings set;

/* ---------------------------------------------------------- helpers */

static void field(const char *head, const char *key, char *out, size_t len) {
    out[0] = 0;
    size_t kl = strlen(key);
    for (const char *p = head; *p && *p != '\n';) {
        const char *eol = strchr(p, '\n');
        if (!strncmp(p, key, kl) && p[kl] == ':') {
            const char *v = p + kl + 1;
            while (*v == ' ') v++;
            size_t n = eol ? (size_t)(eol - v) : strlen(v);
            if (n >= len) n = len - 1;
            memcpy(out, v, n);
            out[n] = 0;
            return;
        }
        if (!eol) break;
        p = eol + 1;
    }
}

static bool tag_char(uint32_t c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' ||
           (c >= 0x0620 && c <= 0x064A) || (c >= 0x0660 && c <= 0x0669) || (c >= 0x00C0 && c <= 0x024F);
}

/* "#tag" words of a text, added to tags (no repeats) */
static void collect_tags(const char *text, char *tags, size_t len) {
    for (const char *p = text; *p; p++) {
        if (*p != '#' || (p > text && !isspace(p[-1]))) continue;
        const char *s = p + 1, *q = s;
        while (*q) {
            uint32_t c;
            int n = utf8_decode(q, 4, &c);
            if (n <= 0 || !tag_char(c)) break;
            q += n;
        }
        if (q == s) continue;
        char tag[64];
        size_t n = (size_t)(q - p) < sizeof tag - 1 ? (size_t)(q - p) : sizeof tag - 1;
        memcpy(tag, p, n);
        tag[n] = 0;
        /* already there? */
        bool have = false;
        for (const char *t = tags; (t = strstr(t, tag)); t += n)
            if ((t == tags || t[-1] == ' ') && (t[n] == 0 || t[n] == ' ')) { have = true; break; }
        if (!have && strlen(tags) + n + 2 < len) {
            if (tags[0]) strlcat(tags, " ", len);
            strlcat(tags, tag, len);
        }
    }
}

void entry_refresh(struct entry *e, const char *text) {
    /* the first line that has something in it is the title */
    const char *p = text;
    while (*p == '\n' || *p == ' ') p++;
    size_t n = strcspn(p, "\n");
    if (n >= sizeof e->title) n = sizeof e->title - 1;
    memcpy(e->title, p, n);
    e->title[n] = 0;
    e->words = k_words(text);
}

static int cmp_entry(const void *a, const void *b) {
    const struct entry *x = a, *y = b;
    if (x->day != y->day) return x->day > y->day ? -1 : 1;
    if (x->minute != y->minute) return x->minute > y->minute ? -1 : 1;
    return y->id - x->id;
}

static void sort_entries(void) {
    /* insertion sort: nearly sorted after a change */
    for (int i = 1; i < nents; i++) {
        struct entry k = ents[i];
        int j = i - 1;
        while (j >= 0 && cmp_entry(&ents[j], &k) > 0) { ents[j + 1] = ents[j]; j--; }
        ents[j + 1] = k;
    }
}

static bool parse(int id, const char *t, struct entry *e) {
    memset(e, 0, sizeof *e);
    e->id = id;
    const char *body = strstr(t, "\n\n");
    body = body ? body + 2 : t + strlen(t);
    char v[64];
    field(t, "date", v, sizeof v);
    if (!k_parse_date(v, &e->day)) return false;
    field(t, "time", v, sizeof v);
    if (!k_parse_time(v, &e->minute)) e->minute = 0;
    field(t, "journal", e->journal, sizeof e->journal);
    if (!e->journal[0]) strlcpy(e->journal, "Journal", sizeof e->journal);
    field(t, "mood", v, sizeof v);
    e->mood = atoi(v);
    if (e->mood < 0 || e->mood > 5) e->mood = 0;
    field(t, "star", v, sizeof v);
    e->star = atoi(v) != 0;
    field(t, "tags", e->tags, sizeof e->tags);
    field(t, "prompt", e->prompt, sizeof e->prompt);
    collect_tags(body, e->tags, sizeof e->tags);
    entry_refresh(e, body);
    return true;
}

static void add(const struct entry *e) {
    if (nents == cap) {
        cap = cap ? cap * 2 : 128;
        struct entry *n = realloc(ents, sizeof *ents * cap);
        if (!n) { dprintf(2, "out of memory\n"); exit(1); }
        ents = n;
    }
    ents[nents++] = *e;
}

void store_load(void) {
    k_mkdirs(ENTRIES);
    nents = 0;
    int fd = open(ENTRIES, O_RDONLY);
    if (fd < 0) return;
    struct itx_dirent de;
    while (readdir(fd, &de) == 1) {
        if (de.type != VT_FILE || !ends_with(de.name, ".txt")) continue;
        int id = atoi(de.name);
        if (id <= 0) continue;
        char path[ITX_PATH_MAX];
        snprintf(path, sizeof path, ENTRIES "/%s", de.name);
        size_t len;
        char *t = read_file(path, &len, 4u << 20);
        if (!t) continue;
        struct entry e;
        if (parse(id, t, &e)) add(&e);
        free(t);
    }
    close(fd);
    sort_entries();
}

struct entry *entry_by_id(int id) {
    for (int i = 0; i < nents; i++) if (ents[i].id == id) return &ents[i];
    return NULL;
}

char *entry_text(const struct entry *e) {
    char path[ITX_PATH_MAX];
    snprintf(path, sizeof path, ENTRIES "/%06d.txt", e->id);
    size_t len;
    char *t = read_file(path, &len, 4u << 20);
    if (!t) return strdup("");
    char *body = strstr(t, "\n\n");
    char *out = strdup(body ? body + 2 : "");
    free(t);
    return out;
}

bool entry_save(struct entry *e, const char *text) {
    if (!e->id) {
        int max = 0;
        for (int i = 0; i < nents; i++) if (ents[i].id > max) max = ents[i].id;
        e->id = max + 1;
    }
    /* tags added by hand are the ones not written in the text */
    char manual[160] = "", in_text[160] = "";
    collect_tags(text, in_text, sizeof in_text);
    for (const char *p = e->tags; *p;) {
        while (*p == ' ') p++;
        const char *s = p;
        while (*p && *p != ' ') p++;
        if (p == s) break;
        char tag[64];
        size_t n = (size_t)(p - s) < sizeof tag - 1 ? (size_t)(p - s) : sizeof tag - 1;
        memcpy(tag, s, n);
        tag[n] = 0;
        bool found = false;
        for (const char *t = in_text; (t = strstr(t, tag)); t += n)
            if ((t == in_text || t[-1] == ' ') && (t[n] == 0 || t[n] == ' ')) { found = true; break; }
        if (!found && strlen(manual) + n + 2 < sizeof manual) {
            if (manual[0]) strlcat(manual, " ", sizeof manual);
            strlcat(manual, tag, sizeof manual);
        }
    }
    size_t tl = strlen(text), cap2 = tl + 1024;
    char *buf = malloc(cap2), d[16], tm[8];
    if (!buf) return false;
    int o = snprintf(buf, cap2, "date: %s\ntime: %s\njournal: %s\nmood: %d\nstar: %d\n", k_fmt_date(d, sizeof d, e->day),
                     k_fmt_time(tm, sizeof tm, e->minute), e->journal, e->mood, e->star ? 1 : 0);
    if (manual[0]) o += snprintf(buf + o, cap2 - o, "tags: %s\n", manual);
    if (e->prompt[0]) o += snprintf(buf + o, cap2 - o, "prompt: %s\n", e->prompt);
    buf[o++] = '\n';
    memcpy(buf + o, text, tl);
    o += (int)tl;
    char path[ITX_PATH_MAX];
    snprintf(path, sizeof path, ENTRIES "/%06d.txt", e->id);
    k_mkdirs(ENTRIES);
    bool ok = k_save_file(path, buf, (size_t)o);
    free(buf);
    if (!ok) return false;
    struct entry fresh;
    buf = NULL;
    size_t len;
    char *t = read_file(path, &len, 4u << 20);
    if (!t || !parse(e->id, t, &fresh)) { free(t); return false; }
    free(t);
    struct entry *old = entry_by_id(e->id);
    if (old) *old = fresh;
    else add(&fresh);
    *e = fresh;
    sort_entries();
    sync();
    return true;
}

/* an entry file from an export: saved as a new entry; 0 when it is
   already here (same day, time and title), -1 when it is not an entry */
int entry_import(const char *t) {
    struct entry e;
    if (!parse(1, t, &e)) return -1;
    for (int i = 0; i < nents; i++)
        if (ents[i].day == e.day && ents[i].minute == e.minute && !strcmp(ents[i].title, e.title)) return 0;
    const char *body = strstr(t, "\n\n");
    body = body ? body + 2 : "";
    e.id = 0;
    return entry_save(&e, body) ? 1 : -1;
}

/* the whole file of an entry, header and text */
char *entry_file(const struct entry *e) {
    char path[ITX_PATH_MAX];
    snprintf(path, sizeof path, ENTRIES "/%06d.txt", e->id);
    size_t len;
    return read_file(path, &len, 4u << 20);
}

bool entry_delete(int id) {
    char path[ITX_PATH_MAX];
    snprintf(path, sizeof path, ENTRIES "/%06d.txt", id);
    if (remove(path)) return false;
    for (int i = 0; i < nents; i++) {
        if (ents[i].id != id) continue;
        memmove(&ents[i], &ents[i + 1], sizeof *ents * (nents - i - 1));
        nents--;
        break;
    }
    sync();
    return true;
}

static const char *find_ci(const char *h, const char *n) {
    size_t nl = strlen(n);
    for (; *h; h++) {
        size_t i = 0;
        while (i < nl && h[i] && tolower((unsigned char)h[i]) == tolower((unsigned char)n[i])) i++;
        if (i == nl) return h;
    }
    return NULL;
}

bool entry_matches(const struct entry *e, const char *q) {
    if (find_ci(e->title, q) || find_ci(e->tags, q) || find_ci(e->journal, q)) return true;
    char *t = entry_text(e);
    bool m = find_ci(t, q) != NULL;
    free(t);
    return m;
}

int entries_on(long day) {
    int n = 0;
    for (int i = 0; i < nents; i++) if (ents[i].day == day) n++;
    return n;
}

int streak_now(int *best) {
    /* ents is newest first: walk the days */
    int cur = 0, run = 0, top = 0;
    long today = k_today(), prev = 0;
    bool first = true, current_open = true;
    for (int i = 0; i < nents; i++) {
        long d = ents[i].day;
        if (d > today) continue;                      /* written ahead of time */
        if (!first && d == prev) continue;
        if (first) {
            run = 1;
            current_open = d >= today - 1;
        } else if (d == prev - 1) {
            run++;
        } else {
            if (current_open) { cur = run; current_open = false; }
            if (run > top) top = run;
            run = 1;
        }
        prev = d;
        first = false;
    }
    if (!first) {
        if (current_open) cur = run;
        if (run > top) top = run;
    }
    if (best) *best = top;
    return cur;
}

/* ------------------------------------------------- journals, settings */

void journals_load(void) {
    njournals = 0;
    size_t len;
    char *t = read_file(JFILE, &len, 8192);
    for (char *p = t; p && *p && njournals < 16;) {
        char *e = strchr(p, '\n');
        if (e) *e = 0;
        if (*p) strlcpy(journals[njournals++], p, sizeof journals[0]);
        if (!e) break;
        p = e + 1;
    }
    free(t);
    /* journals that entries name but the list lost */
    for (int i = 0; i < nents && njournals < 16; i++) {
        bool have = false;
        for (int j = 0; j < njournals; j++) if (!strcmp(journals[j], ents[i].journal)) have = true;
        if (!have) strlcpy(journals[njournals++], ents[i].journal, sizeof journals[0]);
    }
    if (!njournals) strlcpy(journals[njournals++], "Journal", sizeof journals[0]);
}

void journals_save(void) {
    char buf[16 * 34];
    size_t o = 0;
    for (int i = 0; i < njournals; i++) o += (size_t)snprintf(buf + o, sizeof buf - o, "%s\n", journals[i]);
    k_save_file(JFILE, buf, o);
}

void settings_load(void) {
    memset(&set, 0, sizeof set);
    set.reminder = -1;
    set.prompts = true;
    size_t len;
    char *t = read_file(SETFILE, &len, 4096);
    if (!t) return;
    char v[64];
    field(t, "name", set.name, sizeof set.name);
    field(t, "reminder", v, sizeof v);
    if (!k_parse_time(v, &set.reminder)) set.reminder = -1;
    field(t, "prompts", v, sizeof v);
    if (v[0]) set.prompts = atoi(v) != 0;
    free(t);
}

void settings_save(void) {
    char buf[256], tm[8];
    int n = snprintf(buf, sizeof buf, "name: %s\nreminder: %s\nprompts: %d\n", set.name,
                     set.reminder >= 0 ? k_fmt_time(tm, sizeof tm, set.reminder) : "", set.prompts ? 1 : 0);
    k_mkdirs(HOME);
    k_save_file(SETFILE, buf, (size_t)n);
}
