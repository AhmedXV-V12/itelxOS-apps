/* Noter: itelx as a journal.
 *
 * Installed by the Noter customizer as /system/terminal.xvr, so it is what
 * itelx starts instead of the terminal. Everything is done with keys:
 *
 *   1 Timeline     every entry, newest first, by month; search, tags,
 *                  favourites, moods, journals
 *   2 Calendar     the month, the days written on, the entries of a day
 *   3 On this day  a week, a month and years ago today
 *   4 Insights     entries, words, streaks, moods, tags, months, weekdays
 *   5 Prompts      questions to write about
 *   Esc            the menu: templates, journals, export and import with a
 *                  USB stick, settings (name, reminder, passcode), the
 *                  system (restart, power off, restore the system)
 *
 * The entries are text files in /home/noter (store.c). */
#include "noter.h"

int g_next_key;

static const char ABOUT[] =
    "Noter 1.0 - itelx as a journal.\n"
    "Write every day: free writing, a template or a question of the day. Each entry\n"
    "has its day and time, a mood, #tags and a journal. Everything stays on this\n"
    "computer as plain text files in /home/noter; export them to a USB stick to keep\n"
    "a copy. Licence: MPL-2.0 (the itelxOS-apps repository).";

static const char HELP[] =
    "Noter - keys\n"
    "\n"
    "Everywhere     1-5 or Tab: the views    Esc: the menu\n"
    "Timeline       n  new entry              Enter  read the entry\n"
    "               /  search the text        #  only one tag\n"
    "               f  favourites only        m  only one mood\n"
    "               j  another journal        t  new entry from a template\n"
    "               x  show everything again  arrows, PgUp, PgDn, Home, End\n"
    "Reading        e  edit    s  favourite   m  mood    t  tags   j  journal\n"
    "               d  delete  [ ]  the older / newer entry\n"
    "Writing        Ctrl+S save   Esc leave   Ctrl+Z undo   Ctrl+T the time\n"
    "               Alt+Shift switches between Arabic and English\n"
    "Calendar       arrows the day   PgUp/PgDn the month   Home today\n"
    "               Enter the entries of the day   n write on that day\n"
    "\n"
    "#words in the text are tags: \"a long walk #family #weekend\".\n"
    "The first line of an entry is its title.\n"
    "\n"
    "In the menu: templates, journals, export to and import from a USB stick, your\n"
    "name, a daily reminder, a passcode, and the system: restart, power off, the time\n"
    "zone, and \"Restore the system\", which puts itelx back as it was installed\n"
    "(recustomize: it deletes the journal too, so export it first).";

/* ------------------------------------------------------------ filters */

static char query[80];
static int *hits, nhits = -1;            /* the ids the search found; -1 no search */
static char tag[48];
static bool star_only;
static int mood_only;

static bool shown(const struct entry *e) {
    if (cur_journal[0] && strcmp(e->journal, cur_journal)) return false;
    if (star_only && !e->star) return false;
    if (mood_only && e->mood != mood_only) return false;
    if (tag[0]) {
        size_t n = strlen(tag);
        bool ok = false;
        for (const char *t = e->tags; (t = strstr(t, tag)); t += n)
            if ((t == e->tags || t[-1] == ' ') && (t[n] == 0 || t[n] == ' ')) { ok = true; break; }
        if (!ok) return false;
    }
    if (nhits >= 0) {
        bool ok = false;
        for (int i = 0; i < nhits; i++) if (hits[i] == e->id) ok = true;
        if (!ok) return false;
    }
    return true;
}

static void search(void) {
    char q[80];
    if (!k_ask("Search the entries for: ", q, sizeof q)) {
        query[0] = 0;
        nhits = -1;
        return;
    }
    strlcpy(query, q, sizeof query);
    free(hits);
    hits = malloc(sizeof(int) * (nents ? nents : 1));
    nhits = 0;
    for (int i = 0; i < nents; i++) if (entry_matches(&ents[i], query)) hits[nhits++] = ents[i].id;
    k_flash("%d entr%s with \"%s\"", nhits, nhits == 1 ? "y" : "ies", query);
}

