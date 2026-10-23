/* School mode: itelx for a student.
 *
 * Installed by the School mode customizer as /system/terminal.xvr: itelx
 * starts it instead of the terminal.
 *
 *   1 Today       classes of the day, what is due, exams coming, study time
 *   2 Homework    homework, assignments, projects, reading: due days,
 *                 priority, progress
 *   3 Timetable   the week's classes
 *   4 Timers      a timer of its own for every subject, Pomodoro, stopwatch;
 *                 each session is logged against the subject's weekly goal
 *   5 Exams       dates, topics, results
 *   6 Diary       a page for every school day
 *   7 Stats       study time per subject, weeks, streaks, grades
 *   Esc           the menu: subjects, grades, settings, tools (calculator,
 *                 units, tables, typing, quiz), export to a USB stick, the
 *                 system (restart, power off, restore the system)
 *
 * The data are text tables in /home/school (kit table.c). */
#include "school.h"

struct k_table subjects, classes, tasks, exams, sessions, grades, diary;
struct school_settings cfg;

const char *const kinds[] = { "homework", "assignment", "project", "reading", "revision", "lab report" };
const int nkinds = 6;

static const char ABOUT[] =
    "School mode 1.0 - itelx for a student.\n"
    "Subjects with their own study timer and weekly goal, the timetable, homework\n"
    "with due days and progress, exams with countdowns, a school diary, grades and\n"
    "statistics. Everything stays in /home/school as text tables; export them to a\n"
    "USB stick to keep a copy. Licence: MPL-2.0 (the itelxOS-apps repository).";

static const char HELP[] =
    "School mode - keys\n"
    "\n"
    "Everywhere    1-7 or Tab: the views     Esc: the menu\n"
    "Today         t start a timer   n new homework   x new exam   d write the diary\n"
    "Homework      n new   Enter change   Space done   + and - progress   d delete\n"
    "              s only one subject   f show done ones too\n"
    "Timetable     arrows choose   n new class on that day   Enter change   d delete\n"
    "Timers        Enter the subject's own timer   p Pomodoro   w stopwatch\n"
    "              t set the subject's timer   g its weekly goal\n"
    "Timer         Space pause and go on   s stop and save   Esc stop\n"
    "Exams         n new   Enter change   r result   d delete\n"
    "Diary         Enter write   t today's page\n"
    "\n"
    "Dates can be typed as 2026-10-23, 23/10, today, tomorrow, +3 (in three days),\n"
    "or a day of the week (fri: the next Friday).\n"
    "\n"
    "In the menu: subjects (name, teacher, room, colour, weekly goal, timer), grades,\n"
    "settings (name, school, Pomodoro lengths, first day of the week, daily goal),\n"
    "tools, export to a USB stick, and the system: restart, power off, the time zone,\n"
    "and \"Restore the system\" (recustomize: it deletes all of this too).";

/* --------------------------------------------------------------- data */

void data_load(void) {
    static const char *const sf[] = { "id", "name", "teacher", "room", "goal", "timer", "colour" };
    static const char *const cf[] = { "id", "subject", "day", "start", "end", "room" };
    static const char *const tf[] = { "id", "subject", "title", "kind", "due", "priority", "progress", "done", "notes", "created" };
    static const char *const ef[] = { "id", "subject", "title", "day", "time", "room", "topics", "result" };
    static const char *const nf[] = { "id", "subject", "day", "start", "seconds", "kind", "note" };
    static const char *const gf[] = { "id", "subject", "title", "score", "max", "weight", "day" };
    static const char *const df[] = { "id", "day", "text" };
    k_table_open(&subjects, HOME "/subjects.txt", sf, S_N);
    k_table_open(&classes, HOME "/timetable.txt", cf, C_N);
    k_table_open(&tasks, HOME "/homework.txt", tf, T_N);
    k_table_open(&exams, HOME "/exams.txt", ef, E_N);
    k_table_open(&sessions, HOME "/sessions.txt", nf, N_N);
    k_table_open(&grades, HOME "/grades.txt", gf, G_N);
    k_table_open(&diary, HOME "/diary.txt", df, D_N);
}

void data_save(void) {
    struct k_table *all[] = { &subjects, &classes, &tasks, &exams, &sessions, &grades, &diary };
    bool bad = false;
    for (int i = 0; i < 7; i++) if (all[i]->dirty && !k_table_save(all[i])) bad = true;
    if (bad) k_flash("could not save (is the disk full?)");
    sync();
}

