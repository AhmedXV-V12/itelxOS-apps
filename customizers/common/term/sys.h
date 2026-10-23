/* The sys commands of the itelx terminal (sys.c). */
#ifndef SYS_H
#define SYS_H

#define ITELX_USERSPACE "itelx 1.4"

void sys_overview(void);              /* sys */
void sys_usage(void);                 /* sys.info */
void sys_device(void);                /* sys.info.device */
void sys_all(void);                   /* sys.all */
int  sys_top_main(int argc, char **argv);   /* the top program (sys.top) */

#endif
