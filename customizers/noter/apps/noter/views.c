/* Noter: an entry (reading, writing, its mood), the calendar, on this day,
 * insights and the questions to write about. */
#include "noter.h"

/* ------------------------------------------------------------- moods */

const char *mood_mark(int m) {
    static const char *const s[] = { " ", "\xe2\x98\xb9", "\xe2\x98\xb9", "\xe2\x97\x8b", "\xe2\x98\xba", "\xe2\x98\xba" };
    return m >= 0 && m <= 5 ? s[m] : " ";
}

const char *mood_word(int m) {
    static const char *const s[] = { "", "awful", "bad", "okay", "good", "great" };
    return m >= 0 && m <= 5 ? s[m] : "";
}

const char *mood_colour(int m) {
    return m == 1 || m == 2 ? K_RED : m == 3 ? K_DIM : m >= 4 ? K_GRN : "";
}

void pick_mood(struct entry *e) {
    for (;;) {
        k_frame();
        k_header("Noter - how do you feel?", "");
        k_pad_to(4);
        for (int m = 5; m >= 1; m--)
            k_line("      %s%d  %s  %s" K_RESET "%s", mood_colour(m), m, mood_mark(m), mood_word(m),
                   e->mood == m ? K_YEL "   (now)" : "");
        k_line("%s", "");
        k_line("      0     no mood");
        k_footer("1-5 choose   0 none   Esc keep it as it is");
        k_show();
        int k = k_key();
        if (k >= '0' && k <= '5') { e->mood = k - '0'; return; }
        if (k >= 0x0660 && k <= 0x0665) { e->mood = k - 0x0660; return; }   /* Arabic digits */
        if (k == K_ESC || k == K_ENTER) return;
    }
}

/* ----------------------------------------------------------- writing */

int entry_new(long day, const char *journal, const char *prompt, const char *initial) {
    char *text = initial && initial[0] ? strdup(initial) : NULL;
    char title[160], d[40];
    snprintf(title, sizeof title, "Noter - new entry, %s", k_fmt_long(d, sizeof d, day));
    char info[200];
    snprintf(info, sizeof info, "%s", journal);
    if (prompt && prompt[0]) {
        /* the question stays at the top of the text while writing */
        size_t l = strlen(prompt) + 4 + (text ? strlen(text) : 0);
        char *t = malloc(l);
        snprintf(t, l, "%s\n\n%s", prompt, text ? text : "");
        free(text);
        text = t;
    }
    if (!k_edit(&text, title, info)) { free(text); return 0; }
    /* nothing but the question: nothing to keep */
    const char *body = text;
    if (prompt && prompt[0] && !strncmp(body, prompt, strlen(prompt))) body += strlen(prompt);
    bool empty = true;
    for (const char *p = body; *p; p++) if (!isspace((unsigned char)*p)) empty = false;
    if (empty) { free(text); k_flash("nothing written: no entry made"); return 0; }
    struct entry e;
    memset(&e, 0, sizeof e);
    e.day = day;
    e.minute = day == k_today() ? k_minute_of_day() : 12 * 60;
    strlcpy(e.journal, journal, sizeof e.journal);
    if (prompt) strlcpy(e.prompt, prompt, sizeof e.prompt);
    pick_mood(&e);
    bool ok = entry_save(&e, text);
    free(text);
    if (!ok) { k_flash("the entry could not be saved (is the disk full?)"); return 0; }
    k_flash("saved: %d words", e.words);
    return e.id;
}

void entry_edit(int id) {
    struct entry *p = entry_by_id(id);
    if (!p) return;
    struct entry e = *p;
    char *text = entry_text(&e);
    char title[160], d[40];
    snprintf(title, sizeof title, "Noter - %s", k_fmt_long(d, sizeof d, e.day));
    if (k_edit(&text, title, e.journal)) {
        if (entry_save(&e, text)) k_flash("saved: %d words", e.words);
        else k_flash("the entry could not be saved");
    }
    free(text);
}

/* ----------------------------------------------------------- reading */

