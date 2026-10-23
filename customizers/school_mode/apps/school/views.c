/* School mode: the views (today, homework, timetable, timers, exams,
 * diary, stats) and the timer screen. Every view returns the next one. */
#include "school.h"

#define SEL(i, s) ((i) == (s) ? K_YEL "\xe2\x96\xb6" : " ")

static void section(const char *title) { k_line(" " K_YEL "%s" K_RESET, title); }

static const char *due_colour(long due, bool done) {
    if (done) return K_DIM;
    long d = due - k_today();
    return d < 0 ? K_RED : d == 0 ? K_YEL : d == 1 ? K_CYAN : "";
}

/* an idle second: the clock goes on, USB update sticks are looked at */
static int key(void) {
    int k = k_key_wait(1000);
    if (k == K_NONE) k_watch_media();
    return k;
}

/* --------------------------------------------------------------- today */

static void start_timer_menu(void) {
    long id = 0;
    if (pick_subject("School mode - study which subject?", &id)) return;
    int r = k_table_row(&subjects, id);
    char a[64], b[64];
    snprintf(a, sizeof a, "Its own timer (%ld min)", k_geti(&subjects, r, S_TIMER));
    snprintf(b, sizeof b, "Pomodoro (%d + %d min)", cfg.work, cfg.rest);
    const char *items[] = { a, b, "Stopwatch (until I stop it)" };
    int c = k_menu("School mode - which timer?", items, 3, 0);
    if (c == 0) run_timer(id, (int)k_geti(&subjects, r, S_TIMER), false);
    else if (c == 1) run_timer(id, cfg.work, true);
    else if (c == 2) run_timer(id, 0, false);
}

static void write_diary(long day) {
    int row = -1;
    for (int i = 0; i < diary.n; i++) if (k_geti(&diary, i, D_DAY) == day) row = i;
    char *text = strdup(row >= 0 ? k_get(&diary, row, D_TEXT) : "");
    char title[96], d[40];
    snprintf(title, sizeof title, "School mode - diary, %s", k_fmt_long(d, sizeof d, day));
    if (k_edit(&text, title, NULL)) {
        if (row < 0) { row = k_table_add(&diary); k_seti(&diary, row, D_DAY, day); }
        k_set(&diary, row, D_TEXT, text);
        data_save();
        k_flash("the diary page is saved");
    }
    free(text);
}

