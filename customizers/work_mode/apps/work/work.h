/* Work mode: itelx for work. The parts of the program; work.c is the
 * start, the data, the menu and the running timer. */
#ifndef WORK_H
#define WORK_H

#include <kit.h>

#define HOME "/home/work"

enum { P_ID, P_NAME, P_CLIENT, P_STATUS, P_START, P_DUE, P_BUDGET, P_RATE, P_DESC, P_N };               /* projects */
enum { T_ID, T_PROJ, T_TITLE, T_STATUS, T_PRIO, T_DUE, T_EST, T_WHO, T_TAGS, T_NOTES, T_CREATED, T_DONE,
       T_BLOCKED, T_N };                                                                                /* tasks */
enum { M_ID, M_PROJ, M_TASK, M_DAY, M_START, M_SECS, M_BILL, M_NOTE, M_INVOICE, M_N };                  /* time entries */
enum { G_ID, G_TITLE, G_DAY, G_START, G_LEN, G_WHERE, G_PEOPLE, G_PROJ, G_AGENDA, G_NOTES, G_N };       /* meetings */
enum { C_ID, C_NAME, C_COMPANY, C_ROLE, C_KIND, C_PHONE, C_EMAIL, C_NOTES, C_LAST, C_N };               /* people */
enum { D_ID, D_TITLE, D_PERSON, D_VALUE, D_STAGE, D_CLOSE, D_NOTES, D_N };                              /* deals */
enum { N_ID, N_TITLE, N_PROJ, N_DAY, N_TEXT, N_N };                                                     /* notes */
enum { I_ID, I_NUMBER, I_CLIENT, I_FROM, I_TO, I_AMOUNT, I_STATUS, I_DAY, I_DUE, I_N };                 /* invoices */
enum { X_ID, X_PROJ, X_DAY, X_AMOUNT, X_CAT, X_NOTE, X_N };                                             /* expenses */

extern struct k_table projects, tasks, times, meetings, people, deals, notes, invoices, expenses;
void data_load(void);
void data_save(void);

struct work_settings {
    char name[48], company[64], currency[8];
    long rate;                    /* cents an hour, for projects without their own */
    int day_hours;                /* hours of a working day */
    int week_start;               /* 0 Monday .. 6 Sunday */
};
extern struct work_settings cfg;
void settings_save(void);

extern const char *const task_status[], *const prio_name[], *const project_status[], *const person_kind[],
    *const deal_stage[], *const invoice_status[], *const expense_cat[];
extern const int n_task_status, n_prio, n_project_status, n_person_kind, n_deal_stage, n_invoice_status, n_expense_cat;

/* the running timer (it survives a restart: /home/work/timer.txt) */
struct running { bool on; long proj, task, since; bool bill; char note[128]; };
extern struct running timer;
void timer_start(long proj, long task, const char *note);
void timer_stop(void);                       /* the time goes into the time entries */
long timer_secs(void);
void timer_status(char *out, size_t len);    /* for the header */

/* names and numbers */
const char *proj_name(long id);
const char *person_name(long id);
const char *task_title(long id);
long proj_rate(long proj);                   /* cents an hour */
long proj_secs(long proj, long from, long to);   /* proj 0: all */
long week_first(long day);
int  pick_project(const char *title, long *id, bool allow_none);
int  pick_task(const char *title, long proj, long *id);
const char *prio_colour(long p);
const char *cur(void);                       /* the currency */

/* the views; each returns the next view */
int view_dashboard(void);
int view_tasks(void);
int view_projects(void);
int view_time(void);
int view_calendar(void);
int view_clients(void);
int view_notes(void);
int view_reports(void);

bool edit_task(int row, bool is_new);
bool edit_project(int row, bool is_new);
bool edit_meeting(int row, bool is_new);
bool edit_person(int row, bool is_new);
bool edit_deal(int row, bool is_new);
bool edit_time(int row, bool is_new);
void new_task(long proj);
void project_detail(long id);

void invoices_menu(void);
void expenses_menu(void);
void menu(void);
void header(int tab);                        /* the bar, the tabs and the running timer */
int  tab_key(int k, int self);
int  key(void);                              /* a key, or K_NONE after a second */
bool export_to_stick(const char *what, const char *name, const char *text, size_t len);

#endif