static void field(const char *t, const char *key, char *out, size_t len) {
    out[0] = 0;
    const char *p = strstr(t, key);
    if (!p) return;
    p += strlen(key);
    size_t n = strcspn(p, "\n");
    if (n >= len) n = len - 1;
    memcpy(out, p, n);
    out[n] = 0;
}

void settings_load(void) {
    memset(&cfg, 0, sizeof cfg);
    cfg.work = 25;
    cfg.rest = 5;
    cfg.long_rest = 15;
    cfg.week_start = 0;
    cfg.daily_goal = 120;
    size_t len;
    char *t = read_file(HOME "/settings.txt", &len, 4096);
    if (!t) return;
    char v[32];
    field(t, "name=", cfg.name, sizeof cfg.name);
    field(t, "school=", cfg.school, sizeof cfg.school);
    field(t, "work=", v, sizeof v); if (v[0]) cfg.work = atoi(v);
    field(t, "rest=", v, sizeof v); if (v[0]) cfg.rest = atoi(v);
    field(t, "long_rest=", v, sizeof v); if (v[0]) cfg.long_rest = atoi(v);
    field(t, "week_start=", v, sizeof v); if (v[0]) cfg.week_start = atoi(v) % 7;
    field(t, "daily_goal=", v, sizeof v); if (v[0]) cfg.daily_goal = atoi(v);
    free(t);
}

void settings_save(void) {
    char b[512];
    int n = snprintf(b, sizeof b, "name=%s\nschool=%s\nwork=%d\nrest=%d\nlong_rest=%d\nweek_start=%d\ndaily_goal=%d\n", cfg.name,
                     cfg.school, cfg.work, cfg.rest, cfg.long_rest, cfg.week_start, cfg.daily_goal);
    k_mkdirs(HOME);
    k_save_file(HOME "/settings.txt", b, (size_t)n);
}

/* ----------------------------------------------------------- subjects */

static const char *const colours[] = { K_CYAN, K_GRN, K_YEL, K_RED, K_DIM, "" };
static const char *const colour_names[] = { "cyan", "green", "yellow", "red", "grey", "white" };

const char *subj_name(long id) {
    int r = k_table_row(&subjects, id);
    return r >= 0 ? k_get(&subjects, r, S_NAME) : "-";
}

const char *subj_colour(long id) {
    int r = k_table_row(&subjects, id);
    long c = r >= 0 ? k_geti(&subjects, r, S_COLOUR) : 5;
    return colours[c >= 0 && c < 6 ? c : 5];
}

int pick_subject(const char *title, long *id) {
    if (!subjects.n) { k_flash("add a subject first (Esc, Subjects)"); return -1; }
    const char **items = malloc(sizeof(char *) * subjects.n);
    for (int i = 0; i < subjects.n; i++) items[i] = k_get(&subjects, i, S_NAME);
    int cur = 0;
    for (int i = 0; i < subjects.n; i++) if (k_geti(&subjects, i, S_ID) == *id) cur = i;
    int c = k_menu(title, items, subjects.n, cur);
    free(items);
    if (c < 0) return -1;
    *id = k_geti(&subjects, c, S_ID);
    return 0;
}

long study_secs(long subj, long from, long to) {
    long s = 0;
    for (int i = 0; i < sessions.n; i++) {
        long d = k_geti(&sessions, i, N_DAY);
        if (d < from || d > to) continue;
        if (subj && k_geti(&sessions, i, N_SUBJ) != subj) continue;
        s += k_geti(&sessions, i, N_SECS);
    }
    return s;
}

long week_first(long day) {
    int wd = k_weekday(day);
    return day - ((wd - cfg.week_start + 7) % 7);
}

/* -------------------------------------------------------------- forms */

bool edit_subject(int row, bool is_new) {
    struct k_field f[] = {
        { "Name", S_NAME, KF_TEXT, true, NULL, 0, NULL, 0, 0, 0 },
        { "Teacher", S_TEACHER, KF_TEXT, false, NULL, 0, NULL, 0, 0, 0 },
        { "Room", S_ROOM, KF_TEXT, false, NULL, 0, NULL, 0, 0, 0 },
        { "Its timer", S_TIMER, KF_MINUTES, false, NULL, 0, NULL, 0, 0, 0 },
        { "Weekly goal", S_GOAL, KF_MINUTES, false, NULL, 0, NULL, 0, 0, 0 },
        { "Colour", S_COLOUR, KF_CHOICE, false, colour_names, 6, NULL, 0, 0, 0 },
    };
    if (is_new) {
        k_seti(&subjects, row, S_TIMER, 45);
        k_seti(&subjects, row, S_GOAL, 180);
        k_seti(&subjects, row, S_COLOUR, subjects.n % 6);
    }
    return k_form(is_new ? "School mode - new subject" : "School mode - subject", &subjects, row, f, 6, is_new);
}