int view_today(void) {
    for (;;) {
        long today = k_today(), w0 = week_first(today);
        int now = k_minute_of_day(), wd = k_weekday(today);
        k_frame();
        k_header(cfg.school[0] ? cfg.school : "School mode", NULL);
        tabs(0);
        long studied = study_secs(0, today, today);
        char a[32], b[32], bar[512];
        k_line(" " K_CYAN "%s%s%s." K_RESET "   studied today " K_CYAN "%s" K_RESET " of %s  %s", now < 12 * 60 ? "Good morning" : now < 18 * 60 ? "Good afternoon" : "Good evening",
               cfg.name[0] ? ", " : "", cfg.name, k_fmt_dur(a, sizeof a, studied), k_fmt_dur(b, sizeof b, cfg.daily_goal * 60L),
               k_bar(bar, sizeof bar, studied, cfg.daily_goal * 60L, 20));
        k_rule();
        /* classes today, in order */
        section("Classes today");
        int order[64], n = 0;
        for (int i = 0; i < classes.n && n < 64; i++) if (k_geti(&classes, i, C_DAY) == wd) order[n++] = i;
        for (int i = 1; i < n; i++)
            for (int j = i; j > 0 && k_geti(&classes, order[j - 1], C_START) > k_geti(&classes, order[j], C_START); j--) {
                int t = order[j]; order[j] = order[j - 1]; order[j - 1] = t;
            }
        bool next_marked = false;
        for (int i = 0; i < n; i++) {
            int r = order[i];
            long s = k_geti(&classes, r, C_START), e = k_geti(&classes, r, C_END), subj = k_geti(&classes, r, C_SUBJ);
            const char *mark = "";
            if (now >= s && now < e) mark = K_GRN "  now" K_RESET;
            else if (now < s && !next_marked) { mark = K_CYAN "  next" K_RESET; next_marked = true; }
            char nm[64], room[64];
            k_fit(nm, sizeof nm, subj_name(subj), 22);
            snprintf(room, sizeof room, "%s", k_get(&classes, r, C_ROOM));
            k_line("   %s-%s  %s%s" K_RESET " " K_DIM K_SEP " room %s" K_RESET "%s", k_fmt_time(a, sizeof a, (int)s), k_fmt_time(b, sizeof b, (int)e),
                   subj_colour(subj), nm, room[0] ? room : "-", now >= e ? K_DIM "  done" K_RESET : mark);
        }
        if (!n) k_line("   " K_DIM "no classes today (the timetable is view 3)" K_RESET);
        k_line("%s", "");
        /* due: overdue, and the next seven days */
        section("Due");
        int shown = 0;
        for (int pass = 0; pass < 2; pass++)
            for (int i = 0; i < tasks.n && shown < 8; i++) {
                if (k_geti(&tasks, i, T_DONE)) continue;
                long due = k_geti(&tasks, i, T_DUE);
                if (pass == 0 ? due >= today : (due < today || due > today + 7)) continue;
                char rel[32], nm[64], t[300];
                long subj = k_geti(&tasks, i, T_SUBJ);
                k_fit(nm, sizeof nm, subj_name(subj), 16);
                k_fit(t, sizeof t, k_get(&tasks, i, T_TITLE), k_cols - 52);
                k_line("   %s%-12s" K_RESET " %3ld%%  %s%s" K_RESET " " K_DIM K_SEP K_RESET " %s", due_colour(due, false), k_fmt_rel(rel, sizeof rel, due),
                       k_geti(&tasks, i, T_PROG), subj_colour(subj), nm, t);
                shown++;
            }
        if (!shown) k_line("   " K_GRN "nothing due this week" K_RESET);
        k_line("%s", "");
        section("Exams coming");
        shown = 0;
        for (int i = 0; i < exams.n && shown < 4; i++) {
            long day = k_geti(&exams, i, E_DAY);
            if (day < today || day > today + 45) continue;
            char rel[32], d[24], nm[64], t[300];
            long subj = k_geti(&exams, i, E_SUBJ);
            k_fit(nm, sizeof nm, subj_name(subj), 16);
            k_fit(t, sizeof t, k_get(&exams, i, E_TITLE), k_cols - 60);
            k_line("   %s%-12s" K_RESET " %s  %s%s" K_RESET " " K_DIM K_SEP K_RESET " %s", day - today <= 3 ? K_RED : K_YEL,
                   k_fmt_rel(rel, sizeof rel, day), k_fmt_day(d, sizeof d, day), subj_colour(subj), nm, t);
            shown++;
        }
        if (!shown) k_line("   " K_DIM "no exams in the next weeks" K_RESET);
        k_line("%s", "");
        section("Study this week");
        for (int i = 0; i < subjects.n && k_used() < k_rows - 2; i++) {
            long id = k_geti(&subjects, i, S_ID), s = study_secs(id, w0, w0 + 6), goal = k_geti(&subjects, i, S_GOAL) * 60;
            char nm[64];
            k_fit(nm, sizeof nm, k_get(&subjects, i, S_NAME), 16);
            k_line("   %s %s %s / %s  " K_DIM K_SEP K_RESET " %s%s", k_bar(bar, sizeof bar, s, goal > 0 ? goal : 1, 24), s >= goal && goal ? K_GRN "done" K_RESET : "    ",
                   k_fmt_dur(a, sizeof a, s), k_fmt_dur(b, sizeof b, goal), subj_colour(id), nm);
        }
        if (!subjects.n) k_line("   " K_CYAN "add your subjects: Esc, then Subjects" K_RESET);
        k_footer("t start a timer   n new homework   x new exam   d today's diary   1-7 views   Esc menu");
        k_show();
        int k = key();
        if (k == K_NONE) continue;
        int v = tab_key(k, 0);
        if (v >= 0 && v != 0) return v;
        if (k == 't') start_timer_menu();
        else if (k == 'n') { int r = k_table_add(&tasks); if (!edit_task(r, true)) k_table_del(&tasks, r); data_save(); }
        else if (k == 'x') { int r = k_table_add(&exams); if (!edit_exam(r, true)) k_table_del(&exams, r); data_save(); }
        else if (k == 'd') write_diary(today);
    }
}

/* ------------------------------------------------------------ homework */

static int cmp_task(char *const *a, char *const *b) {
    bool da = atol(a[T_DONE]) != 0, db = atol(b[T_DONE]) != 0;
    if (da != db) return da ? 1 : -1;
    long x = atol(a[T_DUE]), y = atol(b[T_DUE]);
    if (da) return x > y ? -1 : x < y;                    /* done: newest first */
    if (x != y) return x < y ? -1 : 1;
    return atol(b[T_PRIO]) - atol(a[T_PRIO]);
}