static void edit_tags(struct entry *e) {
    char b[160];
    char q[220];
    snprintf(q, sizeof q, "Tags, with # (now: %s): ", e->tags[0] ? e->tags : "none");
    if (!k_ask(q, b, sizeof b)) return;
    /* each word becomes a #tag */
    char out[160] = "";
    for (char *p = b; *p;) {
        while (*p == ' ' || *p == ',') p++;
        char *s = p;
        while (*p && *p != ' ' && *p != ',') p++;
        if (p == s) break;
        char c = *p;
        *p = 0;
        if (strlen(out) + strlen(s) + 3 < sizeof out) {
            if (out[0]) strlcat(out, " ", sizeof out);
            if (*s != '#') strlcat(out, "#", sizeof out);
            strlcat(out, s, sizeof out);
        }
        *p = c;
    }
    strlcpy(e->tags, out, sizeof e->tags);
}

static void move_journal(struct entry *e) {
    const char *items[17];
    for (int i = 0; i < njournals; i++) items[i] = journals[i];
    int c = k_menu("Noter - move to which journal?", items, njournals, 0);
    if (c >= 0) strlcpy(e->journal, journals[c], sizeof e->journal);
}

void entry_read(int id) {
    int top = 0;
    for (;;) {
        struct entry *p = entry_by_id(id);
        if (!p) return;
        struct entry e = *p;
        char *text = entry_text(&e);
        char **rows;
        int n = k_wrap(text, k_cols - 6, &rows);
        k_frame();
        char d[40], tm[8];
        k_header("Noter", e.journal);
        k_line(" " K_CYAN "%s" K_RESET "  %s   %s%s %s" K_RESET "%s", k_fmt_long(d, sizeof d, e.day),
               k_fmt_time(tm, sizeof tm, e.minute), mood_colour(e.mood), mood_mark(e.mood), mood_word(e.mood),
               e.star ? K_YEL "   \xe2\x98\x85 favourite" : "");
        char tags[200];
        k_fit(tags, sizeof tags, e.tags[0] ? e.tags : "no tags", k_cols - 20);
        k_line(" " K_DIM "%d words   %s" K_RESET, e.words, tags);
        k_rule();
        int view = k_rows - 5;
        if (top > n - view) top = n - view;
        if (top < 0) top = 0;
        for (int i = top; i < n && i < top + view; i++) {
            char row[1200];
            snprintf(row, sizeof row, "   %s", rows[i]);
            k_text_row(row);
        }
        k_footer("e edit  s favourite  m mood  t tags  j journal  d delete  [ ] older/newer  arrows scroll  Esc back");
        k_show();
        free(text);
        k_wrap_free(rows, n);
        int k = k_key_wait(1000);
        if (k == K_NONE) { k_watch_media(); continue; }
        if (k == K_ESC || k == 'q') return;
        if (k == KEY_DOWN) top++;
        else if (k == KEY_UP) top--;
        else if (k == KEY_PGDN) top += view;
        else if (k == KEY_PGUP) top -= view;
        else if (k == 'e' || k == K_ENTER) entry_edit(id);
        else if (k == 's' || k == 'm' || k == 't' || k == 'j') {
            char *t = entry_text(&e);
            if (k == 's') e.star = !e.star;
            else if (k == 'm') pick_mood(&e);
            else if (k == 't') edit_tags(&e);
            else move_journal(&e);
            if (!entry_save(&e, t)) k_flash("could not be saved");
            free(t);
        } else if (k == 'd' || k == KEY_DELETE) {
            if (k_confirm("Delete this entry for good?")) {
                entry_delete(id);
                k_flash("the entry is deleted");
                return;
            }
        } else if (k == '[' || k == ']' || k == KEY_LEFT || k == KEY_RIGHT) {
            /* ents is newest first: ] and right go to the newer one */
            int i = 0;
            while (i < nents && ents[i].id != id) i++;
            bool newer = k == ']' || k == KEY_RIGHT;
            int j = newer ? i - 1 : i + 1;
            while (j >= 0 && j < nents && cur_journal[0] && strcmp(ents[j].journal, cur_journal)) j += newer ? -1 : 1;
            if (j >= 0 && j < nents) { id = ents[j].id; top = 0; }
            else k_flash(newer ? "this is the newest entry" : "this is the oldest entry");
        }
    }
}