bool edit_task(int row, bool is_new) {
    static const char *const prio[] = { "normal", "high", "urgent" };
    struct k_field f[] = {
        { "Subject", T_SUBJ, KF_REF, true, NULL, 0, &subjects, S_NAME, 0, 0 },
        { "What", T_TITLE, KF_TEXT, true, NULL, 0, NULL, 0, 0, 0 },
        { "Due", T_DUE, KF_DATE, true, NULL, 0, NULL, 0, 0, 0 },
        { "Kind", T_KIND, KF_CHOICE, false, kinds, nkinds, NULL, 0, 0, 0 },
        { "Priority", T_PRIO, KF_CHOICE, false, prio, 3, NULL, 0, 0, 0 },
        { "Progress %", T_PROG, KF_INT, false, NULL, 0, NULL, 0, 0, 100 },
        { "Notes", T_NOTES, KF_LONG, false, NULL, 0, NULL, 0, 0, 0 },
    };
    if (is_new) {
        k_seti(&tasks, row, T_CREATED, k_today());
        k_seti(&tasks, row, T_PROG, 0);
        k_seti(&tasks, row, T_KIND, 0);
        k_seti(&tasks, row, T_PRIO, 0);
    }
    return k_form(is_new ? "School mode - new homework" : "School mode - homework", &tasks, row, f, 7, is_new);
}

bool edit_exam(int row, bool is_new) {
    struct k_field f[] = {
        { "Subject", E_SUBJ, KF_REF, true, NULL, 0, &subjects, S_NAME, 0, 0 },
        { "Exam", E_TITLE, KF_TEXT, true, NULL, 0, NULL, 0, 0, 0 },
        { "Day", E_DAY, KF_DATE, true, NULL, 0, NULL, 0, 0, 0 },
        { "Time", E_TIME, KF_TIME, false, NULL, 0, NULL, 0, 0, 0 },
        { "Room", E_ROOM, KF_TEXT, false, NULL, 0, NULL, 0, 0, 0 },
        { "Topics", E_TOPICS, KF_LONG, false, NULL, 0, NULL, 0, 0, 0 },
        { "Result", E_RESULT, KF_TEXT, false, NULL, 0, NULL, 0, 0, 0 },
    };
    return k_form(is_new ? "School mode - new exam" : "School mode - exam", &exams, row, f, 7, is_new);
}

bool edit_class(int row, bool is_new) {
    static const char *const days[] = { "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday" };
    struct k_field f[] = {
        { "Subject", C_SUBJ, KF_REF, true, NULL, 0, &subjects, S_NAME, 0, 0 },
        { "Day", C_DAY, KF_CHOICE, true, days, 7, NULL, 0, 0, 0 },
        { "Starts", C_START, KF_TIME, true, NULL, 0, NULL, 0, 0, 0 },
        { "Ends", C_END, KF_TIME, true, NULL, 0, NULL, 0, 0, 0 },
        { "Room", C_ROOM, KF_TEXT, false, NULL, 0, NULL, 0, 0, 0 },
    };
    return k_form(is_new ? "School mode - new class" : "School mode - class", &classes, row, f, 5, is_new);
}

