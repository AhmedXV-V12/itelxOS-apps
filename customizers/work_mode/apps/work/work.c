/* Work mode: itelx for work.
 *
 * Installed by the Work mode customizer as /system/terminal.xvr: itelx
 * starts it instead of the terminal.
 *
 *   1 Dashboard  today's meetings, what is due, the week's hours, projects
 *                that run late or over budget, deals, unpaid invoices
 *   2 Tasks      a list or a board (backlog, to do, doing, review, done);
 *                projects, priority, due days, estimates, people, tags,
 *                checklists, tasks that wait for others
 *   3 Projects   status, client, deadline, budget in hours, hourly rate,
 *                progress; a project's own page
 *   4 Time       a timer on a task or project, the week's timesheet, entries
 *                by hand, billable or not
 *   5 Calendar   meetings with agenda and minutes; action items become tasks
 *   6 Clients    people and companies, the deals pipeline
 *   7 Notes      documents, by project
 *   8 Reports    hours, money, tasks for a week, a month or a year
 *   Esc          the menu: invoices from billable time, expenses, settings,
 *                tools, export, the system (restart, power off, restore)
 *
 * One timer runs at a time; it is kept in /home/work/timer.txt, so it goes
 * on across views and restarts. The data are text tables in /home/work. */
#include "work.h"

struct k_table projects, tasks, times, meetings, people, deals, notes, invoices, expenses;
struct work_settings cfg;
struct running timer;

const char *const task_status[] = { "backlog", "to do", "doing", "review", "done" };
const char *const prio_name[] = { "low", "normal", "high", "urgent" };
const char *const project_status[] = { "planning", "active", "on hold", "done", "cancelled" };
const char *const person_kind[] = { "client", "colleague", "supplier", "other" };
const char *const deal_stage[] = { "lead", "qualified", "proposal", "negotiation", "won", "lost" };
const char *const invoice_status[] = { "draft", "sent", "paid" };
const char *const expense_cat[] = { "travel", "equipment", "software", "office", "meals", "services", "other" };
const int n_task_status = 5, n_prio = 4, n_project_status = 5, n_person_kind = 4, n_deal_stage = 6, n_invoice_status = 3,
          n_expense_cat = 7;

static const char ABOUT[] =
    "Work mode 1.0 - itelx for work.\n"
    "Tasks on a list or a board, projects with budgets and hourly rates, a timer and a\n"
    "timesheet, meetings with agendas and minutes, clients and a deals pipeline,\n"
    "notes, invoices from billable time, expenses and reports. Everything stays in\n"
    "/home/work as text tables; export them to a USB stick to keep a copy.\n"
    "Licence: MPL-2.0 (the itelxOS-apps repository).";

static const char HELP[] =
    "Work mode - keys\n"
    "\n"
    "Everywhere   1-8 or Tab: the views    Esc: the menu    the running timer is at the top\n"
    "Dashboard    n new task   s start the timer   x stop it   m new meeting\n"
    "Tasks        n new   Enter change   Space next status   b board or list\n"
    "             left/right (board) move a task   s timer on the task   d delete\n"
    "             p one project   / search   f done ones too   o only overdue and today\n"
    "Projects     n new   Enter the project's page   e change   d delete\n"
    "Time         s start   x stop   a add by hand   Enter change   d delete   [ ] weeks\n"
    "Calendar     n new meeting   Enter open   t action items to tasks   [ ] weeks\n"
    "Clients      n new person   Enter change   c contacted today   p deals pipeline\n"
    "Notes        n new   Enter write   d delete\n"
    "Reports      [ ] the period   e export the report to a USB stick\n"
    "\n"
    "Checklists: lines \"[ ] something\" in a task's notes; \"[x]\" when it is done.\n"
    "Action items: lines \"- [ ] something\" in a meeting's notes become tasks with t.\n"
    "Dates: 2026-10-23, 23/10, today, tomorrow, +3, fri.  Times: 14:30, 2:30pm.\n"
    "\n"
    "In the menu: invoices (made from billable time), expenses, settings (name,\n"
    "company, currency, hourly rate, week), tools, export, and the system: restart,\n"
    "power off, time zone, \"Restore the system\" (recustomize: it deletes all of this).";

