/* Work mode: the timer and the timesheet, invoices, expenses. */
#include "work.h"

#define SEL(i, s) ((i) == (s) ? K_YEL "\xe2\x96\xb6" : " ")

static void start_menu(void) {
    long proj = timer.proj, task = 0;
    if (pick_project("Work mode - the timer: which project?", &proj, true)) return;
    if (proj && pick_task("Work mode - the timer: which task?", proj, &task)) return;
    char note[128] = "";
    k_ask("A note for this time (or just Enter): ", note, sizeof note);
    timer_start(proj, task, note);
}

static int cmp_time(char *const *a, char *const *b) {
    long x = atol(a[M_DAY]) * 1440 + atol(a[M_START]), y = atol(b[M_DAY]) * 1440 + atol(b[M_START]);
    return x > y ? -1 : x < y;
}

int view_time(void) {
    static long week;
    struct k_list l = { 0, 0, 0 };
    if (!week) week = week_first(k_today());
    for (;;) {
        k_table_sort(&times, cmp_time);
        k_frame();
        header(3);
        char a[32], b[32];
        if (timer.on) {
            char clock[32], rows[5][512];
            k_big(k_fmt_clock(clock, sizeof clock, timer_secs()), rows);
            int w = k_width(rows[0]);
            k_line("   " K_GRN "the timer runs" K_RESET "  %s%s%s%s", proj_name(timer.proj), timer.task ? " " K_DIM K_SEP K_RESET " " : "",
                   timer.task ? task_title(timer.task) : "", timer.note[0] ? "   " : "");
            for (int r = 0; r < 5; r++) k_line("%*s" K_GRN "%s" K_RESET, (k_cols - w) / 2, "", rows[r]);
        } else {
            k_line("   " K_DIM "no timer runs: s starts one (on a task: s in the task list)" K_RESET);
        }
        k_rule();
        /* the week's timesheet: one row per project */
        long projs[64];
        int np = 0;
        for (int i = 0; i < times.n; i++) {
            long d = k_geti(&times, i, M_DAY), p = k_geti(&times, i, M_PROJ);
            if (d < week || d > week + 6) continue;
            int k = 0;
            while (k < np && projs[k] != p) k++;
            if (k == np && np < 64) projs[np++] = p;
        }
        char line[1024];
        int o = snprintf(line, sizeof line, " %-18s", "week of");
        for (int d = 0; d < 7; d++) o += snprintf(line + o, sizeof line - o, " %8.3s", k_wday_name(k_weekday(week + d)));
        snprintf(line + o, sizeof line - o, " %9s", "total");
        k_line(K_CYAN "%s" K_RESET "   %s", line, k_fmt_day(a, sizeof a, week));
        long day_tot[7] = { 0 }, all = 0;
        for (int k = 0; k < np && k_used() < k_rows / 2 + 2; k++) {
            o = 0;
            long tot = 0;
            for (int d = 0; d < 7; d++) {
                long s = 0;
                for (int i = 0; i < times.n; i++)
                    if (k_geti(&times, i, M_DAY) == week + d && k_geti(&times, i, M_PROJ) == projs[k]) s += k_geti(&times, i, M_SECS);
                day_tot[d] += s;
                tot += s;
                o += snprintf(line + o, sizeof line - o, " %8s", s ? k_fmt_clock(a, sizeof a, s / 60 * 60) : "-");
            }
            all += tot;
            char nm[64];
            k_fit(nm, sizeof nm, proj_name(projs[k]), 18);
            k_line(" %s" K_DIM K_SEP K_RESET "%s %9s", nm, line, k_fmt_dur(b, sizeof b, tot));
        }
        o = 0;
        for (int d = 0; d < 7; d++) o += snprintf(line + o, sizeof line - o, " %8s", day_tot[d] ? k_fmt_clock(a, sizeof a, day_tot[d] / 60 * 60) : "-");
        k_line(K_YEL " %-18s" K_RESET K_DIM K_SEP K_RESET "%s " K_YEL "%9s" K_RESET "   of %d h", "all", line, k_fmt_dur(b, sizeof b, all), cfg.day_hours * 5);
        k_rule();
        /* the week's entries */
        int idx[2048], n = 0;
        for (int i = 0; i < times.n && n < 2048; i++) {
            long d = k_geti(&times, i, M_DAY);
            if (d >= week && d <= week + 6) idx[n++] = i;
        }
        l.n = n;
        int rows = k_rows - k_used() - 1;
        k_list_fit(&l, rows);
        for (int j = l.top; j < n && j < l.top + rows; j++) {
            int i = idx[j];
            char d[24], tm[16], du[24], pn[64], t[300];
            k_fit(pn, sizeof pn, proj_name(k_geti(&times, i, M_PROJ)), 16);
            snprintf(t, sizeof t, "%s%s%s", k_geti(&times, i, M_TASK) ? task_title(k_geti(&times, i, M_TASK)) : "",
                     k_get(&times, i, M_NOTE)[0] && k_geti(&times, i, M_TASK) ? " - " : "", k_get(&times, i, M_NOTE));
            char tt[300];
            k_fit(tt, sizeof tt, t, k_cols - 62);
            k_line("%s %-11s %s  %8s  %s%-8s" K_RESET " %s " K_DIM K_SEP K_RESET " %s", SEL(j, l.sel), k_fmt_day(d, sizeof d, k_geti(&times, i, M_DAY)),
                   k_fmt_time(tm, sizeof tm, (int)k_geti(&times, i, M_START)), k_fmt_dur(du, sizeof du, k_geti(&times, i, M_SECS)),
                   k_geti(&times, i, M_INVOICE) ? K_DIM : k_geti(&times, i, M_BILL) ? K_GRN : K_DIM,
                   k_geti(&times, i, M_INVOICE) ? "invoiced" : k_geti(&times, i, M_BILL) ? "billable" : "internal", pn, tt);
        }
        if (!n) k_line("   " K_DIM "no time this week" K_RESET);
        k_footer("s start  x stop  a add by hand  Enter change  b billable  d delete  [ ] weeks  1-8 views  Esc menu");
        k_show();
        int k = k_key_wait(timer.on ? 1000 : 1000);
        if (k == K_NONE) { k_watch_media(); continue; }
        if (k_list_key(&l, k, rows)) continue;
        int v = tab_key(k, 3);
        if (v >= 0 && v != 3) return v;
        int row = n ? idx[l.sel] : -1;
        if (k == 's') start_menu();
        else if (k == 'x') { if (timer.on) timer_stop(); else k_flash("no timer runs"); }
        else if (k == '[') { week -= 7; l.sel = 0; }
        else if (k == ']') { week += 7; l.sel = 0; }
        else if (k == 'a') { int r = k_table_add(&times); k_seti(&times, r, M_BILL, 1); if (!edit_time(r, true)) k_table_del(&times, r); data_save(); }
        else if (k == K_ENTER && row >= 0) { edit_time(row, false); data_save(); }
        else if (k == 'b' && row >= 0) { k_seti(&times, row, M_BILL, !k_geti(&times, row, M_BILL)); data_save(); }
        else if ((k == 'd' || k == KEY_DELETE) && row >= 0) {
            if (k_geti(&times, row, M_INVOICE)) k_flash("this time is on an invoice: delete the invoice first");
            else if (k_confirm("Delete this time?")) { k_table_del(&times, row); data_save(); }
        }
    }
}