static void choose_tag(void) {
    char b[48];
    if (!k_ask("Only the entries with the tag (empty: every tag): #", b, sizeof b)) { tag[0] = 0; return; }
    snprintf(tag, sizeof tag, "%s%s", b[0] == '#' ? "" : "#", b);
}

static void clear_filters(void) {
    query[0] = 0;
    nhits = -1;
    tag[0] = 0;
    star_only = false;
    mood_only = 0;
}

/* ---------------------------------------------------------- journals */

static void rename_journal(int j) {
    char b[32];
    if (!k_ask("New name: ", b, sizeof b)) return;
    for (int i = 0; i < njournals; i++) if (!strcmp(journals[i], b)) { k_flash("there is a journal with that name"); return; }
    char old[32];
    strlcpy(old, journals[j], sizeof old);
    for (int i = 0; i < nents; i++) {
        if (strcmp(ents[i].journal, old)) continue;
        struct entry e = ents[i];
        char *t = entry_text(&e);
        strlcpy(e.journal, b, sizeof e.journal);
        entry_save(&e, t);
        free(t);
    }
    strlcpy(journals[j], b, sizeof journals[j]);
    if (!strcmp(cur_journal, old)) strlcpy(cur_journal, b, sizeof cur_journal);
    journals_save();
}

static void journals_menu(void) {
    struct k_list l = { 0, 0, 0 };
    for (;;) {
        l.n = njournals + 1;
        k_frame();
        k_header("Noter - journals", NULL);
        k_line(" " K_DIM "A journal keeps entries apart: personal, work, travel, gratitude ..." K_RESET);
        k_rule();
        int rows = k_rows - 5;
        k_list_fit(&l, rows);
        for (int i = l.top; i < l.n && i < l.top + rows; i++) {
            const char *name = i == 0 ? "All journals" : journals[i - 1];
            int n = 0;
            for (int k = 0; k < nents; k++) if (i == 0 || !strcmp(ents[k].journal, name)) n++;
            bool on = i == 0 ? !cur_journal[0] : !strcmp(cur_journal, name);
            char t[64];
            k_fit(t, sizeof t, name, 32);
            k_line("%s %s %5d entries%s", i == l.sel ? K_YEL "\xe2\x96\xb6" : " ", t, n, on ? K_GRN "   (shown)" : "");
        }
        k_footer("Enter show it   a add   r rename   d delete (an empty one)   Esc back");
        k_show();
        int k = k_key();
        if (k_list_key(&l, k, rows)) continue;
        if (k == K_ESC || k == 'q') return;
        if (k == K_ENTER) {
            if (l.sel == 0) cur_journal[0] = 0;
            else strlcpy(cur_journal, journals[l.sel - 1], sizeof cur_journal);
            return;
        }
        if (k == 'a') {
            char b[32];
            if (njournals >= 16) { k_flash("16 journals are the most"); continue; }
            if (!k_ask("Name of the new journal: ", b, sizeof b)) continue;
            bool dup = false;
            for (int i = 0; i < njournals; i++) if (!strcmp(journals[i], b)) dup = true;
            if (dup) { k_flash("there is one with that name"); continue; }
            strlcpy(journals[njournals++], b, sizeof journals[0]);
            journals_save();
        } else if (k == 'r' && l.sel > 0) {
            rename_journal(l.sel - 1);
        } else if (k == 'd' && l.sel > 0) {
            const char *name = journals[l.sel - 1];
            int n = 0;
            for (int i = 0; i < nents; i++) if (!strcmp(ents[i].journal, name)) n++;
            if (n) { k_flash("%s has %d entries: move or delete them first", name, n); continue; }
            if (njournals == 1) { k_flash("the last journal stays"); continue; }
            if (!strcmp(cur_journal, name)) cur_journal[0] = 0;
            memmove(&journals[l.sel - 1], &journals[l.sel], sizeof journals[0] * (njournals - l.sel));
            njournals--;
            journals_save();
        }
    }
}

/* ---------------------------------------------------------- settings */

