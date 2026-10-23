/* Work mode: tasks (a list or a board) and projects. */
#include "work.h"

#define SEL(i, s) ((i) == (s) ? K_YEL "\xe2\x96\xb6" : " ")

/* "[ ]" and "[x]" lines of a task's notes */
static void checklist(const char *notes, int *done, int *all) {
    *done = *all = 0;
    for (const char *p = notes; *p;) {
        while (*p == ' ' || *p == '-' || *p == '*') p++;
        if (p[0] == '[' && p[2] == ']') { (*all)++; if (p[1] == 'x' || p[1] == 'X') (*done)++; }
        p = strchr(p, '\n');
        if (!p) break;
        p++;
    }
}

static bool blocked(int row) {
    long b = k_geti(&tasks, row, T_BLOCKED);
    int r = b ? k_table_row(&tasks, b) : -1;
    return r >= 0 && k_geti(&tasks, r, T_STATUS) != 4;
}

static void set_status(int row, long st) {
    if (st < 0) st = 0;
    if (st > 4) st = 4;
    k_seti(&tasks, row, T_STATUS, st);
    k_seti(&tasks, row, T_DONE, st == 4 ? k_today() : 0);
    data_save();
    if (st == 4) {
        if (timer.on && timer.task == k_geti(&tasks, row, T_ID)) timer_stop();
        k_flash("done: %s", k_get(&tasks, row, T_TITLE));
    }
}

static const char *due_text(long due, bool done, char *out, size_t len) {
    if (!due) { snprintf(out, len, K_DIM "no due day" K_RESET); return out; }
    char d[24];
    long left = due - k_today();
    snprintf(out, len, "%s%-11s" K_RESET, done ? K_DIM : left < 0 ? K_RED : left == 0 ? K_YEL : left <= 2 ? K_CYAN : "",
             k_fmt_day(d, sizeof d, due));
    return out;
}

static int cmp_task(char *const *a, char *const *b) {
    long sa = atol(a[T_STATUS]), sb = atol(b[T_STATUS]);
    bool da = sa == 4, db = sb == 4;
    if (da != db) return da ? 1 : -1;
    long x = atol(a[T_DUE]), y = atol(b[T_DUE]);
    if (!x) x = 1L << 40;
    if (!y) y = 1L << 40;
    if (x != y) return x < y ? -1 : 1;
    long pa = atol(a[T_PRIO]), pb = atol(b[T_PRIO]);
    if (pa != pb) return pa > pb ? -1 : 1;
    return 0;
}

/* -------------------------------------------------------------- tasks */

static long f_proj;
static bool f_done, f_urgent;
static char f_text[64];

static bool task_shown(int i, bool board) {
    if (f_proj && k_geti(&tasks, i, T_PROJ) != f_proj) return false;
    long st = k_geti(&tasks, i, T_STATUS);
    if (!board && st == 4 && !f_done) return false;
    if (board && st == 4 && !f_done && k_geti(&tasks, i, T_DONE) < k_today() - 14) return false;   /* recent ones only */
    if (f_urgent) {
        long due = k_geti(&tasks, i, T_DUE);
        if (st == 4 || !due || due > k_today()) return false;
    }
    if (f_text[0] && !k_strcasestr(k_get(&tasks, i, T_TITLE), f_text) && !k_strcasestr(k_get(&tasks, i, T_TAGS), f_text) &&
        !k_strcasestr(k_get(&tasks, i, T_NOTES), f_text))
        return false;
    return true;
}

static void filter_line(void) {
    char f[300] = "";
    if (f_proj) { strlcat(f, "project ", sizeof f); strlcat(f, proj_name(f_proj), sizeof f); strlcat(f, "   ", sizeof f); }
    if (f_urgent) strlcat(f, "overdue and today   ", sizeof f);
    if (f_done) strlcat(f, "with the done ones   ", sizeof f);
    if (f_text[0]) { strlcat(f, "search \"", sizeof f); strlcat(f, f_text, sizeof f); strlcat(f, "\"   ", sizeof f); }
    if (f[0]) k_line(" " K_YEL "only: %s" K_RESET K_DIM "(x shows everything)" K_RESET, f);
}

