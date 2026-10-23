/* SQLite on itelx: the time types its date functions name (SQLITE_OMIT_LOCALTIME) */
#ifndef SHIM_TIME_H
#define SHIM_TIME_H
typedef long time_t;
struct tm {
    int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday, tm_yday, tm_isdst;
};
time_t time(time_t *t);
#endif