static void settings_menu(void) {
    for (;;) {
        char items[5][96], tm[8];
        snprintf(items[0], sizeof items[0], "Your name: %s", set.name[0] ? set.name : "(none)");
        snprintf(items[1], sizeof items[1], "Daily reminder: %s", set.reminder >= 0 ? k_fmt_time(tm, sizeof tm, set.reminder) : "off");
        snprintf(items[2], sizeof items[2], "Question of the day on the timeline: %s", set.prompts ? "on" : "off");
        struct itx_stat st;
        snprintf(items[3], sizeof items[3], "Passcode: %s", stat(LOCKFILE, &st) == 0 ? "set (asked at every start)" : "none");
        snprintf(items[4], sizeof items[4], "Back");
        const char *it[5] = { items[0], items[1], items[2], items[3], items[4] };
        int c = k_menu("Noter - settings", it, 5, 0);
        if (c < 0 || c == 4) return;
        if (c == 0) {
            char b[48];
            if (k_ask("Your name (empty: none): ", b, sizeof b)) strlcpy(set.name, b, sizeof set.name);
            else set.name[0] = 0;
        } else if (c == 1) {
            char b[16];
            int m;
            if (!k_ask("Remind me at (for example 21:30; empty: off): ", b, sizeof b)) set.reminder = -1;
            else if (k_parse_time(b, &m)) set.reminder = m;
            else k_flash("not a time: %s", b);
        } else if (c == 2) {
            set.prompts = !set.prompts;
        } else if (c == 3) {
            k_lock_set(LOCKFILE);
        }
        settings_save();
    }
}

static void from_template(void) {
    const char *items[32];
    for (int i = 0; i < ntemplates; i++) items[i] = templates[i].name;
    int c = k_menu("Noter - new entry from a template", items, ntemplates, 0);
    if (c < 0) return;
    int id = entry_new(k_today(), cur_journal[0] ? cur_journal : journals[0], NULL, templates[c].text);
    if (id) entry_read(id);
}

static void menu(void) {
    static const char *const items[] = {
        "New entry from a template",
        "Search",
        "Journals",
        "Export to a USB stick",
        "Import from a USB stick",
        "Settings: name, reminder, passcode",
        "Keys and help",
        "System: restart, power off, time zone, restore the system",
        "Back",
    };
    int c = k_menu("Noter - menu", items, 9, 0);
    switch (c) {
    case 0: from_template(); break;
    case 1: search(); break;
    case 2: journals_menu(); break;
    case 3: export_menu(); break;
    case 4: import_menu(); break;
    case 5: settings_menu(); break;
    case 6: k_message("Noter - keys", HELP); break;
    case 7: k_system_menu("Noter", ABOUT); break;
    }
}

/* ---------------------------------------------------------- timeline */

static void tabs(int active) {
    static const char *const names[] = { "Timeline", "Calendar", "On this day", "Insights", "Prompts" };
    char line[400] = "";
    for (int i = 0; i < 5; i++) {
        char t[64];
        snprintf(t, sizeof t, "%s%d %s" K_RESET "   ", i == active ? K_YEL : K_DIM, i + 1, names[i]);
        strlcat(line, t, sizeof line);
    }
    k_line(" %s", line);
}

static const char *greeting(void) {
    int m = k_minute_of_day();
    return m < 5 * 60 ? "Good night" : m < 12 * 60 ? "Good morning" : m < 18 * 60 ? "Good afternoon" : "Good evening";
}