static void subjects_menu(void) {
    struct k_list l = { 0, 0, 0 };
    for (;;) {
        l.n = subjects.n;
        k_frame();
        k_header("School mode - subjects", NULL);
        k_line(" " K_DIM "Every subject has its own study timer and a weekly goal." K_RESET);
        k_rule();
        int rows = k_rows - 5;
        k_list_fit(&l, rows);
        long w0 = week_first(k_today());
        for (int i = l.top; i < l.n && i < l.top + rows; i++) {
            long id = k_geti(&subjects, i, S_ID);
            char name[64], tm[16], gl[16], wk[16], who[64];
            k_fit(name, sizeof name, k_get(&subjects, i, S_NAME), 20);
            k_fit(who, sizeof who, k_get(&subjects, i, S_TEACHER), 22);
            k_line("%s timer %-7s goal %-7s week %-7s  %s%s" K_RESET " " K_DIM K_SEP K_RESET " %s", i == l.sel ? K_YEL "\xe2\x96\xb6" : " ",
                   k_fmt_dur(tm, sizeof tm, k_geti(&subjects, i, S_TIMER) * 60), k_fmt_dur(gl, sizeof gl, k_geti(&subjects, i, S_GOAL) * 60),
                   k_fmt_dur(wk, sizeof wk, study_secs(id, w0, w0 + 6)), subj_colour(id), name, who);
        }
        if (!l.n) k_line("   " K_CYAN "No subjects yet: press n to add the first one." K_RESET);
        k_footer("n new   Enter change   d delete   Esc back");
        k_show();
        int k = k_key();
        if (k_list_key(&l, k, rows)) continue;
        if (k == K_ESC || k == 'q') { data_save(); return; }
        if (k == 'n') {
            int r = k_table_add(&subjects);
            if (!edit_subject(r, true)) k_table_del(&subjects, r);
            data_save();
        } else if (k == K_ENTER && l.n) {
            edit_subject(l.sel, false);
            data_save();
        } else if (k == 'd' && l.n) {
            long id = k_geti(&subjects, l.sel, S_ID);
            int used = 0;
            for (int i = 0; i < tasks.n; i++) if (k_geti(&tasks, i, T_SUBJ) == id) used++;
            for (int i = 0; i < classes.n; i++) if (k_geti(&classes, i, C_SUBJ) == id) used++;
            for (int i = 0; i < exams.n; i++) if (k_geti(&exams, i, E_SUBJ) == id) used++;
            char q[160];
            snprintf(q, sizeof q, "Delete %s%s?", k_get(&subjects, l.sel, S_NAME), used ? " (its homework, classes and exams stay, without a subject)" : "");
            if (k_confirm(q)) { k_table_del(&subjects, l.sel); data_save(); }
        }
    }
}

/* ------------------------------------------------------------- grades */

static void grades_menu(void) {
    struct k_list l = { 0, 0, 0 };
    for (;;) {
        l.n = grades.n;
        k_frame();
        k_header("School mode - grades", NULL);
        /* averages per subject (weighted, as a percent) */
        char line[1024] = "";
        for (int s = 0; s < subjects.n; s++) {
            long id = k_geti(&subjects, s, S_ID), wsum = 0, acc = 0;
            for (int i = 0; i < grades.n; i++) {
                if (k_geti(&grades, i, G_SUBJ) != id) continue;
                long mx = k_geti(&grades, i, G_MAX), w = k_geti(&grades, i, G_WEIGHT);
                if (mx <= 0) continue;
                if (w <= 0) w = 1;
                acc += k_geti(&grades, i, G_SCORE) * 100 * w / mx;
                wsum += w;
            }
            if (!wsum) continue;
            char t[96];
            snprintf(t, sizeof t, "%ld%% %s " K_DIM K_SEP K_RESET " ", acc / wsum, k_get(&subjects, s, S_NAME));
            strlcat(line, t, sizeof line);
        }
        k_line(" " K_DIM "averages:" K_RESET " %s", line[0] ? line : K_DIM "none yet" K_RESET);
        k_rule();
        int rows = k_rows - 5;
        k_list_fit(&l, rows);
        for (int i = l.top; i < l.n && i < l.top + rows; i++) {
            char d[24], sc[24], mx[24], t[300], sn[64];
            long s = k_geti(&grades, i, G_SCORE), m = k_geti(&grades, i, G_MAX);
            k_fit(sn, sizeof sn, subj_name(k_geti(&grades, i, G_SUBJ)), 16);
            k_fit(t, sizeof t, k_get(&grades, i, G_TITLE), k_cols - 60);
            k_line("%s %-11s %6s / %-6s %3ld%%  %s%s" K_RESET " " K_DIM K_SEP K_RESET " %s", i == l.sel ? K_YEL "\xe2\x96\xb6" : " ",
                   k_fmt_day(d, sizeof d, k_geti(&grades, i, G_DAY)), k_money(sc, sizeof sc, s), k_money(mx, sizeof mx, m),
                   m > 0 ? s * 100 / m : 0, subj_colour(k_geti(&grades, i, G_SUBJ)), sn, t);
        }
        if (!l.n) k_line("   " K_DIM "No grades yet: press n when you get a mark back." K_RESET);
        k_footer("n new   Enter change   d delete   Esc back");
        k_show();
        int k = k_key();
        if (k_list_key(&l, k, rows)) continue;
        if (k == K_ESC || k == 'q') { data_save(); return; }
        struct k_field f[] = {
            { "Subject", G_SUBJ, KF_REF, true, NULL, 0, &subjects, S_NAME, 0, 0 },
            { "What", G_TITLE, KF_TEXT, true, NULL, 0, NULL, 0, 0, 0 },
            { "Score", G_SCORE, KF_MONEY, true, NULL, 0, NULL, 0, 0, 0 },
            { "Out of", G_MAX, KF_MONEY, true, NULL, 0, NULL, 0, 0, 0 },
            { "Weight", G_WEIGHT, KF_INT, false, NULL, 0, NULL, 0, 0, 100 },
            { "Day", G_DAY, KF_DATE, false, NULL, 0, NULL, 0, 0, 0 },
        };
        if (k == 'n') {
            int r = k_table_add(&grades);
            k_seti(&grades, r, G_DAY, k_today());
            k_seti(&grades, r, G_WEIGHT, 1);
            if (!k_form("School mode - new grade", &grades, r, f, 6, true)) k_table_del(&grades, r);
            data_save();
        } else if (k == K_ENTER && l.n) {
            k_form("School mode - grade", &grades, l.sel, f, 6, false);
            data_save();
        } else if (k == 'd' && l.n && k_confirm("Delete this grade?")) {
            k_table_del(&grades, l.sel);
            data_save();
        }
    }
}