/* ----------------------------------------------------------- invoices */

/* the text of an invoice, as it is kept and exported */
static char *invoice_text(int ir) {
    long id = k_geti(&invoices, ir, I_ID), client = k_geti(&invoices, ir, I_CLIENT);
    size_t cap = 8192, o = 0;
    char *t = malloc(cap), a[48], b[48], c[48];
    int cr = k_table_row(&people, client);
    o += (size_t)snprintf(t + o, cap - o, "INVOICE %s\n\n", k_get(&invoices, ir, I_NUMBER));
    o += (size_t)snprintf(t + o, cap - o, "From:   %s%s%s\n", cfg.company[0] ? cfg.company : cfg.name, cfg.company[0] && cfg.name[0] ? ", " : "",
                          cfg.company[0] ? cfg.name : "");
    o += (size_t)snprintf(t + o, cap - o, "To:     %s%s%s\n", person_name(client), cr >= 0 && k_get(&people, cr, C_COMPANY)[0] ? ", " : "",
                          cr >= 0 ? k_get(&people, cr, C_COMPANY) : "");
    o += (size_t)snprintf(t + o, cap - o, "Date:   %s\nDue:    %s\nPeriod: %s to %s\n\n", k_fmt_date(a, sizeof a, k_geti(&invoices, ir, I_DAY)),
                          k_fmt_date(b, sizeof b, k_geti(&invoices, ir, I_DUE)), k_fmt_date(c, sizeof c, k_geti(&invoices, ir, I_FROM)),
                          k_fmt_date(a, sizeof a, k_geti(&invoices, ir, I_TO)));
    o += (size_t)snprintf(t + o, cap - o, "%-40s %10s %12s %14s\n", "Work", "Hours", "Rate", "Amount");
    long total = 0;
    for (int p = 0; p < projects.n; p++) {
        long pid = k_geti(&projects, p, P_ID), secs = 0;
        for (int i = 0; i < times.n; i++) if (k_geti(&times, i, M_INVOICE) == id && k_geti(&times, i, M_PROJ) == pid) secs += k_geti(&times, i, M_SECS);
        if (!secs) continue;
        long rate = proj_rate(pid), amount = secs * rate / 3600;
        total += amount;
        if (o + 512 > cap) { cap *= 2; t = realloc(t, cap); }
        o += (size_t)snprintf(t + o, cap - o, "%-40.40s %7ld:%02ld %12s %14s\n", k_get(&projects, p, P_NAME), secs / 3600, secs / 60 % 60,
                              k_money(a, sizeof a, rate), k_money(b, sizeof b, amount));
    }
    o += (size_t)snprintf(t + o, cap - o, "\n%-40s %10s %12s %14s %s\n", "Total", "", "", k_money(a, sizeof a, total), cur());
    o += (size_t)snprintf(t + o, cap - o, "\nStatus: %s\n", invoice_status[k_geti(&invoices, ir, I_STATUS) % 3]);
    return t;
}