/* list of entries to pick from (a day with several, on this day, ...) */
static void pick_and_read(const int *ids, int n, const char *title) {
    if (n == 1) { entry_read(ids[0]); return; }
    struct k_list l = { 0, 0, n };
    for (;;) {
        k_frame();
        k_header(title, NULL);
        int rows = k_rows - 4;
        k_list_fit(&l, rows);
        for (int i = l.top; i < n && i < l.top + rows; i++) {
            struct entry *e = entry_by_id(ids[i]);
            if (!e) continue;
            char d[24], tm[8], t[300];
            k_fit(t, sizeof t, e->title, k_cols - 34);
            k_line("%s %-12s %s  %s%s" K_RESET "  %s", i == l.sel ? K_YEL "\xe2\x96\xb6" : " ", k_fmt_day(d, sizeof d, e->day),
                   k_fmt_time(tm, sizeof tm, e->minute), mood_colour(e->mood), mood_mark(e->mood), t);
        }
        k_footer("Enter read   Esc back");
        k_show();
        int k = k_key();
        if (k_list_key(&l, k, rows)) continue;
        if (k == K_ENTER && n) entry_read(ids[l.sel]);
        if (k == K_ESC || k == 'q') return;
    }
}

/* ---------------------------------------------------------- calendar */

static bool in_journal(const struct entry *e) { return !cur_journal[0] || !strcmp(e->journal, cur_journal); }

static int count_day(long day) {
    int n = 0;
    for (int i = 0; i < nents; i++) if (ents[i].day == day && in_journal(&ents[i])) n++;
    return n;
}

void view_calendar(void) {
    long sel = k_today();
    for (;;) {
        int y, m, d;
        k_civil(sel, &y, &m, &d);
        long first = k_days(y, m, 1);
        int lead = k_weekday(first), mdays = k_month_days(y, m);
        k_frame();
        char t[64];
        snprintf(t, sizeof t, "Noter - %s %d", k_month_name(m), y);
        k_header(t, NULL);
        k_line("%s", "");
        int cw = 6, left = (k_cols - 7 * cw) / 2;
        if (left < 1) left = 1;
        char row[600];
        int o = snprintf(row, sizeof row, "%*s", left, "");
        for (int w = 0; w < 7; w++) o += snprintf(row + o, sizeof row - o, K_DIM " %.3s  " K_RESET, k_wday_name(w));
        k_line("%s", row);
        int written = 0, cell = 0;
        for (int week = 0; week < 6; week++) {
            o = snprintf(row, sizeof row, "%*s", left, "");
            for (int w = 0; w < 7; w++, cell++) {
                int dd = cell - lead + 1;
                if (dd < 1 || dd > mdays) { o += snprintf(row + o, sizeof row - o, "      "); continue; }
                long day = first + dd - 1;
                int c = count_day(day);
                if (c) written++;
                const char *col = day == sel ? K_YEL : c ? K_GRN : day == k_today() ? K_CYAN : "";
                char mark[8];
                if (c > 9) snprintf(mark, sizeof mark, "+");
                else if (c) snprintf(mark, sizeof mark, "%d", c);
                else mark[0] = 0;
                if (day == sel) o += snprintf(row + o, sizeof row - o, "%s[%2d]%-1s" K_RESET " ", col, dd, mark);
                else o += snprintf(row + o, sizeof row - o, "%s %2d %-1s" K_RESET " ", col, dd, mark);
            }
            k_line("%s", row);
            if (cell - lead + 1 > mdays) break;
        }
        k_line("%s", "");
        char ds[40];
        k_line("  %d of %d days written in %s.   " K_CYAN "%s" K_RESET, written, mdays, k_month_name(m), k_fmt_long(ds, sizeof ds, sel));
        k_rule();
        int ids[64], n = 0;
        for (int i = nents - 1; i >= 0 && n < 64; i--)
            if (ents[i].day == sel && in_journal(&ents[i])) ids[n++] = ents[i].id;
        int room = k_rows - k_used() - 1;
        for (int i = 0; i < n && i < room; i++) {
            struct entry *e = entry_by_id(ids[i]);
            char tm[8], tt[300];
            k_fit(tt, sizeof tt, e->title, k_cols - 20);
            k_line("   at %s  %s%s" K_RESET "  %s", k_fmt_time(tm, sizeof tm, e->minute), mood_colour(e->mood), mood_mark(e->mood), tt);
        }
        if (!n) k_line(K_DIM "   nothing written on this day: press n to write" K_RESET);
        k_footer("arrows day  PgUp/PgDn month  Home today  Enter read  n write on this day  Tab next view  Esc menu");
        k_show();
        int k = k_key_wait(1000);
        if (k == K_NONE) { k_watch_media(); continue; }
        if (k == KEY_LEFT) sel--;
        else if (k == KEY_RIGHT) sel++;
        else if (k == KEY_UP) sel -= 7;
        else if (k == KEY_DOWN) sel += 7;
        else if (k == KEY_PGUP || k == KEY_PGDN) {
            int ny = y, nm = m + (k == KEY_PGDN ? 1 : -1);
            if (nm < 1) { nm = 12; ny--; }
            if (nm > 12) { nm = 1; ny++; }
            int nd = d > k_month_days(ny, nm) ? k_month_days(ny, nm) : d;
            sel = k_days(ny, nm, nd);
        } else if (k == KEY_HOME) sel = k_today();
        else if (k == K_ENTER) { if (n) pick_and_read(ids, n, "Noter - the entries of the day"); else k_flash("nothing written on this day"); }
        else if (k == 'n') {
            int id = entry_new(sel, cur_journal[0] ? cur_journal : journals[0], NULL, NULL);
            if (id) entry_read(id);
        } else if (k == K_TAB || k == K_ESC || (k >= '1' && k <= '5')) {
            /* back to the main loop, which switches the view */
            g_next_key = k;
            return;
        }
    }
}

