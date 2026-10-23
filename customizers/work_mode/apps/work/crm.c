/* Work mode: the calendar (meetings), clients and deals, notes. */
#include "work.h"

#define SEL(i, s) ((i) == (s) ? K_YEL "\xe2\x96\xb6" : " ")

/* ----------------------------------------------------------- calendar */

/* "- [ ] call the bank" lines of a meeting's notes become tasks */
static void action_items(int mr) {
    const char *t = k_get(&meetings, mr, G_NOTES);
    int made = 0;
    for (const char *p = t; *p;) {
        const char *e = strchr(p, '\n');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        const char *s = p;
        while (s < p + n && (*s == ' ' || *s == '-' || *s == '*')) s++;
        if (s + 3 < p + n && s[0] == '[' && s[1] == ' ' && s[2] == ']') {
            s += 3;
            while (*s == ' ') s++;
            char title[256];
            size_t l = (size_t)(p + n - s) < sizeof title - 1 ? (size_t)(p + n - s) : sizeof title - 1;
            memcpy(title, s, l);
            title[l] = 0;
            bool have = false;
            for (int i = 0; i < tasks.n; i++) if (!strcmp(k_get(&tasks, i, T_TITLE), title)) have = true;
            if (l && !have) {
                int r = k_table_add(&tasks);
                k_set(&tasks, r, T_TITLE, title);
                k_seti(&tasks, r, T_PROJ, k_geti(&meetings, mr, G_PROJ));
                k_seti(&tasks, r, T_STATUS, 1);
                k_seti(&tasks, r, T_PRIO, 1);
                k_seti(&tasks, r, T_CREATED, k_today());
                char note[200];
                snprintf(note, sizeof note, "from the meeting \"%s\"", k_get(&meetings, mr, G_TITLE));
                k_set(&tasks, r, T_NOTES, note);
                made++;
            }
        }
        if (!e) break;
        p = e + 1;
    }
    data_save();
    if (made) k_flash("%d new task(s) from the action items", made);
    else k_flash("no new action items (lines \"- [ ] something\" in the notes)");
}

static int cmp_meet(char *const *a, char *const *b) {
    long x = atol(a[G_DAY]) * 1440 + atol(a[G_START]), y = atol(b[G_DAY]) * 1440 + atol(b[G_START]);
    return x < y ? -1 : x > y;
}

int view_calendar(void) {
    static long week;
    struct k_list l = { 0, 0, 0 };
    if (!week) week = week_first(k_today());
    for (;;) {
        k_table_sort(&meetings, cmp_meet);
        k_frame();
        header(4);
        char a[32], b[32];
        k_line(" week of " K_CYAN "%s" K_RESET "   [ ] other weeks", k_fmt_long(a, sizeof a, week));
        k_rule();
        /* rows: a day heading, its meetings, its tasks that are due */
        int item[512], kind[512], n = 0, sel_items[512], ns = 0;
        for (int d = 0; d < 7 && n < 500; d++) {
            long day = week + d;
            item[n] = (int)day;
            kind[n++] = 0;
            for (int i = 0; i < meetings.n && n < 500; i++)
                if (k_geti(&meetings, i, G_DAY) == day) { item[n] = i; kind[n++] = 1; sel_items[ns++] = n - 1; }
            for (int i = 0; i < tasks.n && n < 500; i++)
                if (k_geti(&tasks, i, T_DUE) == day && k_geti(&tasks, i, T_STATUS) != 4) { item[n] = i; kind[n++] = 2; }
        }
        l.n = ns;
        if (l.sel >= ns) l.sel = ns ? ns - 1 : 0;
        int selrow = ns ? sel_items[l.sel] : -1;
        int rows = k_rows - k_used() - 1, top = 0;
        if (selrow >= rows) top = selrow - rows + 2;
        for (int r = top; r < n && r < top + rows; r++) {
            if (kind[r] == 0) {
                long day = item[r];
                k_line(" %s%s" K_RESET "%s", day == k_today() ? K_YEL : K_CYAN, k_fmt_long(a, sizeof a, day), day == k_today() ? K_YEL "  today" K_RESET : "");
            } else if (kind[r] == 1) {
                int i = item[r];
                long s = k_geti(&meetings, i, G_START), len = k_geti(&meetings, i, G_LEN);
                char t[300], where[64];
                k_fit(where, sizeof where, k_get(&meetings, i, G_WHERE), 16);
                k_fit(t, sizeof t, k_get(&meetings, i, G_TITLE), k_cols - 50);
                k_line("%s   %s-%s  meeting  %s " K_DIM K_SEP K_RESET " %s", r == selrow ? K_YEL "\xe2\x96\xb6" : " ", k_fmt_time(a, sizeof a, (int)s),
                       k_fmt_time(b, sizeof b, (int)(s + len)), where, t);
            } else {
                int i = item[r];
                char t[300];
                k_fit(t, sizeof t, k_get(&tasks, i, T_TITLE), k_cols - 30);
                k_line("     " K_DIM "due      task " K_SEP K_RESET " %s%s" K_RESET, prio_colour(k_geti(&tasks, i, T_PRIO)), t);
            }
        }
        k_footer("arrows meetings  n new  Enter open  t action items to tasks  d delete  [ ] weeks  1-8 views  Esc menu");
        k_show();
        int k = key();
        if (k == K_NONE) continue;
        if (k_list_key(&l, k, rows)) continue;
        int v = tab_key(k, 4);
        if (v >= 0 && v != 4) return v;
        int mr = ns ? item[selrow] : -1;
        if (k == '[') { week -= 7; l.sel = 0; }
        else if (k == ']') { week += 7; l.sel = 0; }
        else if (k == 'n') {
            int r = k_table_add(&meetings);
            k_seti(&meetings, r, G_DAY, week <= k_today() && k_today() <= week + 6 ? k_today() : week);
            if (!edit_meeting(r, true)) k_table_del(&meetings, r);
            data_save();
        } else if (k == K_ENTER && mr >= 0) { edit_meeting(mr, false); data_save(); }
        else if (k == 't' && mr >= 0) action_items(mr);
        else if ((k == 'd' || k == KEY_DELETE) && mr >= 0 && k_confirm("Delete this meeting?")) { k_table_del(&meetings, mr); data_save(); }
    }
}