/* ------------------------------------------------------------ settings */

static void settings_menu(void) {
    static const char *const days[] = { "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday" };
    for (;;) {
        char it[8][96];
        snprintf(it[0], 96, "Your name: %s", cfg.name[0] ? cfg.name : "(none)");
        snprintf(it[1], 96, "School: %s", cfg.school[0] ? cfg.school : "(none)");
        snprintf(it[2], 96, "Pomodoro: %d min work, %d min break, %d min long break", cfg.work, cfg.rest, cfg.long_rest);
        snprintf(it[3], 96, "The week starts on %s", days[cfg.week_start]);
        snprintf(it[4], 96, "Daily study goal: %d min", cfg.daily_goal);
        struct itx_stat st;
        snprintf(it[5], 96, "Passcode: %s", stat(HOME "/passcode", &st) == 0 ? "set" : "none");
        snprintf(it[6], 96, "Back");
        const char *items[7] = { it[0], it[1], it[2], it[3], it[4], it[5], it[6] };
        int c = k_menu("School mode - settings", items, 7, 0);
        if (c < 0 || c == 6) return;
        char b[64];
        long v;
        if (c == 0) { if (k_ask("Your name: ", b, sizeof b)) strlcpy(cfg.name, b, sizeof cfg.name); else cfg.name[0] = 0; }
        else if (c == 1) { if (k_ask("Your school: ", b, sizeof b)) strlcpy(cfg.school, b, sizeof cfg.school); else cfg.school[0] = 0; }
        else if (c == 2) {
            if (k_ask_long("Minutes of work in a Pomodoro (25): ", &v) && v > 0 && v < 240) cfg.work = (int)v;
            if (k_ask_long("Minutes of a break (5): ", &v) && v > 0 && v < 120) cfg.rest = (int)v;
            if (k_ask_long("Minutes of the long break after four (15): ", &v) && v > 0 && v < 180) cfg.long_rest = (int)v;
        } else if (c == 3) {
            int d = k_menu("The first day of the week", days, 7, cfg.week_start);
            if (d >= 0) cfg.week_start = d;
        } else if (c == 4) {
            if (k_ask_long("Minutes of study a day: ", &v) && v >= 0 && v < 1440) cfg.daily_goal = (int)v;
        } else if (c == 5) {
            k_lock_set(HOME "/passcode");
        }
        settings_save();
    }
}

/* -------------------------------------------------------------- tools */

static void tools_menu(void) {
    static const char *const items[] = {
        "Calculator (exact fractions)", "Unit converter", "Multiplication tables and practice", "Typing practice",
        "Quiz", "Prime numbers and factors", "Number bases", "Back",
    };
    static const char *const paths[] = {
        "/dex/calc.xvc", "/dex/convert.xvc", "/dex/table.xvc", "/dex/typing.xvc", "/dex/quiz.xvc", "/dex/primes.xvc", "/dex/base.xvc",
    };
    for (;;) {
        int c = k_menu("School mode - tools", items, 8, 0);
        if (c < 0 || c == 7) return;
        struct itx_stat st;
        if (stat(paths[c], &st)) { k_flash("%s is missing", paths[c]); continue; }
        k_run(paths[c], NULL, 0);
    }
}