static void new_invoice(void) {
    if (!people.n) { k_flash("add the client first (view 6, n)"); return; }
    const char **items = malloc(sizeof(char *) * people.n);
    for (int i = 0; i < people.n; i++) items[i] = k_get(&people, i, C_NAME);
    int c = k_menu("Work mode - invoice: for which client?", items, people.n, 0);
    free(items);
    if (c < 0) return;
    long client = k_geti(&people, c, C_ID), from, to;
    char b[32];
    if (!k_ask("From (a date; Enter: the first of this month): ", b, sizeof b)) {
        int y, m, d;
        k_civil(k_today(), &y, &m, &d);
        from = k_days(y, m, 1);
    } else if (!k_parse_date(b, &from)) { k_flash("not a date: %s", b); return; }
    if (!k_ask("To (Enter: today): ", b, sizeof b)) to = k_today();
    else if (!k_parse_date(b, &to)) { k_flash("not a date: %s", b); return; }
    /* billable time of the client's projects, not on an invoice yet */
    long secs = 0, amount = 0;
    int n = 0;
    for (int i = 0; i < times.n; i++) {
        long p = k_geti(&times, i, M_PROJ), d = k_geti(&times, i, M_DAY);
        int pr = p ? k_table_row(&projects, p) : -1;
        if (pr < 0 || k_geti(&projects, pr, P_CLIENT) != client || !k_geti(&times, i, M_BILL) || k_geti(&times, i, M_INVOICE)) continue;
        if (d < from || d > to) continue;
        secs += k_geti(&times, i, M_SECS);
        amount += k_geti(&times, i, M_SECS) * proj_rate(p) / 3600;
        n++;
    }
    if (!n) { k_flash("no billable time of %s's projects in that period that is not invoiced", person_name(client)); return; }
    char q[200], m[32], h[32];
    snprintf(q, sizeof q, "Invoice %s for %s (%d entries): %s %s?", k_fmt_dur(h, sizeof h, secs), person_name(client), n, k_money(m, sizeof m, amount), cur());
    if (!k_confirm(q)) return;
    int r = k_table_add(&invoices);
    long id = k_geti(&invoices, r, I_ID);
    int y, mo, d;
    k_civil(k_today(), &y, &mo, &d);
    char num[32];
    snprintf(num, sizeof num, "INV-%04d-%03ld", y, id);
    k_set(&invoices, r, I_NUMBER, num);
    k_seti(&invoices, r, I_CLIENT, client);
    k_seti(&invoices, r, I_FROM, from);
    k_seti(&invoices, r, I_TO, to);
    k_seti(&invoices, r, I_AMOUNT, amount);
    k_seti(&invoices, r, I_STATUS, 0);
    k_seti(&invoices, r, I_DAY, k_today());
    k_seti(&invoices, r, I_DUE, k_today() + 30);
    for (int i = 0; i < times.n; i++) {
        long p = k_geti(&times, i, M_PROJ), dd = k_geti(&times, i, M_DAY);
        int pr = p ? k_table_row(&projects, p) : -1;
        if (pr < 0 || k_geti(&projects, pr, P_CLIENT) != client || !k_geti(&times, i, M_BILL) || k_geti(&times, i, M_INVOICE)) continue;
        if (dd < from || dd > to) continue;
        k_seti(&times, i, M_INVOICE, id);
    }
    data_save();
    char *t = invoice_text(r), path[ITX_PATH_MAX];
    k_mkdirs(HOME "/invoices");
    snprintf(path, sizeof path, HOME "/invoices/%s.txt", num);
    k_save_file(path, t, strlen(t));
    free(t);
    k_flash("invoice %s made: %s %s", num, k_money(m, sizeof m, amount), cur());
}

