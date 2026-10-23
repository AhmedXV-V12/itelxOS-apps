/* kit: dates, times, durations and the time zone (kit.h).
 *
 * itelx reads the clock of the computer (the RTC) as it is. A computer
 * that also runs Windows keeps local time there, one with Linux keeps UTC;
 * the user's offset (k_tz_set, in the system menu) is added to it. */
#include "kit.h"

#define TZ_FILE "/home/settings.txt"

static int tz = -100000;

int k_tz_minutes(void) {
    if (tz != -100000) return tz;
    tz = 0;
    size_t len;
    char *t = read_file(TZ_FILE, &len, 4096);
    if (t) {
        char *p = strstr(t, "tz=");
        if (p) tz = (int)strtol(p + 3, NULL, 10);
        free(t);
    }
    if (tz < -14 * 60 || tz > 14 * 60) tz = 0;
    return tz;
}

void k_tz_set(int minutes) {
    tz = minutes;
    char buf[64];
    int n = snprintf(buf, sizeof buf, "tz=%d\n", minutes);
    k_mkdirs("/home");
    k_save_file(TZ_FILE, buf, (size_t)n);
}

long k_now(void) { return unix_time() + (long)k_tz_minutes() * 60; }
long k_today(void) { return k_now() / 86400; }
int k_minute_of_day(void) { return (int)(k_now() % 86400 / 60); }