/* ------------------------------------------------------- on this day */

void view_onthisday(void) {
    struct k_list l = { 0, 0, 0 };
    for (;;) {
        long today = k_today();
        int ty, tm, td;
        k_civil(today, &ty, &tm, &td);
        int ids[256], n = 0;
        const char *why[256];
        for (int i = 0; i < nents && n < 256; i++) {
            struct entry *e = &ents[i];
            if (!in_journal(e)) continue;
            int y, m, d;
            k_civil(e->day, &y, &m, &d);
            if (m == tm && d == td && y < ty) { ids[n] = e->id; why[n++] = "years ago"; }
            else if (e->day == today - 7) { ids[n] = e->id; why[n++] = "a week ago"; }
            else if (e->day == k_days(tm == 1 ? ty - 1 : ty, tm == 1 ? 12 : tm - 1, td)) { ids[n] = e->id; why[n++] = "a month ago"; }
        }
        l.n = n;
        k_frame();
        char ds[40];
        k_header("Noter - on this day", NULL);
        k_line(" " K_CYAN "%s" K_RESET ": what you wrote a week, a month and years ago today", k_fmt_long(ds, sizeof ds, today));
        k_rule();
        int rows = k_rows - 5;
        k_list_fit(&l, rows);
        for (int i = l.top; i < n && i < l.top + rows; i++) {
            struct entry *e = entry_by_id(ids[i]);
            char d[24], t[300], ago[32];
            int y, m, dd;
            k_civil(e->day, &y, &m, &dd);
            if (!strcmp(why[i], "years ago")) snprintf(ago, sizeof ago, "%d year%s ago", ty - y, ty - y > 1 ? "s" : "");
            else strlcpy(ago, why[i], sizeof ago);
            k_fit(t, sizeof t, e->title, k_cols - 46);
            k_line("%s %-14s %s  %s%s" K_RESET "  %s", i == l.sel ? K_YEL "\xe2\x96\xb6" : " ", ago, k_fmt_date(d, sizeof d, e->day),
                   mood_colour(e->mood), mood_mark(e->mood), t);
        }
        if (!n) {
            k_line("%s", "");
            k_line(K_DIM "   Nothing yet from this day in other years. What you write today shows up here" K_RESET);
            k_line(K_DIM "   in a week, in a month and next year." K_RESET);
        }
        k_footer("Enter read   n write today   Tab next view   Esc menu");
        k_show();
        int k = k_key_wait(1000);
        if (k == K_NONE) { k_watch_media(); continue; }
        if (k_list_key(&l, k, rows)) continue;
        if (k == K_ENTER && n) entry_read(ids[l.sel]);
        else if (k == 'n') { int id = entry_new(today, cur_journal[0] ? cur_journal : journals[0], NULL, NULL); if (id) entry_read(id); }
        else if (k == K_TAB || k == K_ESC || (k >= '1' && k <= '5')) {
            g_next_key = k;
            return;
        }
    }
}