/* ------------------------------------------------------------- export */

static void export_all(void) {
    char vols[8][64];
    int n = k_sticks(vols, 8);
    if (!n) { k_flash("plug in a USB stick (FAT32) first"); return; }
    int c = 0;
    if (n > 1) {
        const char *items[8];
        for (int i = 0; i < n; i++) items[i] = vols[i];
        c = k_menu("School mode - which USB stick?", items, n, 0);
        if (c < 0) return;
    }
    char dir[ITX_PATH_MAX], d[16];
    k_fmt_date(d, sizeof d, k_today());
    snprintf(dir, sizeof dir, "%s/school-export-%s", vols[c], d);
    k_mkdirs(dir);
    static const char *const files[] = { "subjects", "timetable", "homework", "exams", "sessions", "grades", "diary", "settings" };
    int done = 0;
    for (int i = 0; i < 8; i++) {
        char from[ITX_PATH_MAX], to[ITX_PATH_MAX];
        snprintf(from, sizeof from, HOME "/%s.txt", files[i]);
        snprintf(to, sizeof to, "%s/%s.txt", dir, files[i]);
        size_t len;
        char *t = read_file(from, &len, 64u << 20);
        if (!t) continue;
        if (write_file(to, t, len, false) >= 0) done++;
        free(t);
    }
    sync();
    int e = eject(vols[c]);
    k_flash("%d files written to %s%s", done, dir, e ? "" : "; the stick can be unplugged");
}

/* --------------------------------------------------------------- menu */

void menu(void) {
    static const char *const items[] = {
        "Subjects", "Grades", "Settings: name, Pomodoro, week, passcode", "Tools: calculator, units, tables, typing, quiz",
        "Export to a USB stick", "Keys and help", "System: restart, power off, time zone, restore the system", "Back",
    };
    int c = k_menu("School mode - menu", items, 8, 0);
    switch (c) {
    case 0: subjects_menu(); break;
    case 1: grades_menu(); break;
    case 2: settings_menu(); break;
    case 3: tools_menu(); break;
    case 4: export_all(); break;
    case 5: k_message("School mode - keys", HELP); break;
    case 6: k_system_menu("School mode", ABOUT); break;
    }
}

void tabs(int active) {
    static const char *const names[] = { "Today", "Homework", "Timetable", "Timers", "Exams", "Diary", "Stats" };
    char line[512] = "";
    for (int i = 0; i < 7; i++) {
        char t[64];
        snprintf(t, sizeof t, "%s%d %s" K_RESET "   ", i == active ? K_YEL : K_DIM, i + 1, names[i]);
        strlcat(line, t, sizeof line);
    }
    k_line(" %s", line);
}

int tab_key(int k, int self) {
    if (k == K_TAB) return (self + 1) % 7;
    if (k >= '1' && k <= '7') return k - '1';
    if (k == K_ESC) { menu(); return self; }
    return -1;
}

/* -------------------------------------------------------------- start */

static void first_start(void) {
    k_frame();
    k_header("School mode", NULL);
    k_pad_to(3);
    k_line("     " K_CYAN "Welcome to School mode." K_RESET);
    k_line("%s", "");
    k_line("     This computer is now your school desk: subjects, timetable, homework,");
    k_line("     exams, a study timer for every subject, a diary and your statistics.");
    k_line("%s", "");
    k_line("     Start by adding your subjects (the next screen), then the timetable (3)");
    k_line("     and your homework (2).");
    k_footer("");
    char b[64];
    if (k_ask("Your name (or just Enter): ", b, sizeof b)) strlcpy(cfg.name, b, sizeof cfg.name);
    settings_save();
    subjects_menu();
}

int main(int argc, char **argv) {
    k_begin();
    k_frame();
    struct itx_stat st;
    bool first = stat(HOME "/settings.txt", &st) != 0;
    k_mkdirs(HOME);
    settings_load();
    data_load();
    if (first) first_start();
    k_lock(HOME "/passcode", "School mode");
    int view = 0;
    for (;;) {
        switch (view) {
        case 0: view = view_today(); break;
        case 1: view = view_homework(); break;
        case 2: view = view_timetable(); break;
        case 3: view = view_timers(); break;
        case 4: view = view_exams(); break;
        case 5: view = view_diary(); break;
        default: view = view_stats(); break;
        }
    }
}