/* --------------------------------------------------------------- data */

void data_load(void) {
    static const char *const pf[] = { "id", "name", "client", "status", "start", "due", "budget", "rate", "description" };
    static const char *const tf[] = { "id", "project", "title", "status", "priority", "due", "estimate", "who", "tags",
                                      "notes", "created", "done", "blocked_by" };
    static const char *const mf[] = { "id", "project", "task", "day", "start", "seconds", "billable", "note", "invoice" };
    static const char *const gf[] = { "id", "title", "day", "start", "minutes", "where", "people", "project", "agenda", "notes" };
    static const char *const cf[] = { "id", "name", "company", "role", "kind", "phone", "email", "notes", "last_contact" };
    static const char *const df[] = { "id", "title", "person", "value", "stage", "close", "notes" };
    static const char *const nf[] = { "id", "title", "project", "day", "text" };
    static const char *const inf[] = { "id", "number", "client", "from", "to", "amount", "status", "day", "due" };
    static const char *const xf[] = { "id", "project", "day", "amount", "category", "note" };
    k_table_open(&projects, HOME "/projects.txt", pf, P_N);
    k_table_open(&tasks, HOME "/tasks.txt", tf, T_N);
    k_table_open(&times, HOME "/time.txt", mf, M_N);
    k_table_open(&meetings, HOME "/meetings.txt", gf, G_N);
    k_table_open(&people, HOME "/people.txt", cf, C_N);
    k_table_open(&deals, HOME "/deals.txt", df, D_N);
    k_table_open(&notes, HOME "/notes.txt", nf, N_N);
    k_table_open(&invoices, HOME "/invoices.txt", inf, I_N);
    k_table_open(&expenses, HOME "/expenses.txt", xf, X_N);
}