int view_homework(void) {
    static long only_subj;
    static bool show_done;
    struct k_list l = { 0, 0, 0 };
    for (;;) {
        k_table_sort(&tasks, cmp_task);
        int idx[1024], n = 0, open = 0, late = 0;
        for (int i = 0; i < tasks.n && n < 1024; i++) {
            bool done = k_geti(&tasks, i, T_DONE) != 0;
            if (!done) { open++; if (k_geti(&tasks, i, T_DUE) < k_today()) late++; }
            if (only_subj && k_geti(&tasks, i, T_SUBJ) != only_subj) continue;
            if (done && !show_done) continue;
            idx[n++] = i;
        }
        l.n = n;
        k_frame();
        k_header("School mode", NULL);
        tabs(1);
        k_line(" %d open   %s%d late" K_RESET "   %s%s%s", open, late ? K_RED : K_DIM, late, show_done ? "done ones shown   " : "",
               only_subj ? "only: " : "", only_subj ? subj_name(only_subj) : "");
        k_rule();
        int rows = k_rows - k_used() - 1;
        k_list_fit(&l, rows);
        static const char *const prio[] = { " ", K_YEL "!" K_RESET, K_RED "!!" K_RESET };
        for (int j = l.top; j < n && j < l.top + rows; j++) {
            int i = idx[j];
            bool done = k_geti(&tasks, i, T_DONE) != 0;
            long due = k_geti(&tasks, i, T_DUE), subj = k_geti(&tasks, i, T_SUBJ), p = k_geti(&tasks, i, T_PRIO);
            char d[24], nm[64], t[300], rel[32];
            k_fit(nm, sizeof nm, subj_name(subj), 16);
            k_fit(t, sizeof t, k_get(&tasks, i, T_TITLE), k_cols - 76);
            k_line("%s %s%-11s %-12s" K_RESET " %s %-2s %3ld%%  %-10s %s%s" K_RESET " " K_DIM K_SEP K_RESET " %s%s", SEL(j, l.sel), due_colour(due, done),
                   k_fmt_day(d, sizeof d, due), done ? "done" : k_fmt_rel(rel, sizeof rel, due), done ? K_GRN "[x]" K_RESET : "[ ]",
                   prio[p >= 0 && p <= 2 ? p : 0], k_geti(&tasks, i, T_PROG), kinds[k_geti(&tasks, i, T_KIND) % nkinds], subj_colour(subj), nm,
                   done ? K_DIM : "", t);
        }
        if (!n) k_line("   " K_DIM "%s" K_RESET, tasks.n ? "nothing here (f shows the done ones)" : "no homework yet: press n");
        k_footer("n new  Enter change  Space done  + - progress  d delete  s subject  f done ones  1-7 views  Esc menu");
        k_show();
        int k = key();
        if (k == K_NONE) continue;
        if (k_list_key(&l, k, rows)) continue;
        int v = tab_key(k, 1);
        if (v >= 0 && v != 1) return v;
        int cur = n ? idx[l.sel] : -1;
        if (k == 'n') { int r = k_table_add(&tasks); if (only_subj) k_seti(&tasks, r, T_SUBJ, only_subj); if (!edit_task(r, true)) k_table_del(&tasks, r); data_save(); }
        else if (k == K_ENTER && cur >= 0) { edit_task(cur, false); data_save(); }
        else if (k == ' ' && cur >= 0) {
            bool done = k_geti(&tasks, cur, T_DONE) != 0;
            k_seti(&tasks, cur, T_DONE, done ? 0 : k_today());
            if (!done) k_seti(&tasks, cur, T_PROG, 100);
            data_save();
            if (!done) k_flash("well done");
        } else if ((k == '+' || k == '=' || k == '-') && cur >= 0) {
            long p = k_geti(&tasks, cur, T_PROG) + (k == '-' ? -25 : 25);
            k_seti(&tasks, cur, T_PROG, p < 0 ? 0 : p > 100 ? 100 : p);
            data_save();
        } else if ((k == 'd' || k == KEY_DELETE) && cur >= 0 && k_confirm("Delete this homework?")) {
            k_table_del(&tasks, cur);
            data_save();
        } else if (k == 's') {
            long id = only_subj;
            if (only_subj) only_subj = 0;
            else if (pick_subject("School mode - only one subject", &id) == 0) only_subj = id;
        } else if (k == 'f') show_done = !show_done;
    }
}

/* ----------------------------------------------------------- timetable */

