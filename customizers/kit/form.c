/* kit: a form for one record of a table, and big digits for clocks (kit.h). */
#include "kit.h"

/* ------------------------------------------------------------- values */

char *k_money(char *out, size_t len, long cents) {
    long a = cents < 0 ? -cents : cents;
    snprintf(out, len, "%s%ld.%02ld", cents < 0 ? "-" : "", a / 100, a % 100);
    return out;
}

bool k_parse_money(const char *s, long *cents) {
    while (*s == ' ') s++;
    bool neg = *s == '-';
    if (neg || *s == '+') s++;
    long whole = 0, frac = 0;
    int nd = 0, nf = 0;
    while (isdigit(*s) || *s == ',') { if (*s != ',') { whole = whole * 10 + (*s - '0'); nd++; } s++; }
    if (*s == '.') {
        s++;
        while (isdigit(*s) && nf < 2) { frac = frac * 10 + (*s - '0'); nf++; s++; }
        while (isdigit(*s)) s++;
        if (nf == 1) frac *= 10;
    }
    while (*s == ' ') s++;
    if (*s || (!nd && !nf)) return false;
    *cents = (whole * 100 + frac) * (neg ? -1 : 1);
    return true;
}

char *k_field_text(char *out, size_t len, const struct k_table *t, int row, const struct k_field *f) {
    const char *v = k_get(t, row, f->col);
    long n = atol(v);
    char tmp[64];
    out[0] = 0;
    switch (f->type) {
    case KF_DATE:
        if (n) { char a[24], b[24]; snprintf(out, len, "%s  (%s)", k_fmt_day(a, sizeof a, n), k_fmt_rel(b, sizeof b, n)); }
        break;
    case KF_TIME:
        if (v[0]) k_fmt_time(out, len, (int)n);
        break;
    case KF_MINUTES:
        if (v[0]) k_fmt_dur(out, len, n * 60);
        break;
    case KF_MONEY:
        if (v[0]) k_money(out, len, n);
        break;
    case KF_BOOL:
        snprintf(out, len, "%s", n ? "yes" : "no");
        break;
    case KF_CHOICE:
        if (n >= 0 && n < f->nchoices) snprintf(out, len, "%s", f->choices[n]);
        break;
    case KF_REF: {
        int r = n ? k_table_row(f->ref, n) : -1;
        if (r >= 0) snprintf(out, len, "%s", k_get(f->ref, r, f->ref_col));
        break;
    }
    case KF_LONG: {
        /* the first line, and how many more */
        size_t l = strcspn(v, "\n");
        int lines = 0;
        for (const char *p = v; *p; p++) if (*p == '\n') lines++;
        snprintf(tmp, sizeof tmp, lines ? "  (+%d lines)" : "", lines);
        snprintf(out, len, "%.*s%s", (int)(l < 200 ? l : 200), v, tmp);
        break;
    }
    default:
        snprintf(out, len, "%s", v);
    }
    return out;
}