static int timeline(void) {
    static int sel_id;
    for (;;) {
        /* the rows: month headings and entries */
        int nrow = 0, cap = nents * 2 + 2;
        int *row = malloc(sizeof(int) * cap);
        int lastm = -1, count = 0;
        for (int i = 0; i < nents; i++) {
            if (!shown(&ents[i])) continue;
            int y, m, d;
            k_civil(ents[i].day, &y, &m, &d);
            if (y * 12 + m != lastm) { row[nrow++] = -(y * 12 + m) - 1; lastm = y * 12 + m; }
            row[nrow++] = i;
            count++;
        }
        int sel = -1, first_row = -1;
        for (int r = 0; r < nrow; r++) {
            if (row[r] < 0) continue;
            if (first_row < 0) first_row = r;
            if (ents[row[r]].id == sel_id) sel = r;
        }
        if (sel < 0) sel = first_row;
        k_frame();
        k_header("Noter", NULL);
        tabs(0);
        int best, st = streak_now(&best);
        char who[80];
        snprintf(who, sizeof who, "%s%s%s.", greeting(), set.name[0] ? ", " : "", set.name);
        k_line(" " K_CYAN "%s" K_RESET "   %s%d day streak" K_RESET "   %d entr%s   journal: %s", who, st ? K_GRN : K_DIM, st,
               count, count == 1 ? "y" : "ies", cur_journal[0] ? cur_journal : "all");
        if (set.reminder >= 0 && k_minute_of_day() >= set.reminder && !entries_on(k_today()))
            k_line(" " K_YEL "You have not written today yet. Press n." K_RESET);
        else if (set.prompts) {
            char q[300];
            k_fit(q, sizeof q, prompt_of_day(k_today()), k_cols - 22);
            k_line(" " K_DIM "Today's question:" K_RESET " %s", q);
        }
        if (nhits >= 0 || tag[0] || star_only || mood_only) {
            char f[300] = "";
            if (nhits >= 0) { strlcat(f, "search \"", sizeof f); strlcat(f, query, sizeof f); strlcat(f, "\"  ", sizeof f); }
            if (tag[0]) { strlcat(f, tag, sizeof f); strlcat(f, "  ", sizeof f); }
            if (star_only) strlcat(f, "\xe2\x98\x85 favourites  ", sizeof f);
            if (mood_only) { strlcat(f, "mood ", sizeof f); strlcat(f, mood_word(mood_only), sizeof f); strlcat(f, "  ", sizeof f); }
            k_line(" " K_YEL "only: %s" K_RESET K_DIM "(x shows everything)" K_RESET, f);
        }
        k_rule();
        int view = k_rows - k_used() - 1;
        static int top;
        if (sel >= 0) {
            if (sel < top) top = sel > 0 && row[sel - 1] < 0 ? sel - 1 : sel;
            if (sel >= top + view) top = sel - view + 1;
        }
        if (top > nrow - view) top = nrow - view;
        if (top < 0) top = 0;
        int tw = k_cols - 53;
        if (tw < 10) tw = 10;
        for (int r = top; r < nrow && r < top + view; r++) {
            if (row[r] < 0) {
                int ym = -row[r] - 1;
                k_line(" " K_CYAN "%s %d" K_RESET, k_month_name(ym % 12 ? ym % 12 : 12), ym % 12 ? ym / 12 : ym / 12 - 1);
                continue;
            }
            struct entry *e = &ents[row[r]];
            char d[24], tm[8], t[400], tg[100];
            int y, m, dd;
            k_civil(e->day, &y, &m, &dd);
            snprintf(d, sizeof d, "%.3s %2d", k_wday_name(k_weekday(e->day)), dd);
            k_fit(t, sizeof t, e->title[0] ? e->title : "(empty)", tw);
            k_fit(tg, sizeof tg, e->tags, 18);
            /* the title and the tags may both be Arabic: the word count (Latin)
               keeps them apart */
            k_line("%s %s  %s  %s%s" K_RESET "  %s " K_DIM K_SEP K_RESET " %s %5dw  " K_DIM "%s", r == sel ? K_YEL "\xe2\x96\xb6" : " ", d,
                   k_fmt_time(tm, sizeof tm, e->minute), mood_colour(e->mood), mood_mark(e->mood), t,
                   e->star ? K_YEL "\xe2\x98\x85" K_RESET : " ", e->words, tg);
        }
        if (!count) {
            k_line("%s", "");
            if (nents) k_line(K_DIM "   Nothing matches. Press x to show every entry." K_RESET);
            else {
                k_line("   " K_CYAN "Your journal is empty." K_RESET);
                k_line("%s", "");
                k_line("   n    write the first entry");
                k_line("   t    start from a template (daily reflection, gratitude, ...)");
                k_line("   5    pick a question to write about");
                k_line("   Esc  the menu: journals, settings, a passcode, the system");
            }
        }
        k_footer("n new  Enter read  / search  # tag  f favourites  m mood  j journal  t template  x all  Esc menu");
        k_show();
        int k = k_key_wait(1000);
        if (k == K_NONE) { free(row); k_watch_media(); continue; }
        /* moving: from entry row to entry row */
        int nsel = sel;
        if (k == KEY_DOWN || k == KEY_UP || k == KEY_PGDN || k == KEY_PGUP || k == KEY_HOME || k == KEY_END) {
            int step = k == KEY_DOWN ? 1 : k == KEY_UP ? -1 : k == KEY_PGDN ? view : k == KEY_PGUP ? -view : 0;
            if (k == KEY_HOME) nsel = 0;
            else if (k == KEY_END) nsel = nrow - 1;
            else nsel = sel + step;
            if (nsel < 0) nsel = 0;
            if (nsel > nrow - 1) nsel = nrow - 1;
            int dir = step < 0 || k == KEY_END ? -1 : 1;
            while (nsel >= 0 && nsel < nrow && row[nsel] < 0) nsel += dir;
            if (nsel < 0 || nsel >= nrow) nsel = sel;
            if (nsel >= 0 && nsel < nrow && row[nsel] >= 0) sel_id = ents[row[nsel]].id;
            free(row);
            continue;
        }
        int cur_id = sel >= 0 && row[sel] >= 0 ? ents[row[sel]].id : 0;
        free(row);
        if (k == K_TAB) return 1;
        if (k >= '1' && k <= '5') return k - '1';
        if (k == K_ESC) { menu(); continue; }
        if (k == K_ENTER && cur_id) { entry_read(cur_id); continue; }
        if (k == 'n') { int id = entry_new(k_today(), cur_journal[0] ? cur_journal : journals[0], NULL, NULL); if (id) { sel_id = id; entry_read(id); } }
        else if (k == 't') from_template();
        else if (k == '/') search();
        else if (k == '#') choose_tag();
        else if (k == 'f') star_only = !star_only;
        else if (k == 'm') {
            static const char *const it[] = { "every mood", "great", "good", "okay", "bad", "awful" };
            int c = k_menu("Noter - only one mood", it, 6, 0);
            if (c >= 0) mood_only = c ? 6 - c : 0;
        } else if (k == 'j') journals_menu();
        else if (k == 'x') clear_filters();
        else if ((k == 'd' || k == KEY_DELETE) && cur_id) {
            if (k_confirm("Delete this entry for good?")) { entry_delete(cur_id); k_flash("the entry is deleted"); }
        } else if (k == 's' && cur_id) {
            struct entry *e = entry_by_id(cur_id);
            struct entry c = *e;
            char *t = entry_text(&c);
            c.star = !c.star;
            entry_save(&c, t);
            free(t);
        }
    }
}