/* ------------------------------------------------------------ clients */

static int pipeline(void) {
    static int col, pos;
    for (;;) {
        int cols[6][256], cnt[6] = { 0 };
        long sum[6] = { 0 };
        for (int i = 0; i < deals.n; i++) {
            int s = (int)k_geti(&deals, i, D_STAGE) % 6;
            if (cnt[s] < 256) cols[s][cnt[s]++] = i;
            sum[s] += k_geti(&deals, i, D_VALUE);
        }
        if (pos >= cnt[col]) pos = cnt[col] ? cnt[col] - 1 : 0;
        k_frame();
        header(5);
        long open = sum[0] + sum[1] + sum[2] + sum[3];
        char a[32], b[32];
        k_line(" deals pipeline (p: people)   open %s %s   won %s %s", k_money(a, sizeof a, open), cur(), k_money(b, sizeof b, sum[4]), cur());
        k_rule();
        int cw = (k_cols - 2) / 6;
        char line[4096];
        int o = 0;
        for (int c = 0; c < 6; c++) {
            char h[64], f[64], m[32];
            snprintf(h, sizeof h, "%s %s", deal_stage[c], k_money(m, sizeof m, sum[c]));
            k_fit(f, sizeof f, h, cw - 1);
            o += snprintf(line + o, sizeof line - o, "%s%s" K_RESET K_DIM K_SEP K_RESET, c == col ? K_YEL : c == 4 ? K_GRN : c == 5 ? K_DIM : K_CYAN, f);
        }
        k_line(" %s", line);
        int most = 0;
        for (int c = 0; c < 6; c++) if (cnt[c] > most) most = cnt[c];
        for (int r = 0; r < most && k_used() < k_rows - 3; r++) {
            for (int sub = 0; sub < 2; sub++) {
                o = 0;
                for (int c = 0; c < 6; c++) {
                    char cell[300] = "", f[300], m[32];
                    const char *colr = "";
                    if (r < cnt[c]) {
                        int i = cols[c][r];
                        if (sub == 0) snprintf(cell, sizeof cell, "%s", k_get(&deals, i, D_TITLE));
                        else snprintf(cell, sizeof cell, "%s %s", k_money(m, sizeof m, k_geti(&deals, i, D_VALUE)), person_name(k_geti(&deals, i, D_PERSON)));
                        colr = c == col && r == pos ? K_YEL : sub ? K_DIM : "";
                    }
                    k_fit(f, sizeof f, cell, cw - 1);
                    o += snprintf(line + o, sizeof line - o, "%s%s" K_RESET K_DIM K_SEP K_RESET, colr, f);
                }
                k_line(" %s", line);
            }
        }
        if (!deals.n) k_line("   " K_DIM "no deals yet: press n" K_RESET);
        k_footer("arrows choose  < > stage  n new deal  Enter change  d delete  p people  1-8 views  Esc menu");
        k_show();
        int k = key();
        if (k == K_NONE) continue;
        int v = tab_key(k, 5);
        if (v >= 0 && v != 5) return v;
        int row = cnt[col] ? cols[col][pos] : -1;
        if (k == 'p') return -100;
        if (k == KEY_LEFT && col > 0) { col--; pos = 0; }
        else if (k == KEY_RIGHT && col < 5) { col++; pos = 0; }
        else if (k == KEY_UP && pos > 0) pos--;
        else if (k == KEY_DOWN && pos < cnt[col] - 1) pos++;
        else if ((k == '<' || k == ',') && row >= 0 && col > 0) { k_seti(&deals, row, D_STAGE, col - 1); col--; pos = 0; data_save(); }
        else if ((k == '>' || k == '.') && row >= 0 && col < 5) { k_seti(&deals, row, D_STAGE, col + 1); col++; pos = 0; data_save(); }
        else if (k == 'n') { int r = k_table_add(&deals); k_seti(&deals, r, D_STAGE, col); if (!edit_deal(r, true)) k_table_del(&deals, r); data_save(); }
        else if (k == K_ENTER && row >= 0) { edit_deal(row, false); data_save(); }
        else if ((k == 'd' || k == KEY_DELETE) && row >= 0 && k_confirm("Delete this deal?")) { k_table_del(&deals, row); data_save(); }
    }
}