static bool common_keys(int k, int row) {
    if (k == 'n') { new_task(f_proj); return true; }
    if (k == 'p') { long id = f_proj; if (f_proj) f_proj = 0; else if (pick_project("Work mode - only one project", &id, false) == 0) f_proj = id; return true; }
    if (k == 'f') { f_done = !f_done; return true; }
    if (k == 'o') { f_urgent = !f_urgent; return true; }
    if (k == 'x') { f_proj = 0; f_done = f_urgent = false; f_text[0] = 0; return true; }
    if (k == '/') { if (!k_ask("Search the tasks for: ", f_text, sizeof f_text)) f_text[0] = 0; return true; }
    if (row < 0) return false;
    if (k == K_ENTER) { edit_task(row, false); data_save(); return true; }
    if (k == 's') { timer_start(k_geti(&tasks, row, T_PROJ), k_geti(&tasks, row, T_ID), NULL); return true; }
    if ((k == 'd' || k == KEY_DELETE) && k_confirm("Delete this task?")) { k_table_del(&tasks, row); data_save(); return true; }
    return false;
}

static int board(void);

int view_tasks(void) {
    static bool as_board;
    if (as_board) {
        int v = board();
        if (v != -100) return v;
        as_board = false;
    }
    struct k_list l = { 0, 0, 0 };
    for (;;) {
        k_table_sort(&tasks, cmp_task);
        int idx[2048], n = 0, open = 0, late = 0;
        for (int i = 0; i < tasks.n && n < 2048; i++) {
            long st = k_geti(&tasks, i, T_STATUS), due = k_geti(&tasks, i, T_DUE);
            if (st != 4) { open++; if (due && due < k_today()) late++; }
            if (task_shown(i, false)) idx[n++] = i;
        }
        l.n = n;
        k_frame();
        header(1);
        k_line(" %d open   %s%d overdue" K_RESET "   list (b: board)", open, late ? K_RED : K_DIM, late);
        filter_line();
        k_rule();
        int rows = k_rows - k_used() - 1;
        k_list_fit(&l, rows);
        for (int j = l.top; j < n && j < l.top + rows; j++) {
            int i = idx[j];
            long st = k_geti(&tasks, i, T_STATUS), p = k_geti(&tasks, i, T_PRIO);
            char due[64], est[16], pn[64], t[400], extra[96] = "", ck[32];
            int cd, ca;
            checklist(k_get(&tasks, i, T_NOTES), &cd, &ca);
            if (ca) { snprintf(ck, sizeof ck, " %d/%d", cd, ca); strlcat(extra, ck, sizeof extra); }
            if (blocked(i)) strlcat(extra, " waits", sizeof extra);
            if (k_get(&tasks, i, T_TAGS)[0]) { strlcat(extra, " ", sizeof extra); strlcat(extra, k_get(&tasks, i, T_TAGS), sizeof extra); }
            k_fit(pn, sizeof pn, proj_name(k_geti(&tasks, i, T_PROJ)), 14);
            k_fit(t, sizeof t, k_get(&tasks, i, T_TITLE), k_cols - 66 - k_width(extra));
            if (k_geti(&tasks, i, T_EST)) k_fmt_dur(est, sizeof est, k_geti(&tasks, i, T_EST) * 60);
            else est[0] = 0;
            k_line("%s %s %s%-7s" K_RESET " %s%-6s" K_RESET " %7s  %s " K_DIM K_SEP K_RESET " %s%s" K_RESET " " K_DIM K_SEP "%s" K_RESET, SEL(j, l.sel),
                   due_text(k_geti(&tasks, i, T_DUE), st == 4, due, sizeof due), st == 4 ? K_GRN : st == 2 ? K_CYAN : "",
                   task_status[st % 5], prio_colour(p), prio_name[p % 4], est, pn, st == 4 ? K_DIM : "", t, extra);
        }
        if (!n) k_line("   " K_DIM "%s" K_RESET, tasks.n ? "nothing here (x shows everything)" : "no tasks yet: press n");
        k_footer("n new  Enter change  Space next status  s timer  d delete  b board  p project  / search  o urgent  f done  Esc menu");
        k_show();
        int k = key();
        if (k == K_NONE) continue;
        if (k_list_key(&l, k, rows)) continue;
        int v = tab_key(k, 1);
        if (v >= 0 && v != 1) return v;
        int row = n ? idx[l.sel] : -1;
        if (k == 'b') {
            as_board = true;
            v = board();
            if (v != -100) return v;
            as_board = false;
            continue;
        }
        if (k == ' ' && row >= 0) { long st = k_geti(&tasks, row, T_STATUS); set_status(row, st == 4 ? 1 : st + 1); continue; }
        common_keys(k, row);
    }
}