int view_timetable(void) {
    static int col, pos;
    for (;;) {
        int today_wd = k_weekday(k_today());
        /* every day's classes in order of start */
        int day_rows[7][32], cnt[7] = { 0 };
        for (int i = 0; i < classes.n; i++) {
            int d = (int)k_geti(&classes, i, C_DAY);
            if (d < 0 || d > 6 || cnt[d] >= 32) continue;
            day_rows[d][cnt[d]++] = i;
        }
        for (int d = 0; d < 7; d++)
            for (int i = 1; i < cnt[d]; i++)
                for (int j = i; j > 0 && k_geti(&classes, day_rows[d][j - 1], C_START) > k_geti(&classes, day_rows[d][j], C_START); j--) {
                    int t = day_rows[d][j]; day_rows[d][j] = day_rows[d][j - 1]; day_rows[d][j - 1] = t;
                }
        int most = 0;
        for (int d = 0; d < 7; d++) if (cnt[d] > most) most = cnt[d];
        int wd_col = (col + cfg.week_start) % 7;
        if (pos > cnt[wd_col] - 1) pos = cnt[wd_col] ? cnt[wd_col] - 1 : 0;
        k_frame();
        k_header("School mode", NULL);
        tabs(2);
        k_rule();
        int cw = (k_cols - 2) / 7;
        if (cw < 10) cw = 10;
        char line[2048];
        int o = 0;
        for (int c = 0; c < 7; c++) {
            int d = (c + cfg.week_start) % 7;
            char h[64], f[64];
            snprintf(h, sizeof h, "%s%s", k_wday_name(d), d == today_wd ? " (today)" : "");
            k_fit(f, sizeof f, h, cw - 1);
            o += snprintf(line + o, sizeof line - o, "%s%s" K_RESET K_DIM K_SEP K_RESET, c == col ? K_YEL : d == today_wd ? K_CYAN : K_DIM, f);
        }
        k_line(" %s", line);
        for (int r = 0; r < most || r < 1; r++) {
            for (int sub = 0; sub < 2; sub++) {
                o = 0;
                for (int c = 0; c < 7; c++) {
                    int d = (c + cfg.week_start) % 7;
                    char cell[256] = "", f[256];
                    const char *colr = "";
                    if (r < cnt[d]) {
                        int i = day_rows[d][r];
                        long subj = k_geti(&classes, i, C_SUBJ);
                        char a[16], b[16];
                        if (sub == 0) snprintf(cell, sizeof cell, "%s-%s", k_fmt_time(a, sizeof a, (int)k_geti(&classes, i, C_START)),
                                               k_fmt_time(b, sizeof b, (int)k_geti(&classes, i, C_END)));
                        else snprintf(cell, sizeof cell, "%s", subj_name(subj));
                        colr = c == col && r == pos ? K_YEL : sub ? subj_colour(subj) : K_DIM;
                    }
                    k_fit(f, sizeof f, cell, cw - 1);
                    o += snprintf(line + o, sizeof line - o, "%s%s" K_RESET K_DIM K_SEP K_RESET, colr, f);
                }
                k_line(" %s", line);
            }
        }
        k_rule();
        if (cnt[wd_col]) {
            int i = day_rows[wd_col][pos];
            int sr = k_table_row(&subjects, k_geti(&classes, i, C_SUBJ));
            char a[16], b[16];
            k_line(" " K_CYAN "%s" K_RESET " %s-%s   room %s   teacher %s", k_wday_name(wd_col), k_fmt_time(a, sizeof a, (int)k_geti(&classes, i, C_START)),
                   k_fmt_time(b, sizeof b, (int)k_geti(&classes, i, C_END)), k_get(&classes, i, C_ROOM)[0] ? k_get(&classes, i, C_ROOM) : "-",
                   sr >= 0 && k_get(&subjects, sr, S_TEACHER)[0] ? k_get(&subjects, sr, S_TEACHER) : "-");
            k_line("   %s%s" K_RESET, subj_colour(k_geti(&classes, i, C_SUBJ)), subj_name(k_geti(&classes, i, C_SUBJ)));
        } else {
            k_line(" " K_DIM "no classes on %s: press n to add one" K_RESET, k_wday_name(wd_col));
        }
        k_footer("left/right day  up/down class  n new class  Enter change  d delete  1-7 views  Esc menu");
        k_show();
        int k = key();
        if (k == K_NONE) continue;
        int v = tab_key(k, 2);
        if (v >= 0 && v != 2) return v;
        if (k == KEY_LEFT) { col = (col + 6) % 7; pos = 0; }
        else if (k == KEY_RIGHT) { col = (col + 1) % 7; pos = 0; }
        else if (k == KEY_UP && pos > 0) pos--;
        else if (k == KEY_DOWN && pos < cnt[wd_col] - 1) pos++;
        else if (k == 'n') {
            int r = k_table_add(&classes);
            k_seti(&classes, r, C_DAY, wd_col);
            if (!edit_class(r, true)) k_table_del(&classes, r);
            data_save();
        } else if (k == K_ENTER && cnt[wd_col]) { edit_class(day_rows[wd_col][pos], false); data_save(); }
        else if ((k == 'd' || k == KEY_DELETE) && cnt[wd_col] && k_confirm("Delete this class?")) {
            k_table_del(&classes, day_rows[wd_col][pos]);
            data_save();
        }
    }
}

