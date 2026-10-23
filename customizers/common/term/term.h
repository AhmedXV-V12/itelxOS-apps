/* The terminal of a customizer app: the itelx 1.4 terminal (terminal.c,
 * sys.c) with these hooks, which the customizer's own code defines. It is
 * installed as /system/terminal.xvr, so it is what itelx starts. */
#ifndef TERM_H
#define TERM_H

#include <itx.h>

/* defined by the customizer */
void term_start(void);                      /* once, before the banner */
void term_banner(void);
void term_prompt(const char *cwd);
bool term_command(int argc, char **argv);   /* true: it was one of its commands */
void term_help(void);                       /* after the itelx commands */

/* from terminal.c */
const char *term_cwd(void);
void term_err(const char *what, int code);
void term_absolute(const char *path, char *out, size_t len);
bool term_within(const char *a, const char *b);         /* b is a or inside it */
void term_human(uint64_t n, char *out, size_t len);     /* 12 KiB, 3 MiB */
int  term_copy_tree(const char *a, const char *b, uint64_t *total, int *files);
int  term_del_tree(const char *p);
void term_exec(const char *path, int argc, char **argv);  /* runs a program in the foreground */

#endif