/* the board: a column per status; -100 back to the list */
static int board(void) {
    static int col = 1, pos;
    for (;;) {
        k_table_sort(&tasks, cmp_task);
        int cols[5][512], cnt[5] = { 0 };
        for (int i = 0; i < tasks.n; i++) {
            if (!task_shown(i, true)) continue;
            int s = (int)k_geti(&tasks, i, T_STATUS) % 5;
            if (cnt[s] < 512) cols[s][cnt[s]++] = i;
        }
        if (pos >= cnt[col]) pos = cnt[col] ? cnt[col] - 1 : 0;
        k_frame();
        header(1);
        k_line(" board (b: list)   < > move the task   Space next status");
        filter_line();
        k_rule();
        int cw = (k_cols - 2) / 5;
        char line[4096];
        int o = 0;
        for (int c = 0; c < 5; c++) {
            char h[64], f[64];
            snprintf(h, sizeof h, "%s (%d)", task_status[c], cnt[c]);
            k_fit(f, sizeof f, h, cw - 1);
            o += snprintf(line + o, sizeof line - o, "%s%s" K_RESET K_DIM K_SEP K_RESET, c == col ? K_YEL : K_CYAN, f);
        }
        k_line(" %s", line);
        int room = (k_rows - k_used() - 2) / 3, most = 0;
        for (int c = 0; c < 5; c++) if (cnt[c] > most) most = cnt[c];
        int first = pos >= room ? pos - room + 1 : 0;
        for (int r = first; r < most && r < first + room; r++) {
            for (int sub = 0; sub < 3; sub++) {
                o = 0;
                for (int c = 0; c < 5; c++) {
                    char cell[400] = "", f[400];
                    const char *colr = "";
                    if (r < cnt[c]) {
                        int i = cols[c][r];
                        bool me = c == col && r == pos;
                        if (sub == 0) { snprintf(cell, sizeof cell, "%s", k_get(&tasks, i, T_TITLE)); colr = me ? K_YEL : ""; }
                        else if (sub == 1) {
                            char d[24];
                            long due = k_geti(&tasks, i, T_DUE), p = k_geti(&tasks, i, T_PRIO);
                            snprintf(cell, sizeof cell, "%s %s%s", prio_name[p % 4], due ? k_fmt_day(d, sizeof d, due) : "", blocked(i) ? " waits" : "");
                            colr = me ? K_YEL : due && due < k_today() && c != 4 ? K_RED : prio_colour(p)[0] ? prio_colour(p) : K_DIM;
                        } else {
                            snprintf(cell, sizeof cell, "%s", proj_name(k_geti(&tasks, i, T_PROJ)));
                            colr = K_DIM;
                        }
                    }
                    k_fit(f, sizeof f, cell, cw - 1);
                    o += snprintf(line + o, sizeof line - o, "%s%s" K_RESET K_DIM K_SEP K_RESET, colr, f);
                }
                k_line(" %s", line);
            }
        }
        if (!most) k_line("   " K_DIM "no tasks yet: press n" K_RESET);
        k_footer("arrows choose  < > move  Space next  n new  Enter change  s timer  d delete  b list  p project  Esc menu");
        k_show();
        int k = key();
        if (k == K_NONE) continue;
        int v = tab_key(k, 1);
        if (v >= 0 && v != 1) return v;
        int row = cnt[col] ? cols[col][pos] : -1;
        if (k == 'b') return -100;
        if (k == KEY_LEFT && col > 0) { col--; pos = 0; }
        else if (k == KEY_RIGHT && col < 4) { col++; pos = 0; }
        else if (k == KEY_UP && pos > 0) pos--;
        else if (k == KEY_DOWN && pos < cnt[col] - 1) pos++;
        else if ((k == '<' || k == ',') && row >= 0 && col > 0) { set_status(row, col - 1); col--; pos = 0; }
        else if ((k == '>' || k == '.' || k == ' ') && row >= 0 && col < 4) { set_status(row, col + 1); col++; pos = 0; }
        else common_keys(k, row);
    }
}

