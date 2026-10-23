/* Work mode: the dashboard and the reports. */
#include "work.h"

static void section(const char *t) { k_line(" " K_YEL "%s" K_RESET, t); }

int view_dashboard(void) {
    for (;;) {
        long today = k_today(), w0 = week_first(today);
        int now = k_minute_of_day();
        k_frame();
        header(0);
        char a[48], b[48], c[48], bar[512];
        long week = proj_secs(0, w0, w0 + 6), day = proj_secs(0, today, today) + timer_secs();
        k_line(" " K_CYAN "%s%s%s." K_RESET "   today %s   this week %s of %d h  %s", now < 12 * 60 ? "Good morning" : now < 18 * 60 ? "Good afternoon" : "Good evening",
               cfg.name[0] ? ", " : "", cfg.name, k_fmt_dur(a, sizeof a, day), k_fmt_dur(b, sizeof b, week), cfg.day_hours * 5,
               k_bar(bar, sizeof bar, week, cfg.day_hours * 5L * 3600, 20));
        k_rule();
        /* meetings today */
        section("Meetings today");
        int n = 0;
        for (int i = 0; i < meetings.n; i++) {
            if (k_geti(&meetings, i, G_DAY) != today) continue;
            long s = k_geti(&meetings, i, G_START), len = k_geti(&meetings, i, G_LEN);
            char t[300];
            k_fit(t, sizeof t, k_get(&meetings, i, G_TITLE), k_cols - 30);
            k_line("   %s%s-%s" K_RESET "  %s%s", now >= s && now < s + len ? K_GRN : now >= s + len ? K_DIM : "", k_fmt_time(a, sizeof a, (int)s),
                   k_fmt_time(b, sizeof b, (int)(s + len)), now >= s && now < s + len ? K_GRN "now " K_RESET : "", t);
            n++;
        }
        if (!n) k_line("   " K_DIM "none" K_RESET);
        k_line("%s", "");
        /* tasks: overdue, today, the next days */
        section("Tasks due");
        n = 0;
        for (int pass = 0; pass < 2; pass++)
            for (int i = 0; i < tasks.n && n < 8; i++) {
                long due = k_geti(&tasks, i, T_DUE), st = k_geti(&tasks, i, T_STATUS);
                if (st == 4 || !due) continue;
                if (pass == 0 ? due > today : (due <= today || due > today + 3)) continue;
                char rel[32], t[300], pn[64];
                k_fit(pn, sizeof pn, proj_name(k_geti(&tasks, i, T_PROJ)), 16);
                k_fit(t, sizeof t, k_get(&tasks, i, T_TITLE), k_cols - 50);
                k_line("   %s%-12s" K_RESET " %s%-6s" K_RESET " %s " K_DIM K_SEP K_RESET " %s", due < today ? K_RED : due == today ? K_YEL : K_CYAN,
                       k_fmt_rel(rel, sizeof rel, due), prio_colour(k_geti(&tasks, i, T_PRIO)), prio_name[k_geti(&tasks, i, T_PRIO) % 4], pn, t);
                n++;
            }
        if (!n) k_line("   " K_GRN "nothing due in the next days" K_RESET);
        k_line("%s", "");
        /* projects that need a look */
        section("Projects to look at");
        n = 0;
        for (int i = 0; i < projects.n && n < 5; i++) {
            if (k_geti(&projects, i, P_STATUS) != 1) continue;
            long id = k_geti(&projects, i, P_ID), due = k_geti(&projects, i, P_DUE), budget = k_geti(&projects, i, P_BUDGET) * 60;
            long used = proj_secs(id, 0, 1L << 40);
            const char *why = NULL;
            char w[96];
            if (due && due < today) { snprintf(w, sizeof w, K_RED "late since %s" K_RESET, k_fmt_day(a, sizeof a, due)); why = w; }
            else if (budget && used > budget) { snprintf(w, sizeof w, K_RED "over budget: %s of %s" K_RESET, k_fmt_dur(a, sizeof a, used), k_fmt_dur(b, sizeof b, budget)); why = w; }
            else if (budget && used * 10 > budget * 8) { snprintf(w, sizeof w, K_YEL "%ld%% of the budget used" K_RESET, used * 100 / budget); why = w; }
            else if (due && due - today <= 7) { snprintf(w, sizeof w, K_YEL "deadline %s" K_RESET, k_fmt_rel(a, sizeof a, due)); why = w; }
            if (!why) continue;
            char nm[64];
            k_fit(nm, sizeof nm, k_get(&projects, i, P_NAME), 24);
            k_line("   %s " K_DIM K_SEP K_RESET " %s", why, nm);
            n++;
        }
        if (!n) k_line("   " K_GRN "every active project is on track" K_RESET);
        k_line("%s", "");
        /* money */
        section("Money");
        long open_deals = 0, unpaid = 0, late = 0, unbilled = 0;
        for (int i = 0; i < deals.n; i++) if (k_geti(&deals, i, D_STAGE) < 4) open_deals += k_geti(&deals, i, D_VALUE);
        for (int i = 0; i < invoices.n; i++) {
            if (k_geti(&invoices, i, I_STATUS) == 2) continue;
            unpaid += k_geti(&invoices, i, I_AMOUNT);
            if (k_geti(&invoices, i, I_STATUS) == 1 && k_geti(&invoices, i, I_DUE) < today) late += k_geti(&invoices, i, I_AMOUNT);
        }
        for (int i = 0; i < times.n; i++)
            if (k_geti(&times, i, M_BILL) && !k_geti(&times, i, M_INVOICE))
                unbilled += k_geti(&times, i, M_SECS) * proj_rate(k_geti(&times, i, M_PROJ)) / 3600;
        k_line("   not invoiced yet %s %s   invoices not paid %s %s%s   deals open %s %s", k_money(a, sizeof a, unbilled), cur(),
               k_money(b, sizeof b, unpaid), cur(), late ? K_RED " (late!)" K_RESET : "", k_money(c, sizeof c, open_deals), cur());
        k_line("%s", "");
        section("This week by project");
        long most = 1;
        long ids[32], secs[32];
        int np = 0;
        for (int i = 0; i < projects.n && np < 32; i++) {
            long s = proj_secs(k_geti(&projects, i, P_ID), w0, w0 + 6);
            if (!s) continue;
            ids[np] = k_geti(&projects, i, P_ID);
            secs[np++] = s;
            if (s > most) most = s;
        }
        for (int i = 0; i < np && k_used() < k_rows - 2; i++) {
            char nm[64];
            k_fit(nm, sizeof nm, proj_name(ids[i]), 24);
            k_line("   %s %8s " K_DIM K_SEP K_RESET " %s", k_bar(bar, sizeof bar, secs[i], most, k_cols / 3), k_fmt_dur(a, sizeof a, secs[i]), nm);
        }
        if (!np) k_line("   " K_DIM "no time yet this week (view 4, s starts the timer)" K_RESET);
        k_footer("n new task   s start the timer   x stop it   m new meeting   1-8 views   Esc menu");
        k_show();
        int k = key();
        if (k == K_NONE) continue;
        int v = tab_key(k, 0);
        if (v >= 0 && v != 0) return v;
        if (k == 'n') new_task(0);
        else if (k == 'x') { if (timer.on) timer_stop(); else k_flash("no timer runs"); }
        else if (k == 's') {
            long proj = timer.proj, task = 0;
            if (pick_project("Work mode - the timer: which project?", &proj, true) == 0 &&
                (!proj || pick_task("Work mode - the timer: which task?", proj, &task) == 0))
                timer_start(proj, task, NULL);
        } else if (k == 'm') {
            int r = k_table_add(&meetings);
            k_seti(&meetings, r, G_DAY, today);
            if (!edit_meeting(r, true)) k_table_del(&meetings, r);
            data_save();
        }
    }
}