/* Howard Hinnant's algorithms: days since 1970 <-> a civil date */
long k_days(int y, int m, int d) {
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    long yoe = y - era * 400;
    long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

void k_civil(long days, int *y, int *m, int *d) {
    long z = days + 719468, era = (z >= 0 ? z : z - 146096) / 146097;
    long doe = z - era * 146097, yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long yy = yoe + era * 400, doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = (int)(yy + (*m <= 2));
}

int k_weekday(long days) {
    long w = (days + 3) % 7;                 /* 1970-01-01 was a Thursday */
    return (int)(w < 0 ? w + 7 : w);
}

int k_month_days(int y, int m) {
    static const int n[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (m == 2 && ((y % 4 == 0 && y % 100) || y % 400 == 0)) return 29;
    return n[(m - 1) % 12];
}

static const char *const months[] = { "January", "February", "March", "April", "May", "June", "July",
                                      "August", "September", "October", "November", "December" };
static const char *const wdays[] = { "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday" };

const char *k_month_name(int m) { return m >= 1 && m <= 12 ? months[m - 1] : "?"; }
const char *k_wday_name(int wd) { return wd >= 0 && wd <= 6 ? wdays[wd] : "?"; }

char *k_fmt_date(char *out, size_t len, long days) {
    int y, m, d;
    k_civil(days, &y, &m, &d);
    snprintf(out, len, "%04d-%02d-%02d", y, m, d);
    return out;
}

char *k_fmt_day(char *out, size_t len, long days) {
    int y, m, d;
    k_civil(days, &y, &m, &d);
    snprintf(out, len, "%.3s %d %.3s", wdays[k_weekday(days)], d, months[m - 1]);
    return out;
}

char *k_fmt_long(char *out, size_t len, long days) {
    int y, m, d;
    k_civil(days, &y, &m, &d);
    snprintf(out, len, "%s, %d %s %d", wdays[k_weekday(days)], d, months[m - 1], y);
    return out;
}

char *k_fmt_time(char *out, size_t len, int minutes) {
    if (minutes < 0) { snprintf(out, len, "--:--"); return out; }
    snprintf(out, len, "%02d:%02d", minutes / 60 % 24, minutes % 60);
    return out;
}

char *k_fmt_dur(char *out, size_t len, long s) {
    if (s < 0) s = 0;
    if (s >= 3600) snprintf(out, len, "%ldh %02ldm", s / 3600, s / 60 % 60);
    else if (s >= 60) snprintf(out, len, "%ldm", s / 60);
    else snprintf(out, len, "%lds", s);
    return out;
}

char *k_fmt_clock(char *out, size_t len, long s) {
    if (s < 0) s = 0;
    if (s >= 3600) snprintf(out, len, "%ld:%02ld:%02ld", s / 3600, s / 60 % 60, s % 60);
    else snprintf(out, len, "%02ld:%02ld", s / 60, s % 60);
    return out;
}

char *k_fmt_rel(char *out, size_t len, long days) {
    long d = days - k_today();
    if (d == 0) snprintf(out, len, "today");
    else if (d == 1) snprintf(out, len, "tomorrow");
    else if (d == -1) snprintf(out, len, "yesterday");
    else if (d > 1) snprintf(out, len, "in %ld days", d);
    else snprintf(out, len, "%ld days ago", -d);
    return out;
}

static int num(const char **p) {
    int v = 0, n = 0;
    while (isdigit(**p) && n < 6) { v = v * 10 + (**p - '0'); (*p)++; n++; }
    return n ? v : -1;
}

bool k_parse_date(const char *s, long *days) {
    while (*s == ' ') s++;
    if (!*s) return false;
    long today = k_today();
    char low[16];
    size_t i = 0;
    for (; s[i] && i < sizeof low - 1; i++) low[i] = (char)tolower(s[i]);
    low[i] = 0;
    if (!strcmp(low, "today") || !strcmp(low, "t")) { *days = today; return true; }
    if (!strcmp(low, "tomorrow") || !strcmp(low, "tm")) { *days = today + 1; return true; }
    if (!strcmp(low, "yesterday")) { *days = today - 1; return true; }
    if ((s[0] == '+' || s[0] == '-') && isdigit(s[1])) {
        const char *p = s + 1;
        int n = num(&p);
        if (*p == 'w') { n *= 7; p++; }
        if (*p) return false;
        *days = today + (s[0] == '+' ? n : -n);
        return true;
    }
    for (int w = 0; w < 7; w++) {
        char ab[4];
        for (int k = 0; k < 3; k++) ab[k] = (char)tolower(wdays[w][k]);
        ab[3] = 0;
        if (!strncmp(low, ab, 3) && strlen(low) <= strlen(wdays[w])) {
            int diff = (w - k_weekday(today) + 7) % 7;
            *days = today + (diff ? diff : 7);
            return true;
        }
    }
    const char *p = s;
    int a = num(&p);
    if (a < 0) return false;
    if (*p == '-' && a > 31) {                          /* 2026-10-23 */
        p++;
        int m = num(&p);
        if (*p != '-') return false;
        p++;
        int d = num(&p);
        if (*p || m < 1 || m > 12 || d < 1 || d > k_month_days(a, m)) return false;
        *days = k_days(a, m, d);
        return true;
    }
    if (*p == '/' || *p == '.' || *p == '-') {          /* 23/10/2026, 23/10 */
        char sep = *p++;
        int m = num(&p), y;
        if (m < 1 || m > 12) return false;
        if (*p == sep) {
            p++;
            y = num(&p);
            if (y < 0) return false;
            if (y < 100) y += 2000;
        } else {
            int ty, tm, td;
            k_civil(today, &ty, &tm, &td);
            y = ty;
        }
        if (*p || a < 1 || a > k_month_days(y, m)) return false;
        *days = k_days(y, m, a);
        return true;
    }
    return false;
}

bool k_parse_time(const char *s, int *minutes) {
    while (*s == ' ') s++;
    const char *p = s;
    int h = num(&p), m = 0;
    if (h < 0) return false;
    if (*p == ':' || *p == '.') { p++; m = num(&p); if (m < 0) return false; }
    else if (h >= 100) { m = h % 100; h /= 100; }   /* 1430 */
    while (*p == ' ') p++;
    if (!strcmp(p, "pm") || !strcmp(p, "PM")) { if (h < 12) h += 12; p += 2; }
    else if (!strcmp(p, "am") || !strcmp(p, "AM")) { if (h == 12) h = 0; p += 2; }
    if (*p || h > 23 || m > 59) return false;
    *minutes = h * 60 + m;
    return true;
}