/* -------------------------------------------------------------- timers */

static void log_session(long subj, long secs, int kind, long start_min) {
    if (secs < 30) return;                                 /* a few seconds is not a session */
    int r = k_table_add(&sessions);
    k_seti(&sessions, r, N_SUBJ, subj);
    k_seti(&sessions, r, N_DAY, k_today());
    k_seti(&sessions, r, N_START, start_min);
    k_seti(&sessions, r, N_SECS, secs);
    k_seti(&sessions, r, N_KIND, kind);
    data_save();
}

void run_timer(long subj, int minutes, bool pomodoro) {
    long studied = 0, start_min = k_minute_of_day();
    int round = 1;
    bool rest = false, paused = false, finished = false;
    long length = (long)minutes * 60;             /* 0: a stopwatch */
    long t0 = uptime_ms(), base = 0;              /* seconds counted before the last start */
    for (;;) {
        long run = paused ? base : base + (uptime_ms() - t0) / 1000;
        long shown = length ? length - run : run;
        if (length && shown <= 0 && !finished) {
            /* the end of a work time or a break */
            if (!rest) studied += length;
            if (pomodoro) {
                if (!rest) { rest = true; length = (round % 4 ? cfg.rest : cfg.long_rest) * 60L; }
                else { rest = false; round++; length = cfg.work * 60L; }
                base = 0;
                t0 = uptime_ms();
                k_flash(rest ? "Time for a break." : "Back to work.");
                continue;
            }
            finished = true;
            paused = true;
            base = length;
            shown = 0;
        }
        k_frame();
        char title[96];
        snprintf(title, sizeof title, "School mode - %s", subj_name(subj));
        k_header(title, NULL);
        k_pad_to(k_rows / 2 - 6);
        const char *what = finished ? "Time is up! Well done." : pomodoro ? (rest ? "break" : "work") : length ? "studying" : "stopwatch";
        char info[128];
        if (pomodoro) snprintf(info, sizeof info, "Pomodoro %d   %s%s", round, what, paused ? "   (paused)" : "");
        else snprintf(info, sizeof info, "%s%s", what, paused && !finished ? "   (paused)" : "");
        int w = k_width(info);
        k_line("%*s%s%s" K_RESET, (k_cols - w) / 2, "", finished ? K_GRN : rest ? K_CYAN : K_YEL, info);
        k_line("%s", "");
        char clock[32], rows[5][512];
        k_fmt_clock(clock, sizeof clock, shown < 0 ? 0 : shown);
        k_big(clock, rows);
        int bw = k_width(rows[0]);
        for (int r = 0; r < 5; r++) k_line("%*s%s%s" K_RESET, (k_cols - bw) / 2, "", finished ? K_GRN : rest ? K_CYAN : paused ? K_DIM : "", rows[r]);
        k_line("%s", "");
        if (length) {
            char bar[600];
            k_bar(bar, sizeof bar, length - (shown < 0 ? 0 : shown), length, k_cols / 2);
            k_line("%*s" K_DIM "%s" K_RESET, k_cols / 4, "", bar);
        }
        char a[32], b[32];
        long total = studied + (!rest && !finished ? run : 0);
        k_line("%s", "");
        k_line("%*sstudied now %s   today %s", k_cols / 4, "", k_fmt_dur(a, sizeof a, total),
               k_fmt_dur(b, sizeof b, study_secs(0, k_today(), k_today()) + total));
        k_footer(finished ? "Enter or Esc: save and go back" : "Space pause / go on   s stop and save   Esc stop");
        k_show();
        int k = k_key_wait(paused ? 1000 : 250);
        if (k == K_NONE) continue;
        if (finished && (k == K_ENTER || k == K_ESC || k == 's')) {
            log_session(subj, studied, pomodoro ? 1 : length ? 0 : 2, start_min);
            return;
        }
        if (k == ' ') {
            if (paused) { t0 = uptime_ms(); paused = false; }
            else { base = run; paused = true; }
        } else if (k == 's' || k == K_ESC) {
            long sum = studied + (!rest ? run : 0);
            if (k == K_ESC && sum >= 30 && !k_confirm("Save this study time?")) return;
            log_session(subj, sum, pomodoro ? 1 : length ? 0 : 2, start_min);
            char d[32];
            k_flash("%s of %s saved", k_fmt_dur(d, sizeof d, sum), subj_name(subj));
            return;
        }
    }
}