void invoices_menu(void) {
    struct k_list l = { 0, 0, 0 };
    for (;;) {
        l.n = invoices.n;
        k_frame();
        header(-1);
        long open = 0, paid = 0;
        for (int i = 0; i < invoices.n; i++) {
            if (k_geti(&invoices, i, I_STATUS) == 2) paid += k_geti(&invoices, i, I_AMOUNT);
            else open += k_geti(&invoices, i, I_AMOUNT);
        }
        char a[32], b[32];
        k_line(" " K_CYAN "Invoices" K_RESET "   not paid %s %s   paid %s %s", k_money(a, sizeof a, open), cur(), k_money(b, sizeof b, paid), cur());
        k_rule();
        int rows = k_rows - k_used() - 1;
        k_list_fit(&l, rows);
        for (int i = l.top; i < l.n && i < l.top + rows; i++) {
            long st = k_geti(&invoices, i, I_STATUS), due = k_geti(&invoices, i, I_DUE);
            char d[24], m[32], nm[64];
            k_fit(nm, sizeof nm, person_name(k_geti(&invoices, i, I_CLIENT)), 24);
            k_line("%s %-14s %-11s %s%-6s" K_RESET " %12s %s " K_DIM K_SEP K_RESET " %s", SEL(i, l.sel), k_get(&invoices, i, I_NUMBER),
                   k_fmt_day(d, sizeof d, k_geti(&invoices, i, I_DAY)), st == 2 ? K_GRN : st == 1 && due < k_today() ? K_RED : st == 1 ? K_YEL : K_DIM,
                   invoice_status[st % 3], k_money(m, sizeof m, k_geti(&invoices, i, I_AMOUNT)), cur(), nm);
        }
        if (!l.n) k_line("   " K_DIM "no invoices yet: n makes one from a client's billable time" K_RESET);
        k_footer("n new from billable time   Enter show   Space draft/sent/paid   e export to USB   d delete   Esc back");
        k_show();
        int k = key();
        if (k == K_NONE) continue;
        if (k_list_key(&l, k, rows)) continue;
        if (k == K_ESC || k == 'q') return;
        if (k == 'n') new_invoice();
        else if (!l.n) continue;
        else if (k == K_ENTER) { char *t = invoice_text(l.sel); k_message("Work mode - invoice", t); free(t); }
        else if (k == ' ') { k_seti(&invoices, l.sel, I_STATUS, (k_geti(&invoices, l.sel, I_STATUS) + 1) % 3); data_save(); }
        else if (k == 'e') {
            char *t = invoice_text(l.sel), name[96];
            snprintf(name, sizeof name, "invoices/%s.txt", k_get(&invoices, l.sel, I_NUMBER));
            export_to_stick("the invoice", name, t, strlen(t));
            free(t);
        } else if ((k == 'd' || k == KEY_DELETE) && k_confirm("Delete this invoice (its time becomes billable again)?")) {
            long id = k_geti(&invoices, l.sel, I_ID);
            for (int i = 0; i < times.n; i++) if (k_geti(&times, i, M_INVOICE) == id) k_seti(&times, i, M_INVOICE, 0);
            k_table_del(&invoices, l.sel);
            data_save();
        }
    }
}

