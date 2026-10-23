/* Noter: itelx as a journal. The parts of the program (noter.c is the
 * start and the main views). */
#ifndef NOTER_H
#define NOTER_H

#include <kit.h>

#define HOME     "/home/noter"
#define ENTRIES  HOME "/entries"
#define LOCKFILE HOME "/passcode"
#define SETFILE  HOME "/settings.txt"
#define JFILE    HOME "/journals.txt"

/* an entry as the lists see it; the text itself stays in its file */
struct entry {
    int id;
    long day;                    /* days since 1970 */
    int minute;                  /* of the day */
    int mood;                    /* 0 none, 1 awful .. 5 great */
    bool star;
    int words;
    char journal[32];
    char tags[160];              /* "#work #family" (from the text and added) */
    char title[160];             /* the first line */
    char prompt[160];            /* the question it answered, if any */
};

extern struct entry *ents;      /* newest first */
extern int nents;

/* store.c */
void  store_load(void);
char *entry_text(const struct entry *e);                 /* the whole text (malloc'd) */
bool  entry_save(struct entry *e, const char *text);     /* new when e->id is 0 */
bool  entry_delete(int id);
struct entry *entry_by_id(int id);
void  entry_refresh(struct entry *e, const char *text);  /* title, words, tags from the text */
bool  entry_matches(const struct entry *e, const char *query);   /* in its text, any case */
int   entries_on(long day);
int   streak_now(int *best);
int   entry_import(const char *file_text);               /* 1 added, 0 already here, -1 not an entry */
char *entry_file(const struct entry *e);                 /* header and text (malloc'd) */

extern int g_next_key;           /* a key a view hands back to the main loop */

/* journals and settings */
extern char journals[16][32];
extern int njournals;
extern char cur_journal[32];     /* "" = all journals */
void  journals_load(void);
void  journals_save(void);
struct settings {
    char name[48];
    int reminder;                /* minute of the day, -1 none */
    bool prompts;                /* a question of the day on the timeline */
};
extern struct settings set;
void  settings_load(void);
void  settings_save(void);

/* views.c */
void view_calendar(void);
void view_onthisday(void);
void view_insights(void);
void view_prompts(void);
void entry_read(int id);
int  entry_new(long day, const char *journal, const char *prompt, const char *initial);
void entry_edit(int id);
void pick_mood(struct entry *e);

/* prompts.c */
extern const char *const prompts[];
extern const int nprompts;
const char *prompt_of_day(long day);
struct tmpl { const char *name, *text; };
extern const struct tmpl templates[];
extern const int ntemplates;

/* port.c: export and import with USB sticks */
void export_menu(void);
void import_menu(void);

/* a mood's mark and word */
const char *mood_mark(int mood);
const char *mood_word(int mood);
const char *mood_colour(int mood);

#endif