int view_timers(void) {
    struct k_list l = { 0, 0, 0 };
    for (;;) {
        l.n = subjects.n;
        long today = k_today(), w0 = week_first(today);
        k_frame();
        k_header("School mode", NULL);
        tabs(3);
        k_line(" " K_DIM "Every subject has its own timer. Pomodoro: %d min work, %d min break." K_RESET, cfg.work, cfg.rest);
        k_rule();
        int rows = k_rows - k_used() - 9;
        if (rows < 3) rows = 3;
        k_list_fit(&l, rows);
        for (int i = l.top; i < l.n && i < l.top + rows; i++) {
            long id = k_geti(&subjects, i, S_ID), s = study_secs(id, w0, w0 + 6), goal = k_geti(&subjects, i, S_GOAL) * 60;
            char nm[64], a[24], b[24], c[24], bar[512];
            k_fit(nm, sizeof nm, k_get(&subjects, i, S_NAME), 22);
            k_line("%s timer %-7s today %-7s week %-8s of %-7s %s " K_DIM K_SEP K_RESET " %s%s", SEL(i, l.sel),
                   k_fmt_dur(a, sizeof a, k_geti(&subjects, i, S_TIMER) * 60), k_fmt_dur(c, sizeof c, study_secs(id, today, today)),
                   k_fmt_dur(b, sizeof b, s), k_fmt_dur(c, sizeof c, goal), k_bar(bar, sizeof bar, s, goal > 0 ? goal : 1, 16), subj_colour(id), nm);
        }
        if (!l.n) k_line("   " K_CYAN "add your subjects first: Esc, then Subjects" K_RESET);
        k_rule();
        section("Last sessions");
        int shown = 0;
        for (int i = sessions.n - 1; i >= 0 && shown < 5; i--, shown++) {
            char d[24], a[16], b[24], nm[64];
            static const char *const how[] = { "timer", "pomodoro", "stopwatch" };
            long subj = k_geti(&sessions, i, N_SUBJ);
            k_fit(nm, sizeof nm, subj_name(subj), 24);
            k_line("   %s %s  %-8s %-9s %s%s", k_fmt_day(d, sizeof d, k_geti(&sessions, i, N_DAY)), k_fmt_time(a, sizeof a, (int)k_geti(&sessions, i, N_START)),
                   k_fmt_dur(b, sizeof b, k_geti(&sessions, i, N_SECS)), how[k_geti(&sessions, i, N_KIND) % 3], subj_colour(subj), nm);
        }
        if (!shown) k_line("   " K_DIM "none yet" K_RESET);
        k_footer("Enter its timer   p Pomodoro   w stopwatch   t set its timer   g weekly goal   1-7 views   Esc menu");
        k_show();
        int k = key();
        if (k == K_NONE) continue;
        if (k_list_key(&l, k, rows)) continue;
        int v = tab_key(k, 3);
        if (v >= 0 && v != 3) return v;
        if (!l.n) continue;
        long id = k_geti(&subjects, l.sel, S_ID);
        long v2;
        if (k == K_ENTER) run_timer(id, (int)(k_geti(&subjects, l.sel, S_TIMER) > 0 ? k_geti(&subjects, l.sel, S_TIMER) : 45), false);
        else if (k == 'p') run_timer(id, cfg.work, true);
        else if (k == 'w') run_timer(id, 0, false);
        else if (k == 't' && k_ask_long("Minutes of this subject's timer: ", &v2) && v2 > 0 && v2 <= 600) { k_seti(&subjects, l.sel, S_TIMER, v2); data_save(); }
        else if (k == 'g' && k_ask_long("Minutes of study a week for this subject: ", &v2) && v2 >= 0) { k_seti(&subjects, l.sel, S_GOAL, v2); data_save(); }
    }
}

/* --------------------------------------------------------------- exams */

static int cmp_exam(char *const *a, char *const *b) {
    long x = atol(a[E_DAY]), y = atol(b[E_DAY]), t = k_today();
    bool pa = x < t, pb = y < t;
    if (pa != pb) return pa ? 1 : -1;                    /* the coming ones first */
    if (pa) return x > y ? -1 : x < y;
    return x < y ? -1 : x > y ? 1 : (int)(atol(a[E_TIME]) - atol(b[E_TIME]));
}