/* ---------------------------------------------------------- insights */

void view_insights(void) {
    for (;;) {
        long today = k_today();
        int total = 0, words = 0, days = 0, moods[6] = { 0 }, wd[7] = { 0 }, hours[4] = { 0 };
        long first = today, prev = -1;
        int month_n[12] = { 0 };
        int ty, tm, td;
        k_civil(today, &ty, &tm, &td);
        struct { char tag[48]; int n; } tags[64];
        int ntags = 0;
        for (int i = 0; i < nents; i++) {
            struct entry *e = &ents[i];
            if (!in_journal(e)) continue;
            total++;
            words += e->words;
            if (e->day != prev) days++;
            prev = e->day;
            if (e->day < first) first = e->day;
            moods[e->mood]++;
            wd[k_weekday(e->day)]++;
            hours[e->minute < 12 * 60 ? (e->minute < 6 * 60 ? 3 : 0) : e->minute < 18 * 60 ? 1 : 2]++;
            int y, m, d;
            k_civil(e->day, &y, &m, &d);
            int ago = (ty - y) * 12 + (tm - m);
            if (ago >= 0 && ago < 12) month_n[11 - ago]++;
            for (const char *p = e->tags; *p;) {
                while (*p == ' ') p++;
                const char *s = p;
                while (*p && *p != ' ') p++;
                if (p == s) break;
                char t[48];
                size_t l = (size_t)(p - s) < sizeof t - 1 ? (size_t)(p - s) : sizeof t - 1;
                memcpy(t, s, l);
                t[l] = 0;
                int k = 0;
                while (k < ntags && strcmp(tags[k].tag, t)) k++;
                if (k == ntags && ntags < 64) { strlcpy(tags[ntags].tag, t, sizeof tags[0].tag); tags[ntags++].n = 0; }
                if (k < ntags) tags[k].n++;
            }
        }
        int best, cur = streak_now(&best);
        k_frame();
        k_header(cur_journal[0] ? "Noter - insights" : "Noter - insights, all journals", NULL);
        char a[512], b[512], c[512];
        k_line(" " K_CYAN "%d" K_RESET " entries   " K_CYAN "%d" K_RESET " words   " K_CYAN "%d" K_RESET " days written   "
               "streak " K_CYAN "%d" K_RESET " day%s (best %d)   average %d words", total, words, days, cur, cur == 1 ? "" : "s",
               best, total ? words / total : 0);
        if (total) k_line(" " K_DIM "since %s" K_RESET, k_fmt_long(a, sizeof a, first));
        k_rule();
        int bw = k_cols / 2 - 22;
        if (bw < 8) bw = 8;
        /* moods and the last twelve months side by side */
        int mmax = 1, nmax = 1, wmax = 1;
        for (int i = 1; i <= 5; i++) if (moods[i] > mmax) mmax = moods[i];
        for (int i = 0; i < 12; i++) if (month_n[i] > nmax) nmax = month_n[i];
        for (int i = 0; i < 7; i++) if (wd[i] > wmax) wmax = wd[i];
        k_line(" " K_YEL "%-*s" K_RESET "  " K_YEL "%s" K_RESET, bw + 18, "How you felt", "Entries in the last 12 months");
        for (int i = 0; i < 12; i++) {
            char left[400] = "", right[400];
            if (i < 5) {
                int mo = 5 - i;
                snprintf(left, sizeof left, "%s%-6s %s %4d" K_RESET, mood_colour(mo), mood_word(mo), k_bar(a, sizeof a, moods[mo], mmax, bw), moods[mo]);
            } else if (i == 5) {
                snprintf(left, sizeof left, K_DIM "%-6s %s %4d" K_RESET, "none", k_bar(a, sizeof a, moods[0], total ? total : 1, bw), moods[0]);
            } else if (i == 7) {
                snprintf(left, sizeof left, K_YEL "%s" K_RESET, "Days of the week");
            } else if (i > 7) {
                /* two weekdays per row, short bars */
                int w1 = (i - 8) * 2, w2 = w1 + 1;
                char x[512];
                if (w1 < 7) {
                    int o = snprintf(left, sizeof left, "%.3s %s %3d", k_wday_name(w1), k_bar(x, sizeof x, wd[w1], wmax, bw / 2 - 2), wd[w1]);
                    if (w2 < 7) snprintf(left + o, sizeof left - o, "  %.3s %s %3d", k_wday_name(w2), k_bar(b, sizeof b, wd[w2], wmax, bw / 2 - 2), wd[w2]);
                }
            }
            int m = tm - 11 + i, y = ty;
            while (m < 1) { m += 12; y--; }
            snprintf(right, sizeof right, "%.3s %02d %s %3d", k_month_name(m), y % 100, k_bar(c, sizeof c, month_n[i], nmax, bw), month_n[i]);
            char lf[500];
            k_fit(lf, sizeof lf, left, bw + 18);
            k_line(" %s  %s", lf, right);
        }
        k_rule();
        /* tags, most used first */
        for (int i = 0; i < ntags; i++)
            for (int j = i + 1; j < ntags; j++)
                if (tags[j].n > tags[i].n) { __typeof__(tags[0]) t = tags[i]; tags[i] = tags[j]; tags[j] = t; }
        char line[600] = "";
        for (int i = 0; i < ntags && i < 12; i++) {
            char t[80];
            snprintf(t, sizeof t, "%d %s " K_DIM K_SEP K_RESET " ", tags[i].n, tags[i].tag);
            strlcat(line, t, sizeof line);
        }
        k_line(" " K_YEL "Tags" K_RESET "  %s", ntags ? line : K_DIM "none yet: write #words in an entry" K_RESET);
        k_line(" " K_YEL "When" K_RESET "  morning %d   afternoon %d   evening %d   night %d", hours[0], hours[1], hours[2], hours[3]);
        k_footer("Tab next view   Esc menu");
        k_show();
        int k = k_key_wait(1000);
        if (k == K_NONE) { k_watch_media(); continue; }
        if (k == K_TAB || k == K_ESC || (k >= '1' && k <= '5')) {
            g_next_key = k;
            return;
        }
    }
}