/* asks for one field; false when it was left as it was */
static bool edit_field(const char *form, struct k_table *t, int row, const struct k_field *f) {
    char title[160];
    snprintf(title, sizeof title, "%s: %s", form, f->label);
    char b[512], q[256];
    snprintf(q, sizeof q, "%s: ", f->label);
    switch (f->type) {
    case KF_LONG: {
        char *text = strdup(k_get(t, row, f->col));
        bool ok = k_edit(&text, title, NULL);
        if (ok) k_set(t, row, f->col, text);
        free(text);
        return ok;
    }
    case KF_CHOICE: {
        int c = k_menu(title, f->choices, f->nchoices, (int)k_geti(t, row, f->col));
        if (c < 0) return false;
        k_seti(t, row, f->col, c);
        return true;
    }
    case KF_BOOL:
        k_seti(t, row, f->col, !k_geti(t, row, f->col));
        return true;
    case KF_REF: {
        int n = f->ref->n + (f->required ? 0 : 1);
        const char **items = malloc(sizeof(char *) * (n ? n : 1));
        int base = f->required ? 0 : 1;
        if (!f->required) items[0] = "(none)";
        for (int i = 0; i < f->ref->n; i++) items[base + i] = k_get(f->ref, i, f->ref_col);
        if (!n) { free(items); k_flash("there is nothing to choose from yet"); return false; }
        int cur = 0, id = (int)k_geti(t, row, f->col);
        for (int i = 0; i < f->ref->n; i++) if (k_geti(f->ref, i, 0) == id) cur = base + i;
        int c = k_menu(title, items, n, cur);
        free(items);
        if (c < 0) return false;
        k_seti(t, row, f->col, c < base ? 0 : k_geti(f->ref, c - base, 0));
        return true;
    }
    default:
        break;
    }
    const char *hint = f->type == KF_DATE ? " (2026-10-23, 23/10, today, tomorrow, +3, fri)" :
                       f->type == KF_TIME ? " (14:30, 2:30pm)" : f->type == KF_MINUTES ? " (minutes, or 1:30 for hours)" :
                       f->type == KF_MONEY ? " (12.50)" : "";
    snprintf(q, sizeof q, "%s%s: ", f->label, hint);
    if (!k_ask(q, b, sizeof b)) {
        if (f->required) return false;
        /* an empty answer clears an optional field */
        if (k_get(t, row, f->col)[0] && k_confirm("Leave it empty?")) { k_set(t, row, f->col, ""); return true; }
        return false;
    }
    long n;
    int m;
    switch (f->type) {
    case KF_DATE:
        if (!k_parse_date(b, &n)) { k_flash("not a date: %s", b); return false; }
        k_seti(t, row, f->col, n);
        return true;
    case KF_TIME:
        if (!k_parse_time(b, &m)) { k_flash("not a time: %s", b); return false; }
        k_seti(t, row, f->col, m);
        return true;
    case KF_MINUTES: {
        char *e;
        long h = strtol(b, &e, 10), mm = 0;
        if (*e == ':') { mm = strtol(e + 1, &e, 10); n = h * 60 + mm; }
        else n = h;
        if (*e || n < 0) { k_flash("not a number of minutes: %s", b); return false; }
        k_seti(t, row, f->col, n);
        return true;
    }
    case KF_INT: {
        char *e;
        n = strtol(b, &e, 10);
        if (*e) { k_flash("not a number: %s", b); return false; }
        if (f->max > f->min && (n < f->min || n > f->max)) { k_flash("a number from %ld to %ld", f->min, f->max); return false; }
        k_seti(t, row, f->col, n);
        return true;
    }
    case KF_MONEY:
        if (!k_parse_money(b, &n)) { k_flash("not an amount: %s", b); return false; }
        k_seti(t, row, f->col, n);
        return true;
    default:
        k_set(t, row, f->col, b);
        return true;
    }
}