static int cmp_person(char *const *a, char *const *b) { return strcasecmp(a[C_NAME], b[C_NAME]); }

int view_clients(void) {
    static bool deals_view;
    static int only_kind = -1;
    struct k_list l = { 0, 0, 0 };
    for (;;) {
        if (deals_view) {
            int v = pipeline();
            if (v != -100) return v;
            deals_view = false;
        }
        k_table_sort(&people, cmp_person);
        int idx[2048], n = 0;
        for (int i = 0; i < people.n && n < 2048; i++)
            if (only_kind < 0 || k_geti(&people, i, C_KIND) == only_kind) idx[n++] = i;
        l.n = n;
        k_frame();
        header(5);
        k_line(" people (p: deals pipeline)   %d %s", n, only_kind < 0 ? "people" : person_kind[only_kind]);
        k_rule();
        int rows = k_rows - k_used() - 6;
        k_list_fit(&l, rows);
        for (int j = l.top; j < n && j < l.top + rows; j++) {
            int i = idx[j];
            char last[32] = "", co[64], nm[64], ph[32];
            long lc = k_geti(&people, i, C_LAST);
            if (lc) k_fmt_rel(last, sizeof last, lc);
            k_fit(co, sizeof co, k_get(&people, i, C_COMPANY), 18);
            k_fit(ph, sizeof ph, k_get(&people, i, C_PHONE), 16);
            k_fit(nm, sizeof nm, k_get(&people, i, C_NAME), k_cols - 74);
            k_line("%s %-9s %-16s %-14s %s " K_DIM K_SEP K_RESET " %s", SEL(j, l.sel), person_kind[k_geti(&people, i, C_KIND) % 4], ph,
                   last[0] ? last : "-", co, nm);
        }
        if (!n) k_line("   " K_DIM "no people yet: press n" K_RESET);
        k_rule();
        if (n) {
            int i = idx[l.sel];
            k_line(" " K_CYAN "%s" K_RESET "  %s %s", k_get(&people, i, C_NAME), k_get(&people, i, C_ROLE), k_get(&people, i, C_COMPANY));
            k_line(" phone %s   email %s", k_get(&people, i, C_PHONE)[0] ? k_get(&people, i, C_PHONE) : "-", k_get(&people, i, C_EMAIL)[0] ? k_get(&people, i, C_EMAIL) : "-");
            char nt[400];
            k_fit(nt, sizeof nt, k_get(&people, i, C_NOTES), k_cols - 8);
            k_line(" notes %s", nt);
        }
        k_footer("n new  Enter change  c contacted today  k kind  d delete  p deals  1-8 views  Esc menu");
        k_show();
        int k = key();
        if (k == K_NONE) continue;
        if (k_list_key(&l, k, rows)) continue;
        int v = tab_key(k, 5);
        if (v >= 0 && v != 5) return v;
        int row = n ? idx[l.sel] : -1;
        if (k == 'p') deals_view = true;
        else if (k == 'n') { int r = k_table_add(&people); if (only_kind >= 0) k_seti(&people, r, C_KIND, only_kind); if (!edit_person(r, true)) k_table_del(&people, r); data_save(); }
        else if (k == 'k') {
            const char *items[] = { "everyone", "clients", "colleagues", "suppliers", "others" };
            int c = k_menu("Work mode - show", items, 5, only_kind + 1);
            if (c >= 0) { only_kind = c - 1; l.sel = 0; }
        } else if (row < 0) continue;
        else if (k == K_ENTER) { edit_person(row, false); data_save(); }
        else if (k == 'c') { k_seti(&people, row, C_LAST, k_today()); data_save(); k_flash("contacted today: %s", k_get(&people, row, C_NAME)); }
        else if ((k == 'd' || k == KEY_DELETE) && k_confirm("Delete this person?")) { k_table_del(&people, row); data_save(); }
    }
}