int view_exams(void) {
    struct k_list l = { 0, 0, 0 };
    for (;;) {
        k_table_sort(&exams, cmp_exam);
        l.n = exams.n;
        long today = k_today();
        k_frame();
        k_header("School mode", NULL);
        tabs(4);
        k_rule();
        int rows = k_rows - k_used() - 6;
        k_list_fit(&l, rows);
        for (int i = l.top; i < l.n && i < l.top + rows; i++) {
            long day = k_geti(&exams, i, E_DAY), subj = k_geti(&exams, i, E_SUBJ);
            char d[24], tm[16], rel[32], nm[64], t[300], res[64];
            k_fit(nm, sizeof nm, subj_name(subj), 16);
            k_fit(t, sizeof t, k_get(&exams, i, E_TITLE), k_cols - 70);
            k_fit(res, sizeof res, k_get(&exams, i, E_RESULT), 10);
            const char *c = day < today ? K_DIM : day - today <= 3 ? K_RED : day - today <= 10 ? K_YEL : "";
            k_line("%s %s%-11s %s  %-12s" K_RESET " result %s %s%s" K_RESET " " K_DIM K_SEP K_RESET " %s", SEL(i, l.sel), c, k_fmt_day(d, sizeof d, day),
                   k_get(&exams, i, E_TIME)[0] ? k_fmt_time(tm, sizeof tm, (int)k_geti(&exams, i, E_TIME)) : "     ",
                   k_fmt_rel(rel, sizeof rel, day), res, subj_colour(subj), nm, t);
        }
        if (!l.n) k_line("   " K_DIM "no exams yet: press n" K_RESET);
        k_rule();
        if (l.n) {
            const char *topics = k_get(&exams, l.sel, E_TOPICS);
            char **lines;
            int n = k_wrap(topics[0] ? topics : "(no topics written)", k_cols - 6, &lines);
            k_line(" " K_YEL "Topics" K_RESET "   room %s", k_get(&exams, l.sel, E_ROOM)[0] ? k_get(&exams, l.sel, E_ROOM) : "-");
            for (int i = 0; i < n && k_used() < k_rows - 1; i++) {
                char row[600];
                snprintf(row, sizeof row, "   %s", lines[i]);
                k_text_row(row);
            }
            k_wrap_free(lines, n);
        }
        k_footer("n new   Enter change   r result   d delete   1-7 views   Esc menu");
        k_show();
        int k = key();
        if (k == K_NONE) continue;
        if (k_list_key(&l, k, rows)) continue;
        int v = tab_key(k, 4);
        if (v >= 0 && v != 4) return v;
        if (k == 'n') { int r = k_table_add(&exams); if (!edit_exam(r, true)) k_table_del(&exams, r); data_save(); }
        else if (k == K_ENTER && l.n) { edit_exam(l.sel, false); data_save(); }
        else if (k == 'r' && l.n) {
            char b[64];
            if (k_ask("Result (for example 18/20 or A): ", b, sizeof b)) { k_set(&exams, l.sel, E_RESULT, b); data_save(); }
        } else if ((k == 'd' || k == KEY_DELETE) && l.n && k_confirm("Delete this exam?")) { k_table_del(&exams, l.sel); data_save(); }
    }
}

/* --------------------------------------------------------------- diary */

static int cmp_diary(char *const *a, char *const *b) {
    long x = atol(a[D_DAY]), y = atol(b[D_DAY]);
    return x > y ? -1 : x < y;
}

int view_diary(void) {
    struct k_list l = { 0, 0, 0 };
    for (;;) {
        k_table_sort(&diary, cmp_diary);
        l.n = diary.n;
        k_frame();
        k_header("School mode", NULL);
        tabs(5);
        k_line(" " K_DIM "A page for every school day: what you learned, what was hard, what to ask." K_RESET);
        k_rule();
        int rows = k_rows / 2 - 4;
        k_list_fit(&l, rows);
        for (int i = l.top; i < l.n && i < l.top + rows; i++) {
            const char *text = k_get(&diary, i, D_TEXT);
            char d[40], t[300], first[300];
            size_t n = strcspn(text, "\n");
            snprintf(first, sizeof first, "%.*s", (int)(n < 290 ? n : 290), text);
            k_fit(t, sizeof t, first, k_cols - 34);
            k_line("%s %-12s %5d words " K_DIM K_SEP K_RESET " %s", SEL(i, l.sel), k_fmt_day(d, sizeof d, k_geti(&diary, i, D_DAY)), k_words(text), t);
        }
        if (!l.n) k_line("   " K_DIM "no pages yet: press t to write today's" K_RESET);
        k_rule();
        if (l.n) {
            char **lines;
            int n = k_wrap(k_get(&diary, l.sel, D_TEXT), k_cols - 6, &lines);
            for (int i = 0; i < n && k_used() < k_rows - 1; i++) {
                char row[600];
                snprintf(row, sizeof row, "   %s", lines[i]);
                k_text_row(row);
            }
            k_wrap_free(lines, n);
        }
        k_footer("t today's page   Enter write this page   d delete   1-7 views   Esc menu");
        k_show();
        int k = key();
        if (k == K_NONE) continue;
        if (k_list_key(&l, k, rows)) continue;
        int v = tab_key(k, 5);
        if (v >= 0 && v != 5) return v;
        if (k == 't') write_diary(k_today());
        else if (k == K_ENTER && l.n) write_diary(k_geti(&diary, l.sel, D_DAY));
        else if ((k == 'd' || k == KEY_DELETE) && l.n && k_confirm("Delete this page?")) { k_table_del(&diary, l.sel); data_save(); }
    }
}

