/* kit: what the programs of the customizer apps are built with.
 *
 * The itelx terminal keeps lines of text and has no cursor movement, so a
 * full-screen program draws a whole frame each time: the frame is built
 * in memory and written after "clear the screen" (the terminal only
 * redraws cells that changed). Lines are laid out by the terminal itself:
 * Arabic is shaped and a line that starts with Arabic runs right to left.
 *
 *   screen.c   frames, text width, keys, questions, lists, bars
 *   date.c     calendar dates, times and durations; the time zone
 *   table.c    tables of records in text files, saved safely
 *   editor.c   a full-screen text editor (Arabic and English)
 *   shell.c    what a program that replaces the terminal needs: the system
 *              menu (restart, power off, restore the system), USB update
 *              sticks, starting other programs, a passcode lock
 *   sha256.c   SHA-256, HMAC-SHA-256 and PBKDF2 (passcodes, File saver)
 */
#ifndef KIT_H
#define KIT_H

#include <itx.h>

/* ---- libx.c: C library functions libitx does not have */
size_t strcspn(const char *s, const char *reject);
size_t strspn(const char *s, const char *accept);
void  *memchr(const void *s, int c, size_t n);
size_t strnlen(const char *s, size_t max);
int    strcasecmp(const char *a, const char *b);
int    strncasecmp(const char *a, const char *b, size_t n);
const char *k_strcasestr(const char *h, const char *n);
int    isupper(int c);
int    islower(int c);
int    isxdigit(int c);
int    isprint(int c);
int    ispunct(int c);
void   qsort(void *base, size_t n, size_t w, int (*cmp)(const void *, const void *));

/* ---- colours: the terminal has these six */
#define K_RESET "\x1b[0m"
#define K_DIM   "\x1b[90m"
#define K_YEL   "\x1b[93m"
#define K_RED   "\x1b[91m"
#define K_GRN   "\x1b[92m"
#define K_CYAN  "\x1b[96m"
/* a column separator that is a letter (U+01C0, it looks like a thin bar):
   between two columns that may both be Arabic it keeps them apart */
#define K_SEP   "\xc7\x80"

/* ---- keys (k_key): a Unicode character, or one of these */
#define K_ENTER '\n'
#define K_BS    '\b'
#define K_TAB   '\t'
#define K_ESC   27
#define K_NONE  (-2)                 /* k_key_wait: no key in time */
#define K_CTRL(c) ((c) & 0x1F)       /* Ctrl+letter arrives as a control character */
#define K_F(n)  (KEY_F1 + (n) - 1)   /* F1 .. F12 */

/* ---------------------------------------------------------- screen.c */
extern int k_cols, k_rows;          /* the screen in cells (the status row not counted) */

void k_begin(void);                 /* raw keys on */
void k_end(void);                   /* raw keys off (line input) */
int  k_key(void);                   /* waits for a key; Ctrl+C ends the program */
int  k_key_wait(long ms);           /* a key, or K_NONE after ms */

void k_frame(void);                 /* starts a new frame (and reads the screen size) */
void k_put(const char *s);
void k_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void k_line(const char *fmt, ...) __attribute__((format(printf, 1, 2)));   /* a row of the frame */
/* a row of running text: it takes its direction from its first letter
   (an Arabic row runs right to left and is right aligned); k_line rows
   always run left to right, so columns stay in place */
void k_text_row(const char *s);
int  k_used(void);                  /* rows of the frame so far */
void k_pad_to(int row);             /* empty rows up to row */
void k_show(void);                  /* writes the frame */