/* ----------------------------------------------------------- projects */

static void project_numbers(long id, int *open, int *all, long *secs, long *bill_secs, long *spent) {
    *open = *all = 0;
    *secs = *bill_secs = *spent = 0;
    for (int i = 0; i < tasks.n; i++) {
        if (k_geti(&tasks, i, T_PROJ) != id) continue;
        (*all)++;
        if (k_geti(&tasks, i, T_STATUS) != 4) (*open)++;
    }
    for (int i = 0; i < times.n; i++) {
        if (k_geti(&times, i, M_PROJ) != id) continue;
        *secs += k_geti(&times, i, M_SECS);
        if (k_geti(&times, i, M_BILL)) *bill_secs += k_geti(&times, i, M_SECS);
    }
    for (int i = 0; i < expenses.n; i++) if (k_geti(&expenses, i, X_PROJ) == id) *spent += k_geti(&expenses, i, X_AMOUNT);
}

void project_detail(long id) {
    struct k_list l = { 0, 0, 0 };
    for (;;) {
        int pr = k_table_row(&projects, id);
        if (pr < 0) return;
        k_table_sort(&tasks, cmp_task);
        int idx[1024], n = 0;
        for (int i = 0; i < tasks.n && n < 1024; i++) if (k_geti(&tasks, i, T_PROJ) == id) idx[n++] = i;
        l.n = n;
        int open, all;
        long secs, bsecs, spent, budget = k_geti(&projects, pr, P_BUDGET) * 60, rate = proj_rate(id), due = k_geti(&projects, pr, P_DUE);
        project_numbers(id, &open, &all, &secs, &bsecs, &spent);
        k_frame();
        header(2);
        char a[32], b[32], c[32], d[32], e[32], bar[512];
        k_line(" " K_CYAN "%s" K_RESET "   %s   client %s", k_get(&projects, pr, P_NAME), project_status[k_geti(&projects, pr, P_STATUS) % 5],
               person_name(k_geti(&projects, pr, P_CLIENT)));
        k_line(" tasks %d done of %d  %s   deadline %s %s", all - open, all, k_bar(bar, sizeof bar, all - open, all ? all : 1, 20),
               due ? k_fmt_day(a, sizeof a, due) : "none", due ? k_fmt_rel(b, sizeof b, due) : "");
        k_line(" time %s%s%s   billable %s   worth %s %s   expenses %s %s", k_fmt_dur(a, sizeof a, secs), budget ? " of " : "",
               budget ? k_fmt_dur(b, sizeof b, budget) : "", k_fmt_dur(c, sizeof c, bsecs), k_money(d, sizeof d, bsecs * rate / 3600), cur(),
               k_money(e, sizeof e, spent), cur());
        if (budget && secs > budget) k_line(" " K_RED "over budget by %s" K_RESET, k_fmt_dur(a, sizeof a, secs - budget));
        k_rule();
        int rows = k_rows - k_used() - 1;
        k_list_fit(&l, rows);
        for (int j = l.top; j < n && j < l.top + rows; j++) {
            int i = idx[j];
            long st = k_geti(&tasks, i, T_STATUS), p = k_geti(&tasks, i, T_PRIO);
            char due_s[64], t[400];
            k_fit(t, sizeof t, k_get(&tasks, i, T_TITLE), k_cols - 40);
            k_line("%s %s %s%-7s" K_RESET " %s%-6s" K_RESET " " K_DIM K_SEP K_RESET " %s%s", SEL(j, l.sel), due_text(k_geti(&tasks, i, T_DUE), st == 4, due_s, sizeof due_s),
                   st == 4 ? K_GRN : st == 2 ? K_CYAN : "", task_status[st % 5], prio_colour(p), prio_name[p % 4], st == 4 ? K_DIM : "", t);
        }
        if (!n) k_line("   " K_DIM "no tasks in this project: press n" K_RESET);
        k_footer("n new task  Enter change  Space next status  s timer on the project  e change the project  Esc back");
        k_show();
        int k = key();
        if (k == K_NONE) continue;
        if (k_list_key(&l, k, rows)) continue;
        int row = n ? idx[l.sel] : -1;
        if (k == K_ESC || k == 'q') return;
        if (k == 'n') new_task(id);
        else if (k == K_ENTER && row >= 0) { edit_task(row, false); data_save(); }
        else if (k == ' ' && row >= 0) { long st = k_geti(&tasks, row, T_STATUS); set_status(row, st == 4 ? 1 : st + 1); }
        else if (k == 's') timer_start(id, row >= 0 ? k_geti(&tasks, row, T_ID) : 0, NULL);
        else if (k == 'e') { edit_project(pr, false); data_save(); }
    }
}