void data_save(void) {
    struct k_table *all[] = { &projects, &tasks, &times, &meetings, &people, &deals, &notes, &invoices, &expenses };
    bool bad = false;
    for (int i = 0; i < 9; i++) if (all[i]->dirty && !k_table_save(all[i])) bad = true;
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

static void settings_load(void) {
    memset(&cfg, 0, sizeof cfg);
    strlcpy(cfg.currency, "EGP", sizeof cfg.currency);
    cfg.rate = 0;
    cfg.day_hours = 8;
    cfg.week_start = 6;
    size_t len;
    char *t = read_file(HOME "/settings.txt", &len, 4096);
    if (!t) return;
    char v[32];
    field(t, "name=", cfg.name, sizeof cfg.name);
    field(t, "company=", cfg.company, sizeof cfg.company);
    field(t, "currency=", v, sizeof v); if (v[0]) strlcpy(cfg.currency, v, sizeof cfg.currency);
    field(t, "rate=", v, sizeof v); if (v[0]) cfg.rate = atol(v);
    field(t, "day_hours=", v, sizeof v); if (v[0]) cfg.day_hours = atoi(v);
    field(t, "week_start=", v, sizeof v); if (v[0]) cfg.week_start = atoi(v) % 7;
    free(t);
}

void settings_save(void) {
    char b[512];
    int n = snprintf(b, sizeof b, "name=%s\ncompany=%s\ncurrency=%s\nrate=%ld\nday_hours=%d\nweek_start=%d\n", cfg.name,
                     cfg.company, cfg.currency, cfg.rate, cfg.day_hours, cfg.week_start);
    k_mkdirs(HOME);
    k_save_file(HOME "/settings.txt", b, (size_t)n);
}

const char *cur(void) { return cfg.currency; }

/* -------------------------------------------------------------- timer */

static void timer_save(void) {
    if (!timer.on) { remove(HOME "/timer.txt"); return; }
    char b[300];
    int n = snprintf(b, sizeof b, "project=%ld\ntask=%ld\nsince=%ld\nbillable=%d\nnote=%s\n", timer.proj, timer.task, timer.since,
                     timer.bill ? 1 : 0, timer.note);
    k_save_file(HOME "/timer.txt", b, (size_t)n);
    sync();
}

static void timer_load(void) {
    memset(&timer, 0, sizeof timer);
    size_t len;
    char *t = read_file(HOME "/timer.txt", &len, 4096);
    if (!t) return;
    char v[32];
    field(t, "project=", v, sizeof v); timer.proj = atol(v);
    field(t, "task=", v, sizeof v); timer.task = atol(v);
    field(t, "since=", v, sizeof v); timer.since = atol(v);
    field(t, "billable=", v, sizeof v); timer.bill = atoi(v) != 0;
    field(t, "note=", timer.note, sizeof timer.note);
    timer.on = timer.since > 0;
    free(t);
}

void timer_start(long proj, long task, const char *note) {
    if (timer.on) timer_stop();
    timer.on = true;
    timer.proj = proj;
    timer.task = task;
    timer.since = unix_time();
    timer.bill = proj && proj_rate(proj) > 0;
    strlcpy(timer.note, note ? note : "", sizeof timer.note);
    timer_save();
    k_flash("the timer runs: %s", task ? task_title(task) : proj_name(proj));
}

long timer_secs(void) { return timer.on ? unix_time() - timer.since : 0; }

void timer_stop(void) {
    if (!timer.on) return;
    long secs = timer_secs();
    if (secs >= 30) {
        int r = k_table_add(&times);
        long start = timer.since + (long)k_tz_minutes() * 60;
        k_seti(&times, r, M_PROJ, timer.proj);
        k_seti(&times, r, M_TASK, timer.task);
        k_seti(&times, r, M_DAY, start / 86400);
        k_seti(&times, r, M_START, start % 86400 / 60);
        k_seti(&times, r, M_SECS, secs);
        k_seti(&times, r, M_BILL, timer.bill);
        k_set(&times, r, M_NOTE, timer.note);
        data_save();
        char d[32];
        k_flash("%s saved on %s", k_fmt_dur(d, sizeof d, secs), timer.task ? task_title(timer.task) : proj_name(timer.proj));
    } else {
        k_flash("under half a minute: not saved");
    }
    timer.on = false;
    timer_save();
}

void timer_status(char *out, size_t len) {
    if (!timer.on) { out[0] = 0; return; }
    char c[32], n[64];
    k_fit(n, sizeof n, timer.task ? task_title(timer.task) : proj_name(timer.proj), 24);
    snprintf(out, len, "\xe2\x97\x8f %s  %s", k_fmt_clock(c, sizeof c, timer_secs()), n);
}

/* ------------------------------------------------------------ helpers */

const char *proj_name(long id) {
    int r = id ? k_table_row(&projects, id) : -1;
    return r >= 0 ? k_get(&projects, r, P_NAME) : "(no project)";
}

const char *person_name(long id) {
    int r = id ? k_table_row(&people, id) : -1;
    return r >= 0 ? k_get(&people, r, C_NAME) : "-";
}

const char *task_title(long id) {
    int r = id ? k_table_row(&tasks, id) : -1;
    return r >= 0 ? k_get(&tasks, r, T_TITLE) : "-";
}

long proj_rate(long proj) {
    int r = proj ? k_table_row(&projects, proj) : -1;
    long rate = r >= 0 ? k_geti(&projects, r, P_RATE) : 0;
    return rate > 0 ? rate : cfg.rate;
}

long proj_secs(long proj, long from, long to) {
    long s = 0;
    for (int i = 0; i < times.n; i++) {
        long d = k_geti(&times, i, M_DAY);
        if (d < from || d > to) continue;
        if (proj && k_geti(&times, i, M_PROJ) != proj) continue;
        s += k_geti(&times, i, M_SECS);
    }
    return s;
}

long week_first(long day) {
    int wd = k_weekday(day);
    return day - ((wd - cfg.week_start + 7) % 7);
}

const char *prio_colour(long p) { return p >= 3 ? K_RED : p == 2 ? K_YEL : p == 0 ? K_DIM : ""; }

int pick_project(const char *title, long *id, bool allow_none) {
    int n = projects.n + (allow_none ? 1 : 0);
    if (!n) { k_flash("make a project first (view 3, n)"); return -1; }
    const char **items = malloc(sizeof(char *) * n);
    int base = allow_none ? 1 : 0, sel = 0;
    if (allow_none) items[0] = "(no project)";
    for (int i = 0; i < projects.n; i++) {
        items[base + i] = k_get(&projects, i, P_NAME);
        if (k_geti(&projects, i, P_ID) == *id) sel = base + i;
    }
    int c = k_menu(title, items, n, sel);
    free(items);
    if (c < 0) return -1;
    *id = c < base ? 0 : k_geti(&projects, c - base, P_ID);
    return 0;
}

int pick_task(const char *title, long proj, long *id) {
    int idx[1024], n = 0;
    const char *items[1025];
    items[n] = "(the project itself, no task)";
    idx[n++] = -1;
    for (int i = 0; i < tasks.n && n < 1024; i++) {
        if (k_geti(&tasks, i, T_PROJ) != proj || k_geti(&tasks, i, T_STATUS) == 4) continue;
        items[n] = k_get(&tasks, i, T_TITLE);
        idx[n++] = i;
    }
    int c = k_menu(title, items, n, 0);
    if (c < 0) return -1;
    *id = idx[c] < 0 ? 0 : k_geti(&tasks, idx[c], T_ID);
    return 0;
}

/* -------------------------------------------------------------- forms */

bool edit_task(int row, bool is_new) {
    struct k_field f[] = {
        { "Title", T_TITLE, KF_TEXT, true, NULL, 0, NULL, 0, 0, 0 },
        { "Project", T_PROJ, KF_REF, false, NULL, 0, &projects, P_NAME, 0, 0 },
        { "Status", T_STATUS, KF_CHOICE, false, task_status, 5, NULL, 0, 0, 0 },
        { "Priority", T_PRIO, KF_CHOICE, false, prio_name, 4, NULL, 0, 0, 0 },
        { "Due", T_DUE, KF_DATE, false, NULL, 0, NULL, 0, 0, 0 },
        { "Estimate", T_EST, KF_MINUTES, false, NULL, 0, NULL, 0, 0, 0 },
        { "Who", T_WHO, KF_REF, false, NULL, 0, &people, C_NAME, 0, 0 },
        { "Tags", T_TAGS, KF_TEXT, false, NULL, 0, NULL, 0, 0, 0 },
        { "Waits for task", T_BLOCKED, KF_REF, false, NULL, 0, &tasks, T_TITLE, 0, 0 },
        { "Notes, checklist", T_NOTES, KF_LONG, false, NULL, 0, NULL, 0, 0, 0 },
    };
    if (is_new) {
        k_seti(&tasks, row, T_STATUS, 1);
        k_seti(&tasks, row, T_PRIO, 1);
        k_seti(&tasks, row, T_CREATED, k_today());
    }
    long before = k_geti(&tasks, row, T_STATUS);
    bool ok = k_form(is_new ? "Work mode - new task" : "Work mode - task", &tasks, row, f, 10, is_new);
    if (ok) {
        long now = k_geti(&tasks, row, T_STATUS);
        if (now == 4 && before != 4) k_seti(&tasks, row, T_DONE, k_today());
        if (now != 4) k_seti(&tasks, row, T_DONE, 0);
    }
    return ok;
}

void new_task(long proj) {
    int r = k_table_add(&tasks);
    k_seti(&tasks, r, T_PROJ, proj);
    if (!edit_task(r, true)) k_table_del(&tasks, r);
    data_save();
}

bool edit_project(int row, bool is_new) {
    struct k_field f[] = {
        { "Name", P_NAME, KF_TEXT, true, NULL, 0, NULL, 0, 0, 0 },
        { "Client", P_CLIENT, KF_REF, false, NULL, 0, &people, C_NAME, 0, 0 },
        { "Status", P_STATUS, KF_CHOICE, false, project_status, 5, NULL, 0, 0, 0 },
        { "Starts", P_START, KF_DATE, false, NULL, 0, NULL, 0, 0, 0 },
        { "Deadline", P_DUE, KF_DATE, false, NULL, 0, NULL, 0, 0, 0 },
        { "Budget (time)", P_BUDGET, KF_MINUTES, false, NULL, 0, NULL, 0, 0, 0 },
        { "Hourly rate", P_RATE, KF_MONEY, false, NULL, 0, NULL, 0, 0, 0 },
        { "Description", P_DESC, KF_LONG, false, NULL, 0, NULL, 0, 0, 0 },
    };
    if (is_new) {
        k_seti(&projects, row, P_STATUS, 1);
        k_seti(&projects, row, P_START, k_today());
    }
    return k_form(is_new ? "Work mode - new project" : "Work mode - project", &projects, row, f, 8, is_new);
}

bool edit_meeting(int row, bool is_new) {
    struct k_field f[] = {
        { "Title", G_TITLE, KF_TEXT, true, NULL, 0, NULL, 0, 0, 0 },
        { "Day", G_DAY, KF_DATE, true, NULL, 0, NULL, 0, 0, 0 },
        { "Starts", G_START, KF_TIME, true, NULL, 0, NULL, 0, 0, 0 },
        { "Length", G_LEN, KF_MINUTES, false, NULL, 0, NULL, 0, 0, 0 },
        { "Where", G_WHERE, KF_TEXT, false, NULL, 0, NULL, 0, 0, 0 },
        { "People", G_PEOPLE, KF_TEXT, false, NULL, 0, NULL, 0, 0, 0 },
        { "Project", G_PROJ, KF_REF, false, NULL, 0, &projects, P_NAME, 0, 0 },
        { "Agenda", G_AGENDA, KF_LONG, false, NULL, 0, NULL, 0, 0, 0 },
        { "Notes (minutes)", G_NOTES, KF_LONG, false, NULL, 0, NULL, 0, 0, 0 },
    };
    if (is_new) k_seti(&meetings, row, G_LEN, 60);
    return k_form(is_new ? "Work mode - new meeting" : "Work mode - meeting", &meetings, row, f, 9, is_new);
}

bool edit_person(int row, bool is_new) {
    struct k_field f[] = {
        { "Name", C_NAME, KF_TEXT, true, NULL, 0, NULL, 0, 0, 0 },
        { "Company", C_COMPANY, KF_TEXT, false, NULL, 0, NULL, 0, 0, 0 },
        { "Role", C_ROLE, KF_TEXT, false, NULL, 0, NULL, 0, 0, 0 },
        { "Kind", C_KIND, KF_CHOICE, false, person_kind, 4, NULL, 0, 0, 0 },
        { "Phone", C_PHONE, KF_TEXT, false, NULL, 0, NULL, 0, 0, 0 },
        { "Email", C_EMAIL, KF_TEXT, false, NULL, 0, NULL, 0, 0, 0 },
        { "Last contact", C_LAST, KF_DATE, false, NULL, 0, NULL, 0, 0, 0 },
        { "Notes", C_NOTES, KF_LONG, false, NULL, 0, NULL, 0, 0, 0 },
    };
    return k_form(is_new ? "Work mode - new person" : "Work mode - person", &people, row, f, 8, is_new);
}

bool edit_deal(int row, bool is_new) {
    struct k_field f[] = {
        { "Deal", D_TITLE, KF_TEXT, true, NULL, 0, NULL, 0, 0, 0 },
        { "With", D_PERSON, KF_REF, false, NULL, 0, &people, C_NAME, 0, 0 },
        { "Value", D_VALUE, KF_MONEY, false, NULL, 0, NULL, 0, 0, 0 },
        { "Stage", D_STAGE, KF_CHOICE, false, deal_stage, 6, NULL, 0, 0, 0 },
        { "Expected close", D_CLOSE, KF_DATE, false, NULL, 0, NULL, 0, 0, 0 },
        { "Notes", D_NOTES, KF_LONG, false, NULL, 0, NULL, 0, 0, 0 },
    };
    return k_form(is_new ? "Work mode - new deal" : "Work mode - deal", &deals, row, f, 6, is_new);
}

bool edit_time(int row, bool is_new) {
    struct k_field f[] = {
        { "Project", M_PROJ, KF_REF, false, NULL, 0, &projects, P_NAME, 0, 0 },
        { "Task", M_TASK, KF_REF, false, NULL, 0, &tasks, T_TITLE, 0, 0 },
        { "Day", M_DAY, KF_DATE, true, NULL, 0, NULL, 0, 0, 0 },
        { "Started", M_START, KF_TIME, false, NULL, 0, NULL, 0, 0, 0 },
        { "Minutes", M_SECS, KF_MINUTES, true, NULL, 0, NULL, 0, 0, 0 },
        { "Billable", M_BILL, KF_BOOL, false, NULL, 0, NULL, 0, 0, 0 },
        { "Note", M_NOTE, KF_TEXT, false, NULL, 0, NULL, 0, 0, 0 },
    };
    /* the form works in minutes; the table keeps seconds */
    long secs = k_geti(&times, row, M_SECS);
    if (!is_new) k_seti(&times, row, M_SECS, secs / 60);
    else { k_seti(&times, row, M_DAY, k_today()); k_set(&times, row, M_SECS, ""); }
    bool ok = k_form(is_new ? "Work mode - time by hand" : "Work mode - time", &times, row, f, 7, is_new);
    if (ok) k_seti(&times, row, M_SECS, k_geti(&times, row, M_SECS) * 60);
    else if (!is_new) k_seti(&times, row, M_SECS, secs);
    return ok;
}

/* ------------------------------------------------------- the screen */

void header(int tab) {
    char right[160], t[96], d[24], tm[16];
    long now = k_now();
    timer_status(t, sizeof t);
    snprintf(right, sizeof right, "%s%s%s  %s", t, t[0] ? "   " : "", k_fmt_day(d, sizeof d, now / 86400), k_fmt_time(tm, sizeof tm, (int)(now % 86400 / 60)));
    k_header(cfg.company[0] ? cfg.company : "Work mode", right);
    static const char *const names[] = { "Dashboard", "Tasks", "Projects", "Time", "Calendar", "Clients", "Notes", "Reports" };
    char line[512] = "";
    for (int i = 0; i < 8; i++) {
        char x[64];
        snprintf(x, sizeof x, "%s%d %s" K_RESET "   ", i == tab ? K_YEL : K_DIM, i + 1, names[i]);
        strlcat(line, x, sizeof line);
    }
    k_line(" %s", line);
}

int tab_key(int k, int self) {
    if (k == K_TAB) return (self + 1) % 8;
    if (k >= '1' && k <= '8') return k - '1';
    if (k == K_ESC) { menu(); return self; }
    return -1;
}

int key(void) {
    int k = k_key_wait(1000);
    if (k == K_NONE) k_watch_media();
    return k;
}

static bool choose_stick(char *out, size_t len) {
    char vols[8][64];
    int n = k_sticks(vols, 8);
    if (!n) { k_flash("plug in a USB stick (FAT32) first"); return false; }
    int c = 0;
    if (n > 1) {
        const char *items[8];
        for (int i = 0; i < n; i++) items[i] = vols[i];
        c = k_menu("Work mode - which USB stick?", items, n, 0);
        if (c < 0) return false;
    }
    strlcpy(out, vols[c], len);
    return true;
}

static bool write_on(const char *stick, const char *name, const char *text, size_t len) {
    char path[ITX_PATH_MAX], dir[ITX_PATH_MAX];
    snprintf(path, sizeof path, "%s/%s", stick, name);
    strlcpy(dir, path, sizeof dir);
    char *s = strrchr(dir, '/');
    if (s) { *s = 0; k_mkdirs(dir); }
    return write_file(path, text, len, false) >= 0;
}

bool export_to_stick(const char *what, const char *name, const char *text, size_t len) {
    char stick[64];
    if (!choose_stick(stick, sizeof stick)) return false;
    if (!write_on(stick, name, text, len)) { k_flash("%s/%s could not be written", stick, name); return false; }
    sync();
    k_flash("%s written to %s/%s", what, stick, name);
    return true;
}

/* --------------------------------------------------------------- menu */

static void settings_menu(void) {
    static const char *const days[] = { "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday" };
    for (;;) {
        char it[8][120], m[32];
        snprintf(it[0], 120, "Your name: %s", cfg.name[0] ? cfg.name : "(none)");
        snprintf(it[1], 120, "Company: %s", cfg.company[0] ? cfg.company : "(none)");
        snprintf(it[2], 120, "Currency: %s", cfg.currency);
        snprintf(it[3], 120, "Hourly rate for projects without one: %s %s", k_money(m, sizeof m, cfg.rate), cfg.currency);
        snprintf(it[4], 120, "Hours of a working day: %d", cfg.day_hours);
        snprintf(it[5], 120, "The week starts on %s", days[cfg.week_start]);
        struct itx_stat st;
        snprintf(it[6], 120, "Passcode: %s", stat(HOME "/passcode", &st) == 0 ? "set" : "none");
        snprintf(it[7], 120, "Back");
        const char *items[8] = { it[0], it[1], it[2], it[3], it[4], it[5], it[6], it[7] };
        int c = k_menu("Work mode - settings", items, 8, 0);
        if (c < 0 || c == 7) return;
        char b[64];
        long v;
        if (c == 0) { if (k_ask("Your name: ", b, sizeof b)) strlcpy(cfg.name, b, sizeof cfg.name); else cfg.name[0] = 0; }
        else if (c == 1) { if (k_ask("Company: ", b, sizeof b)) strlcpy(cfg.company, b, sizeof cfg.company); else cfg.company[0] = 0; }
        else if (c == 2) { if (k_ask("Currency (EGP, USD, EUR, SAR ...): ", b, sizeof b)) strlcpy(cfg.currency, b, sizeof cfg.currency); }
        else if (c == 3) { if (k_ask("Hourly rate: ", b, sizeof b) && k_parse_money(b, &v)) cfg.rate = v; }
        else if (c == 4) { if (k_ask_long("Hours of a working day: ", &v) && v > 0 && v <= 24) cfg.day_hours = (int)v; }
        else if (c == 5) { int d = k_menu("The first day of the week", days, 7, cfg.week_start); if (d >= 0) cfg.week_start = d; }
        else if (c == 6) k_lock_set(HOME "/passcode");
        settings_save();
    }
}

static void tools_menu(void) {
    static const char *const items[] = { "Calculator", "File manager", "Text editor", "Checksums (SHA-256)", "Unit converter", "Back" };
    static const char *const paths[] = { "/dex/calc.xvc", "/dex/fm.xvr", "/dex/edit.xvr", "/dex/sum.xvr", "/dex/convert.xvc" };
    for (;;) {
        int c = k_menu("Work mode - tools", items, 6, 0);
        if (c < 0 || c == 5) return;
        struct itx_stat st;
        if (stat(paths[c], &st)) { k_flash("%s is missing", paths[c]); continue; }
        k_run(paths[c], NULL, 0);
    }
}

static void export_all(void) {
    static const char *const files[] = { "projects", "tasks", "time", "meetings", "people", "deals", "notes", "invoices",
                                         "expenses", "settings" };
    char d[16], stick[64];
    if (!choose_stick(stick, sizeof stick)) return;
    k_fmt_date(d, sizeof d, k_today());
    int done = 0, bad = 0;
    for (int i = 0; i < 10; i++) {
        char from[ITX_PATH_MAX], name[96];
        snprintf(from, sizeof from, HOME "/%s.txt", files[i]);
        snprintf(name, sizeof name, "work-export-%s/%s.txt", d, files[i]);
        size_t len;
        char *t = read_file(from, &len, 64u << 20);
        if (!t) continue;
        if (write_on(stick, name, t, len)) done++;
        else bad++;
        free(t);
    }
    sync();
    int e = bad ? -1 : eject(stick);
    k_flash("%d tables written to %s/work-export-%s%s%s", done, stick, d, bad ? ", some could not be written" : "",
            e ? "" : "; the stick can be unplugged");
}

void menu(void) {
    static const char *const items[] = {
        "Invoices", "Expenses", "Settings: name, company, currency, rate, week, passcode", "Tools: calculator, files, editor",
        "Export everything to a USB stick", "Keys and help", "System: restart, power off, time zone, restore the system", "Back",
    };
    int c = k_menu("Work mode - menu", items, 8, 0);
    switch (c) {
    case 0: invoices_menu(); break;
    case 1: expenses_menu(); break;
    case 2: settings_menu(); break;
    case 3: tools_menu(); break;
    case 4: export_all(); break;
    case 5: k_message("Work mode - keys", HELP); break;
    case 6: k_system_menu("Work mode", ABOUT); break;
    }
}

/* -------------------------------------------------------------- start */

static void first_start(void) {
    k_frame();
    k_header("Work mode", NULL);
    k_pad_to(3);
    k_line("     " K_CYAN "Welcome to Work mode." K_RESET);
    k_line("%s", "");
    k_line("     This computer is now a workplace: tasks, projects, a timer and timesheets,");
    k_line("     meetings, clients and deals, notes, invoices, expenses and reports.");
    k_line("%s", "");
    k_line("     Start with a project (3, then n), then its tasks (2, then n).");
    k_footer("");
    char b[64];
    if (k_ask("Your name (or just Enter): ", b, sizeof b)) strlcpy(cfg.name, b, sizeof cfg.name);
    if (k_ask("Your company (or just Enter): ", b, sizeof b)) strlcpy(cfg.company, b, sizeof cfg.company);
    if (k_ask("Currency (Enter: EGP): ", b, sizeof b)) strlcpy(cfg.currency, b, sizeof cfg.currency);
    settings_save();
}

int main(int argc, char **argv) {
    k_begin();
    k_frame();
    struct itx_stat st;
    bool first = stat(HOME "/settings.txt", &st) != 0;
    k_mkdirs(HOME);
    settings_load();
    data_load();
    timer_load();
    if (first) first_start();
    k_lock(HOME "/passcode", "Work mode");
    int view = 0;
    for (;;) {
        switch (view) {
        case 0: view = view_dashboard(); break;
        case 1: view = view_tasks(); break;
        case 2: view = view_projects(); break;
        case 3: view = view_time(); break;
        case 4: view = view_calendar(); break;
        case 5: view = view_clients(); break;
        case 6: view = view_notes(); break;
        default: view = view_reports(); break;
        }
    }
}
