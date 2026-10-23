/* School mode: itelx for a student. The parts of the program; school.c
 * is the start, the menu and the subjects. */
#ifndef SCHOOL_H
#define SCHOOL_H

#include <kit.h>

#define HOME "/home/school"

/* the tables (kit table.c), one text file each in /home/school */
enum { S_ID, S_NAME, S_TEACHER, S_ROOM, S_GOAL, S_TIMER, S_COLOUR, S_N };          /* subjects */
enum { C_ID, C_SUBJ, C_DAY, C_START, C_END, C_ROOM, C_N };                        /* classes (the timetable) */
enum { T_ID, T_SUBJ, T_TITLE, T_KIND, T_DUE, T_PRIO, T_PROG, T_DONE, T_NOTES, T_CREATED, T_N };   /* homework */
enum { E_ID, E_SUBJ, E_TITLE, E_DAY, E_TIME, E_ROOM, E_TOPICS, E_RESULT, E_N };  /* exams */
enum { N_ID, N_SUBJ, N_DAY, N_START, N_SECS, N_KIND, N_NOTE, N_N };              /* study sessions */
enum { G_ID, G_SUBJ, G_TITLE, G_SCORE, G_MAX, G_WEIGHT, G_DAY, G_N };            /* grades (scores x100) */
enum { D_ID, D_DAY, D_TEXT, D_N };                                                /* the diary */

extern struct k_table subjects, classes, tasks, exams, sessions, grades, diary;
void data_load(void);
void data_save(void);                     /* every table that changed */

struct school_settings {
    char name[48], school[64];
    int work, rest, long_rest;            /* Pomodoro minutes */
    int week_start;                       /* 0 Monday, 5 Saturday, 6 Sunday */
    int daily_goal;                       /* minutes of study a day */
};
extern struct school_settings cfg;
void settings_load(void);
void settings_save(void);

/* subjects */
const char *subj_name(long id);
const char *subj_colour(long id);
int  pick_subject(const char *title, long *id);   /* 0 chosen, -1 none */
long study_secs(long subj, long from_day, long to_day);   /* subj 0: all */
long week_first(long day);                         /* the first day of day's week */

/* the views */
int  view_today(void);
int  view_homework(void);
int  view_timetable(void);
int  view_timers(void);
int  view_exams(void);
int  view_diary(void);
int  view_stats(void);

/* the timer screen: counts down (minutes > 0) or up (0); a Pomodoro when
   pomodoro is set. The time studied is logged as a session. */
void run_timer(long subj, int minutes, bool pomodoro);

/* the edit forms */
bool edit_task(int row, bool is_new);
bool edit_exam(int row, bool is_new);
bool edit_class(int row, bool is_new);
bool edit_subject(int row, bool is_new);

void menu(void);
void tabs(int active);
int  tab_key(int k, int self);            /* the view a key switches to, -1 none */
extern const char *const kinds[];
extern const int nkinds;

#endif