static int cmp_project(char *const *a, char *const *b) {
    long sa = atol(a[P_STATUS]), sb = atol(b[P_STATUS]);
    bool ca = sa >= 3, cb = sb >= 3;
    if (ca != cb) return ca ? 1 : -1;
    long x = atol(a[P_DUE]), y = atol(b[P_DUE]);
    if (!x) x = 1L << 40;
    if (!y) y = 1L << 40;
    return x < y ? -1 : x > y;
}

int view_projects(void) {
    struct k_list l = { 0, 0, 0 };
    for (;;) {
        k_table_sort(&projects, cmp_project);
        l.n = projects.n;
        k_frame();
        header(2);
        int active = 0;
        for (int i = 0; i < projects.n; i++) if (k_geti(&projects, i, P_STATUS) == 1) active++;
        k_line(" %d projects, %d active", projects.n, active);
        k_rule();
        int rows = k_rows - k_used() - 1;
        k_list_fit(&l, rows);
        for (int i = l.top; i < l.n && i < l.top + rows; i++) {
            long id = k_geti(&projects, i, P_ID), due = k_geti(&projects, i, P_DUE), budget = k_geti(&projects, i, P_BUDGET) * 60;
            int open, all;
            long secs, bsecs, spent;
            project_numbers(id, &open, &all, &secs, &bsecs, &spent);
            char d[24], a[16], b[16], bar[512], nm[300], cl[64];
            long st = k_geti(&projects, i, P_STATUS);
            const char *dc = st >= 3 ? K_DIM : due && due < k_today() ? K_RED : due && due - k_today() <= 7 ? K_YEL : "";
            k_fit(cl, sizeof cl, person_name(k_geti(&projects, i, P_CLIENT)), 14);
            k_fit(nm, sizeof nm, k_get(&projects, i, P_NAME), k_cols - 92);
            k_line("%s %s%-9s" K_RESET " %s%-11s" K_RESET " %3d/%-3d %s %7s/%-7s%s %s " K_DIM K_SEP K_RESET " %s", SEL(i, l.sel), st == 1 ? K_GRN : K_DIM,
                   project_status[st % 5], dc, due ? k_fmt_day(d, sizeof d, due) : "-", all - open, all,
                   k_bar(bar, sizeof bar, all - open, all ? all : 1, 10), k_fmt_dur(a, sizeof a, secs), budget ? k_fmt_dur(b, sizeof b, budget) : "-",
                   budget && secs > budget ? K_RED "!" K_RESET : " ", cl, nm);
        }
        if (!l.n) k_line("   " K_CYAN "no projects yet: press n" K_RESET);
        k_footer("n new   Enter its page   e change   s timer   d delete   1-8 views   Esc menu");
        k_show();
        int k = key();
        if (k == K_NONE) continue;
        if (k_list_key(&l, k, rows)) continue;
        int v = tab_key(k, 2);
        if (v >= 0 && v != 2) return v;
        if (k == 'n') { int r = k_table_add(&projects); if (!edit_project(r, true)) k_table_del(&projects, r); data_save(); }
        else if (!l.n) continue;
        else if (k == K_ENTER) project_detail(k_geti(&projects, l.sel, P_ID));
        else if (k == 'e') { edit_project(l.sel, false); data_save(); }
        else if (k == 's') timer_start(k_geti(&projects, l.sel, P_ID), 0, NULL);
        else if (k == 'd' || k == KEY_DELETE) {
            long id = k_geti(&projects, l.sel, P_ID);
            int n = 0;
            for (int i = 0; i < tasks.n; i++) if (k_geti(&tasks, i, T_PROJ) == id) n++;
            char q[160];
            snprintf(q, sizeof q, "Delete the project%s?", n ? " (its tasks and time stay, without a project)" : "");
            if (k_confirm(q)) { k_table_del(&projects, l.sel); data_save(); }
        }
    }
}