/* ----------------------------------------------------------- prompts */

void view_prompts(void) {
    struct k_list l = { 0, 0, nprompts };
    const char *today = prompt_of_day(k_today());
    for (int i = 0; i < nprompts; i++) if (prompts[i] == today) l.sel = i;
    for (;;) {
        k_frame();
        k_header("Noter - something to write about", NULL);
        k_line(" " K_DIM "Today's question:" K_RESET " " K_CYAN "%s" K_RESET, today);
        k_rule();
        int rows = k_rows - 5;
        k_list_fit(&l, rows);
        for (int i = l.top; i < nprompts && i < l.top + rows; i++) {
            char t[400];
            k_fit(t, sizeof t, prompts[i], k_cols - 6);
            k_line("%s %s", i == l.sel ? K_YEL "\xe2\x96\xb6" : " ", t);
        }
        k_footer("Enter write about it   r any question   Tab next view   Esc menu");
        k_show();
        int k = k_key_wait(1000);
        if (k == K_NONE) { k_watch_media(); continue; }
        if (k_list_key(&l, k, rows)) continue;
        if (k == 'r') { l.sel = (int)((uint32_t)uptime_ms() % (uint32_t)nprompts); continue; }
        if (k == K_ENTER) {
            int id = entry_new(k_today(), cur_journal[0] ? cur_journal : journals[0], prompts[l.sel], NULL);
            if (id) entry_read(id);
        } else if (k == K_TAB || k == K_ESC || (k >= '1' && k <= '5')) {
            g_next_key = k;
            return;
        }
    }
}