/* --------------------------------------------------------------- stats */

int view_stats(void) {
    for (;;) {
        long today = k_today(), w0 = week_first(today);
        k_frame();
        k_header("School mode", NULL);
        tabs(6);
        /* study days in a row */
        int streak = 0;
        long d = today;
        if (!study_secs(0, d, d)) d--;                     /* not yet today: the streak counts from yesterday */
        while (study_secs(0, d, d) > 0 && streak < 3650) { streak++; d--; }
        int done = 0, on_time = 0, open = 0;
        for (int i = 0; i < tasks.n; i++) {
            long dn = k_geti(&tasks, i, T_DONE);
            if (!dn) { open++; continue; }
            done++;
            if (dn <= k_geti(&tasks, i, T_DUE)) on_time++;
        }
        char a[32], b[32], c[32], bar[512];
        k_line(" study streak " K_CYAN "%d day%s" K_RESET "   this week " K_CYAN "%s" K_RESET "   homework done %d (%d%% on time), %d open",
               streak, streak == 1 ? "" : "s", k_fmt_dur(a, sizeof a, study_secs(0, w0, w0 + 6)), done, done ? on_time * 100 / done : 0, open);
        k_rule();
        section("This week, per subject (the goal is the end of the bar)");
        long most = 1;
        for (int i = 0; i < subjects.n; i++) {
            long g = k_geti(&subjects, i, S_GOAL) * 60, s = study_secs(k_geti(&subjects, i, S_ID), w0, w0 + 6);
            if (g > most) most = g;
            if (s > most) most = s;
        }
        for (int i = 0; i < subjects.n; i++) {
            long id = k_geti(&subjects, i, S_ID), s = study_secs(id, w0, w0 + 6), g = k_geti(&subjects, i, S_GOAL) * 60;
            char nm[64];
            k_fit(nm, sizeof nm, k_get(&subjects, i, S_NAME), 18);
            k_line("   %s %-8s of %-8s %3ld%% " K_DIM K_SEP K_RESET " %s%s", k_bar(bar, sizeof bar, s, most, k_cols / 3), k_fmt_dur(a, sizeof a, s),
                   k_fmt_dur(b, sizeof b, g), g ? s * 100 / g : 0, subj_colour(id), nm);
        }
        if (!subjects.n) k_line("   " K_DIM "no subjects yet" K_RESET);
        k_line("%s", "");
        section("The last eight weeks");
        long wk[8], wmax = 1;
        for (int i = 0; i < 8; i++) {
            long f = w0 - 7L * (7 - i);
            wk[i] = study_secs(0, f, f + 6);
            if (wk[i] > wmax) wmax = wk[i];
        }
        for (int i = 0; i < 8; i++) {
            long f = w0 - 7L * (7 - i);
            k_line("   week of %-11s %s %s", k_fmt_day(c, sizeof c, f), k_bar(bar, sizeof bar, wk[i], wmax, k_cols / 3), k_fmt_dur(a, sizeof a, wk[i]));
        }
        k_line("%s", "");
        section("The last seven days");
        char line[1024] = "";
        for (int i = 6; i >= 0; i--) {
            char t[64];
            snprintf(t, sizeof t, "%.3s %s   ", k_wday_name(k_weekday(today - i)), k_fmt_dur(a, sizeof a, study_secs(0, today - i, today - i)));
            strlcat(line, t, sizeof line);
        }
        k_line("   %s", line);
        k_footer("1-7 views   Esc menu");
        k_show();
        int k = key();
        if (k == K_NONE) continue;
        int v = tab_key(k, 6);
        if (v >= 0 && v != 6) return v;
    }
}