/* the program's bar at the top: title, right part (date and time when NULL) */
void k_header(const char *title, const char *right);
void k_rule(void);                  /* a thin line across */
/* the key hints at the bottom, and a message for the next frame */
void k_footer(const char *keys);
void k_flash(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

int  k_width(const char *s);        /* cells the text takes (colour codes not counted) */
/* s cut to cells (with "…" when cut) and padded with spaces to exactly cells */
char *k_fit(char *out, size_t len, const char *s, int cells);
char *k_bar(char *out, size_t len, long value, long max, int cells);   /* ████▌ */
bool k_has_rtl(const char *s);      /* has Arabic letters */
/* text wrapped into rows of at most width cells, broken at spaces; the
   rows are malloc'd strings in a malloc'd array (k_wrap_free) */
int  k_wrap(const char *text, int width, char ***rows);
void k_wrap_free(char **rows, int n);

/* questions: a line typed with the terminal's own editing (Arabic too),
   at the bottom of the current frame; false on an empty answer or Esc */
bool k_ask(const char *prompt, char *buf, size_t len);
bool k_ask_long(const char *prompt, long *v);
bool k_confirm(const char *question);           /* y or n */
/* a menu over the screen; the index chosen, -1 on Esc */
int  k_menu(const char *title, const char *const *items, int n, int sel);
/* waits for a key after a message (the frame is shown) */
void k_message(const char *title, const char *text);

/* a list: the selected row and the first row shown */
struct k_list { int sel, top, n; };
bool k_list_key(struct k_list *l, int key, int page);   /* true when the key moved it */
void k_list_fit(struct k_list *l, int rows);            /* sel in view */

/* ------------------------------------------------------------ date.c */
long k_days(int y, int m, int d);                 /* days since 1970-01-01 */
void k_civil(long days, int *y, int *m, int *d);
int  k_weekday(long days);                        /* 0 Monday .. 6 Sunday */
int  k_month_days(int y, int m);
long k_now(void);                                 /* seconds since 1970, in the user's time zone */
long k_today(void);                               /* days, in the user's time zone */
int  k_minute_of_day(void);                       /* 0 .. 1439 */
const char *k_month_name(int m);                  /* 1 .. 12 */
const char *k_wday_name(int wd);                  /* 0 Monday */
char *k_fmt_date(char *out, size_t len, long days);           /* 2026-10-23 */
char *k_fmt_day(char *out, size_t len, long days);            /* Fri 23 Oct */
char *k_fmt_long(char *out, size_t len, long days);           /* Friday, 23 October 2026 */
char *k_fmt_time(char *out, size_t len, int minutes);         /* 14:05 */
char *k_fmt_dur(char *out, size_t len, long seconds);         /* 1h 05m, 12m, 40s */
char *k_fmt_clock(char *out, size_t len, long seconds);       /* 25:00, 1:02:03 */
char *k_fmt_rel(char *out, size_t len, long days);            /* today, tomorrow, in 3 days, 2 days ago */
/* "2026-10-23", "23/10/2026", "23/10", "today", "tomorrow", "yesterday",
   "+3", "-2", "mon" .. "sun" (the next one), "" -> false */
bool k_parse_date(const char *s, long *days);
bool k_parse_time(const char *s, int *minutes);  /* "14:30", "2:30pm", "1430", "9" */
int  k_tz_minutes(void);                          /* the offset the user set */
void k_tz_set(int minutes);

/* ----------------------------------------------------------- table.c */
/* A table is a text file: a first line naming the fields, then one record
 * per line, fields separated by tabs (tab, newline and backslash written
 * as \t \n \\). Field 0 is a number that names the record (its id). */
struct k_table {
    char path[ITX_PATH_MAX];
    int nf;
    char **names;
    int n, cap;
    char ***rows;
    long next_id;
    bool dirty;
};
bool k_table_open(struct k_table *t, const char *path, const char *const *fields, int nf);
bool k_table_save(struct k_table *t);            /* written next to it, then put in place */
int  k_table_add(struct k_table *t);             /* a new record with the next id: its row */
void k_table_del(struct k_table *t, int row);
int  k_table_row(struct k_table *t, long id);    /* -1 when there is none */
const char *k_get(const struct k_table *t, int row, int f);
long k_geti(const struct k_table *t, int row, int f);
void k_set(struct k_table *t, int row, int f, const char *v);
void k_seti(struct k_table *t, int row, int f, long v);
/* sorts the rows (stable); cmp gets two rows' fields: a[F] is field F */
void k_table_sort(struct k_table *t, int (*cmp)(char *const *a, char *const *b));
void k_table_free(struct k_table *t);

/* files: whole-file helpers that write safely (NAME.new, then renamed) */
bool k_save_file(const char *path, const void *data, size_t len);
void k_mkdirs(const char *dir);                  /* a folder and the ones above it */

/* ------------------------------------------------------------ form.c */
/* the fields of a record as a form: each names a column of the table */
enum k_ftype { KF_TEXT, KF_LONG, KF_DATE, KF_TIME, KF_MINUTES, KF_INT, KF_MONEY, KF_CHOICE, KF_REF, KF_BOOL };
struct k_field {
    const char *label;
    int col;
    enum k_ftype type;
    bool required;
    const char *const *choices;      /* KF_CHOICE: the value is the index */
    int nchoices;
    struct k_table *ref;             /* KF_REF: the value is an id of this table */
    int ref_col;                     /* the column that names it */
    long min, max;                   /* KF_INT, when max > min */
};
/* the record at row as a form: Enter changes a field, Ctrl+S saves (true),
   Esc puts it back as it was (false). A new record asks the required
   fields one after the other first. The table is not saved here. */
bool  k_form(const char *title, struct k_table *t, int row, const struct k_field *f, int nf, bool is_new);
char *k_field_text(char *out, size_t len, const struct k_table *t, int row, const struct k_field *f);
char *k_money(char *out, size_t len, long cents);          /* 1234 -> 12.34 */
bool  k_parse_money(const char *s, long *cents);
/* a clock or a number in big digits: 5 rows, 10 cells per character */
void  k_big(const char *s, char rows[5][512]);

/* ---------------------------------------------------------- editor.c */
/* Edits *text (UTF-8, malloc'd; may be NULL) full screen. Returns true when
 * the user saved (Ctrl+S, or Esc and yes); *text is then the new text.
 * title is shown at the top, info (may be NULL) next to it. */
bool k_edit(char **text, const char *title, const char *info);
int  k_words(const char *text);

/* ----------------------------------------------------------- shell.c */
/* the system menu of a program that replaces the terminal: about, the time
   zone, restart, power off, restore the system (recustomize) */
void k_system_menu(const char *app, const char *about);
/* call every second or so while waiting for keys: when a USB stick with an
   itelx update (update.xvr) is plugged in, the terminal is handed to the
   kernel's question and to the update, then the program goes on */
void k_watch_media(void);
/* runs a program (an .xvr, or an .xvc with the XVC interpreter) with the
   terminal; waits for it and for a key after it */
int  k_run(const char *path, const char *const *args, int nargs);
/* a passcode (kept as salted PBKDF2-SHA-256 in file): k_lock asks for it
   when one is set; k_lock_set sets or removes it */
bool k_lock(const char *file, const char *app);
void k_lock_set(const char *file);
/* the USB volumes (FAT) in /media, the system's boot partition left out */
int  k_sticks(char out[][64], int max);

/* ---------------------------------------------------------- sha256.c */
struct k_sha256 { uint32_t h[8]; uint64_t len; uint8_t buf[64]; size_t n; };
void k_sha256_init(struct k_sha256 *c);
void k_sha256_add(struct k_sha256 *c, const void *data, size_t len);
void k_sha256_end(struct k_sha256 *c, uint8_t out[32]);
void k_sha256(const void *data, size_t len, uint8_t out[32]);
void k_hmac_sha256(const void *key, size_t klen, const void *msg, size_t mlen, uint8_t out[32]);
void k_pbkdf2_sha256(const void *pass, size_t plen, const void *salt, size_t slen, uint32_t iter,
                     uint8_t *out, size_t olen);
void k_hex(char *out, const uint8_t *b, size_t n);
void k_random(void *out, size_t n);              /* from the time stamp counter and the clock */

/* -------------------------------------------------------- chacha20.c */
struct k_chacha20 { uint32_t s[16]; uint8_t ks[64]; int used; };
void k_chacha20_init(struct k_chacha20 *c, const uint8_t key[32], const uint8_t nonce[12], uint32_t counter);
void k_chacha20_xor(struct k_chacha20 *c, void *data, size_t len);   /* encrypts and decrypts */

#endif