/* ------------------------------------------------------------- start */

static void first_start(void) {
    k_frame();
    k_header("Noter", NULL);
    k_pad_to(3);
    k_line("     " K_CYAN "Welcome to Noter." K_RESET);
    k_line("%s", "");
    k_line("     This computer is now a journal. Write a little every day: what happened,");
    k_line("     what you felt, what you learned. Each entry keeps its day, its mood and its");
    k_line("     #tags; the calendar shows the days you wrote, insights show your streaks.");
    k_line("%s", "");
    k_line("     Everything stays on this computer. To keep a copy, export the journal to a");
    k_line("     USB stick (Esc, then Export).");
    k_line("%s", "");
    k_footer("type your name below (or just Enter)");
    char b[48];
    if (k_ask("What should Noter call you? ", b, sizeof b)) strlcpy(set.name, b, sizeof set.name);
    settings_save();
    journals_save();
}

int main(int argc, char **argv) {
    k_begin();
    k_frame();
    struct itx_stat st;
    bool first = stat(SETFILE, &st) != 0;
    k_mkdirs(ENTRIES);
    settings_load();
    store_load();
    journals_load();
    if (first) first_start();
    k_lock(LOCKFILE, "Noter");
    int view = 0;
    for (;;) {
        g_next_key = 0;
        if (view == 0) { view = timeline(); continue; }
        if (view == 1) view_calendar();
        else if (view == 2) view_onthisday();
        else if (view == 3) view_insights();
        else view_prompts();
        int k = g_next_key;
        if (k == K_TAB) view = (view + 1) % 5;
        else if (k >= '1' && k <= '5') view = k - '1';
        else if (k == K_ESC) menu();
    }
}