/* ----------------------------------------------------------- expenses */

static int cmp_exp(char *const *a, char *const *b) {
    long x = atol(a[X_DAY]), y = atol(b[X_DAY]);
    return x > y ? -1 : x < y;
}

void expenses_menu(void) {
    struct k_list l = { 0, 0, 0 };
    struct k_field f[] = {
        { "Amount", X_AMOUNT, KF_MONEY, true, NULL, 0, NULL, 0, 0, 0 },
        { "Day", X_DAY, KF_DATE, true, NULL, 0, NULL, 0, 0, 0 },
        { "Category", X_CAT, KF_CHOICE, false, expense_cat, 7, NULL, 0, 0, 0 },
        { "Project", X_PROJ, KF_REF, false, NULL, 0, &projects, P_NAME, 0, 0 },
        { "Note", X_NOTE, KF_TEXT, false, NULL, 0, NULL, 0, 0, 0 },
    };
    for (;;) {
        k_table_sort(&expenses, cmp_exp);
        l.n = expenses.n;
        k_frame();
        header(-1);
        int y, m, d;
        k_civil(k_today(), &y, &m, &d);
        long first = k_days(y, m, 1), month = 0, bycat[7] = { 0 };
        for (int i = 0; i < expenses.n; i++) {
            if (k_geti(&expenses, i, X_DAY) < first) continue;
            month += k_geti(&expenses, i, X_AMOUNT);
            bycat[k_geti(&expenses, i, X_CAT) % 7] += k_geti(&expenses, i, X_AMOUNT);
        }
        char a[32], line[600] = "";
        for (int c = 0; c < 7; c++) {
            if (!bycat[c]) continue;
            char t[64];
            snprintf(t, sizeof t, "%s %s   ", expense_cat[c], k_money(a, sizeof a, bycat[c]));
            strlcat(line, t, sizeof line);
        }
        k_line(" " K_CYAN "Expenses" K_RESET "   %s: %s %s   %s", k_month_name(m), k_money(a, sizeof a, month), cur(), line);
        k_rule();
        int rows = k_rows - k_used() - 1;
        k_list_fit(&l, rows);
        for (int i = l.top; i < l.n && i < l.top + rows; i++) {
            char dd[24], mm[32], pn[64], t[300];
            k_fit(pn, sizeof pn, proj_name(k_geti(&expenses, i, X_PROJ)), 16);
            k_fit(t, sizeof t, k_get(&expenses, i, X_NOTE), k_cols - 64);
            k_line("%s %-11s %12s %s  %-10s %s " K_DIM K_SEP K_RESET " %s", SEL(i, l.sel), k_fmt_day(dd, sizeof dd, k_geti(&expenses, i, X_DAY)),
                   k_money(mm, sizeof mm, k_geti(&expenses, i, X_AMOUNT)), cur(), expense_cat[k_geti(&expenses, i, X_CAT) % 7], pn, t);
        }
        if (!l.n) k_line("   " K_DIM "no expenses yet: press n" K_RESET);
        k_footer("n new   Enter change   d delete   Esc back");
        k_show();
        int k = key();
        if (k == K_NONE) continue;
        if (k_list_key(&l, k, rows)) continue;
        if (k == K_ESC || k == 'q') return;
        if (k == 'n') {
            int r = k_table_add(&expenses);
            k_seti(&expenses, r, X_DAY, k_today());
            k_seti(&expenses, r, X_CAT, 6);
            if (!k_form("Work mode - new expense", &expenses, r, f, 5, true)) k_table_del(&expenses, r);
            data_save();
        } else if (k == K_ENTER && l.n) { k_form("Work mode - expense", &expenses, l.sel, f, 5, false); data_save(); }
        else if ((k == 'd' || k == KEY_DELETE) && l.n && k_confirm("Delete this expense?")) { k_table_del(&expenses, l.sel); data_save(); }
    }
}