/* ------------------------------------------------------------ reports */

static void period(int which, long *from, long *to, char *name, size_t len) {
    long today = k_today(), w0 = week_first(today);
    int y, m, d;
    k_civil(today, &y, &m, &d);
    switch (which) {
    case 0: *from = w0; *to = w0 + 6; snprintf(name, len, "this week"); break;
    case 1: *from = w0 - 7; *to = w0 - 1; snprintf(name, len, "last week"); break;
    case 2: *from = k_days(y, m, 1); *to = k_days(y, m, k_month_days(y, m)); snprintf(name, len, "%s %d", k_month_name(m), y); break;
    case 3: {
        int pm = m == 1 ? 12 : m - 1, py = m == 1 ? y - 1 : y;
        *from = k_days(py, pm, 1);
        *to = k_days(py, pm, k_month_days(py, pm));
        snprintf(name, len, "%s %d", k_month_name(pm), py);
        break;
    }
    default: *from = k_days(y, 1, 1); *to = k_days(y, 12, 31); snprintf(name, len, "the year %d", y);
    }
}

/* the report as text: shown on the screen and exported */
static char *report(long from, long to, const char *name) {
    size_t cap = 16384, o = 0;
    char *t = malloc(cap), a[48], b[48], c[48];
#define OUT(...) do { if (o + 600 > cap) { cap *= 2; t = realloc(t, cap); } o += (size_t)snprintf(t + o, cap - o, __VA_ARGS__); } while (0)
    OUT("Report for %s (%s to %s)%s%s\n\n", name, k_fmt_date(a, sizeof a, from), k_fmt_date(b, sizeof b, to), cfg.company[0] ? " - " : "", cfg.company);
    long all = 0, bill = 0, money = 0, spent = 0;
    for (int i = 0; i < times.n; i++) {
        long d = k_geti(&times, i, M_DAY);
        if (d < from || d > to) continue;
        long s = k_geti(&times, i, M_SECS);
        all += s;
        if (k_geti(&times, i, M_BILL)) { bill += s; money += s * proj_rate(k_geti(&times, i, M_PROJ)) / 3600; }
    }
    for (int i = 0; i < expenses.n; i++) {
        long d = k_geti(&expenses, i, X_DAY);
        if (d >= from && d <= to) spent += k_geti(&expenses, i, X_AMOUNT);
    }
    OUT("TIME      %s worked, %s billable (%ld%%)\n", k_fmt_dur(a, sizeof a, all), k_fmt_dur(b, sizeof b, bill), all ? bill * 100 / all : 0);
    OUT("MONEY     billable work %s %s, expenses %s %s, the difference %s %s\n\n", k_money(a, sizeof a, money), cur(),
        k_money(b, sizeof b, spent), cur(), k_money(c, sizeof c, money - spent), cur());
    OUT("BY PROJECT\n");
    for (int p = -1; p < projects.n; p++) {
        long id = p < 0 ? 0 : k_geti(&projects, p, P_ID), s = 0, bs = 0;
        for (int i = 0; i < times.n; i++) {
            long d = k_geti(&times, i, M_DAY);
            if (d < from || d > to || k_geti(&times, i, M_PROJ) != id) continue;
            s += k_geti(&times, i, M_SECS);
            if (k_geti(&times, i, M_BILL)) bs += k_geti(&times, i, M_SECS);
        }
        if (!s) continue;
        OUT("  %-32.32s %9s  billable %9s  %12s %s\n", proj_name(id), k_fmt_dur(a, sizeof a, s), k_fmt_dur(b, sizeof b, bs),
            k_money(c, sizeof c, bs * proj_rate(id) / 3600), cur());
    }
    OUT("\nBY CLIENT\n");
    for (int pr = 0; pr < people.n; pr++) {
        long pid = k_geti(&people, pr, C_ID), s = 0, m = 0;
        for (int i = 0; i < times.n; i++) {
            long d = k_geti(&times, i, M_DAY), p = k_geti(&times, i, M_PROJ);
            int r = p ? k_table_row(&projects, p) : -1;
            if (d < from || d > to || r < 0 || k_geti(&projects, r, P_CLIENT) != pid) continue;
            s += k_geti(&times, i, M_SECS);
            if (k_geti(&times, i, M_BILL)) m += k_geti(&times, i, M_SECS) * proj_rate(p) / 3600;
        }
        if (s) OUT("  %-32.32s %9s  %12s %s\n", k_get(&people, pr, C_NAME), k_fmt_dur(a, sizeof a, s), k_money(b, sizeof b, m), cur());
    }
    int made = 0, done = 0, late = 0, open = 0;
    for (int i = 0; i < tasks.n; i++) {
        long cr = k_geti(&tasks, i, T_CREATED), dn = k_geti(&tasks, i, T_DONE), due = k_geti(&tasks, i, T_DUE);
        if (cr >= from && cr <= to) made++;
        if (dn >= from && dn <= to) { done++; if (due && dn > due) late++; }
        if (!dn && k_geti(&tasks, i, T_STATUS) != 4) open++;
    }
    OUT("\nTASKS     %d new, %d done (%d after their due day), %d open now\n", made, done, late, open);
    int meet = 0;
    long meet_min = 0;
    for (int i = 0; i < meetings.n; i++) {
        long d = k_geti(&meetings, i, G_DAY);
        if (d >= from && d <= to) { meet++; meet_min += k_geti(&meetings, i, G_LEN); }
    }
    OUT("MEETINGS  %d, %s together\n", meet, k_fmt_dur(a, sizeof a, meet_min * 60));
    long won = 0, lost = 0;
    for (int i = 0; i < deals.n; i++) {
        long d = k_geti(&deals, i, D_CLOSE);
        if (d < from || d > to) continue;
        if (k_geti(&deals, i, D_STAGE) == 4) won += k_geti(&deals, i, D_VALUE);
        if (k_geti(&deals, i, D_STAGE) == 5) lost += k_geti(&deals, i, D_VALUE);
    }
    OUT("DEALS     won %s %s, lost %s %s\n", k_money(a, sizeof a, won), cur(), k_money(b, sizeof b, lost), cur());
#undef OUT
    return t;
}

