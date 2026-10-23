/* Noter: the journal to a USB stick and back.
 *
 * An export is a folder on the stick, noter-export-2026-10-23 (-2, -3 on
 * the same day): entries/ holds every entry file as it is on itelx, and
 * journal.txt all of them in one readable text, oldest first. Import
 * reads entries/ of such a folder; entries already in the journal (same
 * day, time and first line) are skipped. */
#include "noter.h"

static int pick_stick(char *out, size_t len) {
    char vols[8][64];
    int n = k_sticks(vols, 8);
    if (!n) { k_flash("plug in a USB stick (FAT32) first"); return -1; }
    if (n == 1) { strlcpy(out, vols[0], len); return 0; }
    const char *items[8];
    for (int i = 0; i < n; i++) items[i] = vols[i];
    int c = k_menu("Noter - which USB stick?", items, n, 0);
    if (c < 0) return -1;
    strlcpy(out, vols[c], len);
    return 0;
}

static void progress(const char *what, int done, int total) {
    k_frame();
    k_header("Noter", "");
    k_pad_to(k_rows / 2 - 1);
    char bar[200];
    k_line("     %s", what);
    k_line("     %s %d / %d", k_bar(bar, sizeof bar, done, total ? total : 1, k_cols - 30), done, total);
    k_show();
}

void export_menu(void) {
    char stick[64];
    if (pick_stick(stick, sizeof stick)) return;
    const char *items[] = { cur_journal[0] ? "Only this journal" : "Every journal", "Every journal", "Cancel" };
    int c = k_menu("Noter - export to the USB stick", items, cur_journal[0] ? 3 : 2, 0);
    if (c < 0 || (cur_journal[0] && c == 2) || (!cur_journal[0] && c == 1)) return;
    bool only = cur_journal[0] && c == 0;
    char dir[ITX_PATH_MAX], d[16];
    k_fmt_date(d, sizeof d, k_today());
    struct itx_stat st;
    snprintf(dir, sizeof dir, "%s/noter-export-%s", stick, d);
    for (int i = 2; stat(dir, &st) == 0 && i < 100; i++) snprintf(dir, sizeof dir, "%s/noter-export-%s-%d", stick, d, i);
    char edir[ITX_PATH_MAX];
    snprintf(edir, sizeof edir, "%s/entries", dir);
    k_mkdirs(edir);
    if (stat(edir, &st)) { k_flash("%s could not be written (is it full or read only?)", stick); return; }
    /* journal.txt is built oldest first */
    size_t cap = 65536, o = 0;
    char *all = malloc(cap);
    int done = 0, total = 0, failed = 0;
    for (int i = 0; i < nents; i++) if (!only || !strcmp(ents[i].journal, cur_journal)) total++;
    for (int i = nents - 1; i >= 0; i--) {
        struct entry *e = &ents[i];
        if (only && strcmp(e->journal, cur_journal)) continue;
        char *f = entry_file(e);
        if (!f) { failed++; continue; }
        char path[ITX_PATH_MAX];
        snprintf(path, sizeof path, "%s/%06d.txt", edir, e->id);
        if (write_file(path, f, strlen(f), false) < 0) failed++;
        char *text = entry_text(e), ld[40], tm[8];
        size_t need = strlen(text) + 512;
        if (o + need >= cap) { while (o + need >= cap) cap *= 2; all = realloc(all, cap); }
        o += (size_t)snprintf(all + o, cap - o, "==== %s %s   %s%s%s%s%s\n%s%s%s\n\n", k_fmt_long(ld, sizeof ld, e->day),
                              k_fmt_time(tm, sizeof tm, e->minute), e->journal, e->mood ? "   mood: " : "", mood_word(e->mood),
                              e->tags[0] ? "   " : "", e->tags, e->prompt[0] ? e->prompt : "", e->prompt[0] ? "\n\n" : "", text);
        free(text);
        free(f);
        done++;
        if (done % 10 == 0 || done == total) progress("writing the entries to the stick ...", done, total);
    }
    char path[ITX_PATH_MAX];
    snprintf(path, sizeof path, "%s/journal.txt", dir);
    if (write_file(path, all, o, false) < 0) failed++;
    free(all);
    sync();
    if (failed) { k_flash("%d entries written to %s, %d could not be written", done - failed, dir, failed); return; }
    /* written out and unmounted: the stick can be pulled */
    int e = eject(stick);
    k_flash("%d entries written to %s%s", done, dir, e ? "" : "; the stick can be unplugged");
}

void import_menu(void) {
    char stick[64];
    if (pick_stick(stick, sizeof stick)) return;
    int fd = open(stick, O_RDONLY);
    if (fd < 0) { k_flash("cannot read %s", stick); return; }
    char names[32][64];
    const char *items[32];
    int n = 0;
    struct itx_dirent de;
    while (readdir(fd, &de) == 1 && n < 32) {
        if (de.type != VT_DIR || strncmp(de.name, "noter-export-", 13)) continue;
        strlcpy(names[n], de.name, sizeof names[0]);
        items[n] = names[n];
        n++;
    }
    close(fd);
    if (!n) { k_flash("no Noter export (noter-export-...) on %s", stick); return; }
    int c = k_menu("Noter - import which export?", items, n, n - 1);
    if (c < 0) return;
    char dir[ITX_PATH_MAX];
    snprintf(dir, sizeof dir, "%s/%s/entries", stick, names[c]);
    fd = open(dir, O_RDONLY);
    if (fd < 0) { k_flash("%s has no entries folder", names[c]); return; }
    int added = 0, same = 0, bad = 0, seen = 0;
    while (readdir(fd, &de) == 1) {
        if (de.type != VT_FILE || !ends_with(de.name, ".txt")) continue;
        char path[ITX_PATH_MAX];
        snprintf(path, sizeof path, "%s/%s", dir, de.name);
        size_t len;
        char *t = read_file(path, &len, 4u << 20);
        int r = t ? entry_import(t) : -1;
        free(t);
        if (r > 0) added++;
        else if (r == 0) same++;
        else bad++;
        if (++seen % 10 == 0) progress("reading the entries from the stick ...", seen, seen);
    }
    close(fd);
    journals_load();
    journals_save();
    k_flash("%d entries imported, %d were here already%s", added, same, bad ? ", some files were not entries" : "");
}