bool k_form(const char *title, struct k_table *t, int row, const struct k_field *f, int nf, bool is_new) {
    /* the values as they were, for Esc */
    char **old = malloc(sizeof(char *) * t->nf);
    for (int i = 0; i < t->nf; i++) old[i] = strdup(k_get(t, row, i));
    bool changed = false;
    if (is_new) {
        /* a new record: the fields that must be filled are asked one after the other */
        for (int i = 0; i < nf; i++) {
            if (!f[i].required || f[i].type == KF_BOOL) continue;
            k_frame();
            k_header(title, "");
            k_pad_to(3);
            for (int j = 0; j < nf; j++) {
                char v[300], l[64];
                k_fit(l, sizeof l, f[j].label, 18);
                k_line("   %s%s  %s" K_RESET, j == i ? K_YEL : K_DIM, l, k_field_text(v, sizeof v, t, row, &f[j]));
            }
            k_footer("");
            if (!edit_field(title, t, row, &f[i])) {
                if (f[i].required && !k_get(t, row, f[i].col)[0]) {
                    for (int c = 0; c < t->nf; c++) k_set(t, row, c, old[c]);
                    for (int c = 0; c < t->nf; c++) free(old[c]);
                    free(old);
                    return false;
                }
            }
            changed = true;
        }
    }
    struct k_list l = { 0, 0, nf };
    for (;;) {
        k_frame();
        k_header(title, "");
        k_line("%s", "");
        int lw = 4;
        for (int i = 0; i < nf; i++) if (k_width(f[i].label) > lw) lw = k_width(f[i].label);
        for (int i = 0; i < nf; i++) {
            char v[600], lab[64], vv[600];
            k_fit(lab, sizeof lab, f[i].label, lw);
            k_field_text(v, sizeof v, t, row, &f[i]);
            k_fit(vv, sizeof vv, v[0] ? v : "-", k_cols - lw - 10);
            k_line("%s %s%s" K_RESET "  %s", i == l.sel ? K_YEL "\xe2\x96\xb6" : " ", i == l.sel ? K_YEL : K_DIM, lab, vv);
        }
        k_footer("arrows choose   Enter change   Ctrl+S or F2 save   Esc cancel");
        k_show();
        int k = k_key();
        if (k_list_key(&l, k, nf)) continue;
        if (k == K_ENTER) { if (edit_field(title, t, row, &f[l.sel])) changed = true; continue; }
        if (k == K_CTRL('s') || k == K_F(2)) {
            bool missing = false;
            for (int i = 0; i < nf; i++) if (f[i].required && !k_get(t, row, f[i].col)[0]) { missing = true; l.sel = i; }
            if (missing) { k_flash("%s is needed", f[l.sel].label); continue; }
            for (int c = 0; c < t->nf; c++) free(old[c]);
            free(old);
            t->dirty = true;
            return true;
        }
        if (k == K_ESC) {
            if (changed && !is_new && !k_confirm("Throw the changes away?")) continue;
            if (is_new && changed && !k_confirm("Throw this new one away?")) continue;
            for (int c = 0; c < t->nf; c++) { k_set(t, row, c, old[c]); free(old[c]); }
            free(old);
            return false;
        }
    }
}

/* ------------------------------------------------------- big digits */

/* 0-9, ':' and ' ' in 5 rows of 4 cells, drawn with full blocks */
static const char *const big[12][5] = {
    { "####", "#  #", "#  #", "#  #", "####" }, { "  # ", " ## ", "  # ", "  # ", " ###" },
    { "####", "   #", "####", "#   ", "####" }, { "####", "   #", " ###", "   #", "####" },
    { "#  #", "#  #", "####", "   #", "   #" }, { "####", "#   ", "####", "   #", "####" },
    { "####", "#   ", "####", "#  #", "####" }, { "####", "   #", "  # ", " #  ", " #  " },
    { "####", "#  #", "####", "#  #", "####" }, { "####", "#  #", "####", "   #", "####" },
    { "    ", " ## ", "    ", " ## ", "    " }, { "    ", "    ", "    ", "    ", "    " },
};

void k_big(const char *s, char rows[5][512]) {
    for (int r = 0; r < 5; r++) {
        size_t o = 0;
        for (const char *p = s; *p && o < 480; p++) {
            int g = *p >= '0' && *p <= '9' ? *p - '0' : *p == ':' ? 10 : 11;
            for (int c = 0; c < 4; c++) {
                if (big[g][r][c] == '#') { memcpy(rows[r] + o, "\xe2\x96\x88\xe2\x96\x88", 6); o += 6; }
                else { rows[r][o++] = ' '; rows[r][o++] = ' '; }
            }
            rows[r][o++] = ' ';
            rows[r][o++] = ' ';
        }
        rows[r][o] = 0;
    }
}