int view_reports(void) {
    static int which;
    for (;;) {
        long from, to;
        char name[48];
        period(which, &from, &to, name, sizeof name);
        char *t = report(from, to, name);
        k_frame();
        header(7);
        static const char *const names[] = { "this week", "last week", "this month", "last month", "this year" };
        char line[300] = "";
        for (int i = 0; i < 5; i++) {
            char x[64];
            snprintf(x, sizeof x, "%s%s" K_RESET "   ", i == which ? K_YEL : K_DIM, names[i]);
            strlcat(line, x, sizeof line);
        }
        k_line(" %s", line);
        k_rule();
        for (const char *p = t; *p && k_used() < k_rows - 1;) {
            const char *e = strchr(p, '\n');
            size_t n = e ? (size_t)(e - p) : strlen(p);
            char row[700], f[700];
            snprintf(row, sizeof row, "%.*s", (int)(n < 690 ? n : 690), p);
            k_fit(f, sizeof f, row, k_cols - 3);
            k_line(" %s%s" K_RESET, row[0] && row[0] != ' ' && isupper((unsigned char)row[0]) && row[1] && isupper((unsigned char)row[1]) ? K_CYAN : "", f);
            if (!e) break;
            p = e + 1;
        }
        k_footer("[ ] the period   e export to a USB stick   1-8 views   Esc menu");
        k_show();
        int k = key();
        if (k == '[') which = (which + 4) % 5;
        else if (k == ']') which = (which + 1) % 5;
        else if (k == 'e') {
            char fn[96], d[16];
            snprintf(fn, sizeof fn, "work-report-%s.txt", k_fmt_date(d, sizeof d, k_today()));
            export_to_stick("the report", fn, t, strlen(t));
        }
        free(t);
        if (k == K_NONE) continue;
        int v = tab_key(k, 7);
        if (v >= 0 && v != 7) return v;
    }
}