/* -------------------------------------------------------------- notes */

static int cmp_note(char *const *a, char *const *b) {
    long x = atol(a[N_DAY]), y = atol(b[N_DAY]);
    return x > y ? -1 : x < y;
}

int view_notes(void) {
    struct k_list l = { 0, 0, 0 };
    for (;;) {
        k_table_sort(&notes, cmp_note);
        l.n = notes.n;
        k_frame();
        header(6);
        k_line(" %d notes", notes.n);
        k_rule();
        int rows = (k_rows - k_used()) / 2 - 1;
        k_list_fit(&l, rows);
        for (int i = l.top; i < l.n && i < l.top + rows; i++) {
            char d[24], pn[64], t[300];
            k_fit(pn, sizeof pn, proj_name(k_geti(&notes, i, N_PROJ)), 16);
            k_fit(t, sizeof t, k_get(&notes, i, N_TITLE), k_cols - 50);
            k_line("%s %-11s %5d words  %s " K_DIM K_SEP K_RESET " %s", SEL(i, l.sel), k_fmt_day(d, sizeof d, k_geti(&notes, i, N_DAY)),
                   k_words(k_get(&notes, i, N_TEXT)), pn, t);
        }
        if (!l.n) k_line("   " K_DIM "no notes yet: press n" K_RESET);
        k_rule();
        if (l.n) {
            char **lines;
            int n = k_wrap(k_get(&notes, l.sel, N_TEXT), k_cols - 6, &lines);
            for (int i = 0; i < n && k_used() < k_rows - 1; i++) {
                char row[600];
                snprintf(row, sizeof row, "   %s", lines[i]);
                k_text_row(row);
            }
            k_wrap_free(lines, n);
        }
        k_footer("n new   Enter write   r rename   j project   d delete   1-8 views   Esc menu");
        k_show();
        int k = key();
        if (k == K_NONE) continue;
        if (k_list_key(&l, k, rows)) continue;
        int v = tab_key(k, 6);
        if (v >= 0 && v != 6) return v;
        if (k == 'n') {
            char title[128];
            if (!k_ask("Title of the note: ", title, sizeof title)) continue;
            int r = k_table_add(&notes);
            k_set(&notes, r, N_TITLE, title);
            k_seti(&notes, r, N_DAY, k_today());
            char *text = NULL;
            char h[200];
            snprintf(h, sizeof h, "Work mode - %s", title);
            if (k_edit(&text, h, NULL)) k_set(&notes, r, N_TEXT, text);
            free(text);
            data_save();
            l.sel = 0;
        } else if (!l.n) continue;
        else if (k == K_ENTER) {
            char *text = strdup(k_get(&notes, l.sel, N_TEXT)), h[200];
            snprintf(h, sizeof h, "Work mode - %s", k_get(&notes, l.sel, N_TITLE));
            if (k_edit(&text, h, NULL)) { k_set(&notes, l.sel, N_TEXT, text); k_seti(&notes, l.sel, N_DAY, k_today()); data_save(); }
            free(text);
        } else if (k == 'r') {
            char title[128];
            if (k_ask("New title: ", title, sizeof title)) { k_set(&notes, l.sel, N_TITLE, title); data_save(); }
        } else if (k == 'j') {
            long p = k_geti(&notes, l.sel, N_PROJ);
            if (pick_project("Work mode - the note's project", &p, true) == 0) { k_seti(&notes, l.sel, N_PROJ, p); data_save(); }
        } else if ((k == 'd' || k == KEY_DELETE) && k_confirm("Delete this note?")) { k_table_del(&notes, l.sel); data_save(); }
    }
}
