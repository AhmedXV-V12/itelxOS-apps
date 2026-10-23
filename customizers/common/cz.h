/* cz: what the customizer installers (cz_install.c) and recustomize share.
 *
 * A customizer app is one XVR file: this installer with a payload (made by
 * tools/mkczpayload.py) that holds every file it puts on the system: the
 * apps, the program that takes the terminal's place, and, for some, an
 * itelx kernel with more drivers.
 *
 * The kernel lives on the system's boot partition. itelx 1.4 lets only an
 * update the user accepted write it (the "are you need update ?" question
 * of a USB stick), so a customizer that brings a kernel installs from a
 * stick: run from the terminal, it copies itself to a stick as update.xvr
 * and asks the user to plug the stick in again.
 */
#ifndef CZ_H
#define CZ_H

#include <itx.h>

#define CZ_CYAN   "\x1b[96m"
#define CZ_YELLOW "\x1b[93m"
#define CZ_GREEN  "\x1b[92m"
#define CZ_RED    "\x1b[91m"
#define CZ_DIM    "\x1b[90m"
#define CZ_RESET  "\x1b[0m"

/* ------------------------------------------------------------- payload */

#define T_ITFS 1            /* a file for the system disk */
#define T_BOOT 2            /* a file for the system's boot partition */
#define T_DIR  4            /* a directory to make on the system disk */
#define T_INFO 8            /* kept for the installer (about.txt) */

struct cz_head {
    char magic[8];          /* "ITXCZ001" */
    uint32_t count;
    uint32_t version;       /* major << 8 | minor */
    char id[16];
    char title[32];
};

struct cz_entry {
    char path[104];
    uint32_t targets;
    uint32_t offset;        /* from the start of the payload */
    uint32_t size;
    uint32_t crc;
    uint8_t reserved[8];
};

extern const unsigned char cz_payload[], cz_payload_end[];

const struct cz_head *cz_head(void);
const struct cz_entry *cz_entries(void);
const unsigned char *cz_data(const struct cz_entry *e);
const struct cz_entry *cz_find(const char *path);
bool cz_verify(char *why, size_t len);          /* every file intact */

/* -------------------------------------------------------------- checks */

void     cz_crc_init(void);
uint32_t cz_crc(uint32_t crc, const void *data, size_t len);
bool     cz_file_crc(const char *path, uint32_t *crc, uint64_t *size);
int      cz_version_num(const char *v);        /* "itelx 1.4 net" -> 0x0104 */
bool     cz_system_ok(void);                   /* itelx 1.4, or says why not */

/* ------------------------------------------------------------- writing */

enum { F_SAME, F_DONE, F_ROFS, F_FAIL };

void cz_make_dirs(const char *file);           /* the directories above a file */
/* one file put in place: NAME.new written, read back, checked, renamed */
int  cz_install(const char *dst, const void *data, uint32_t size, uint32_t crc, char *why, size_t wlen);
int  cz_copy_file(const char *from, const char *to, uint32_t *crc);
void cz_join(char *out, size_t len, const char *root, const char *path);

/* --------------------------------------------------------- the machine */

bool cz_find_boot(char *out, size_t len);      /* the system's boot partition in /media */
bool cz_boot_writable(const char *boot);       /* this process may write it */
bool cz_find_stick(char *out, size_t len, const char *not_this);   /* a writable USB volume */
bool cz_ask_yes(const char *q);                /* yes/no on the terminal */
bool cz_ask_word(const char *q, const char *word);   /* the exact word typed */
int  cz_restart_terminal(void);                /* init starts /system/terminal.xvr again */
int  cz_stop_others(void);                     /* stops every program but this one, its starters and init */
void cz_today(char *out, size_t len);          /* YYYY-MM-DD HH:MM */

/* -------------------------------------------- the update stick (kernel) */

#define CZ_STICK_MARK "itelx-customizer.txt"

/* copies this program to a USB stick as update.xvr; false when there is
 * no stick or it could not be written (the reason is printed) */
bool cz_to_stick(const char *self, const char *title);
/* after an install from a stick this program prepared: update.xvr and the
 * mark go, an update.xvr that was there before comes back */
void cz_clean_stick(const char *stick);

/* -------------------------------------------- what is installed (state) */

#define CZ_STATE "/system/customizer.txt"

struct cz_state {
    char id[16];
    char title[32];
    char version[8];
    char installed[24];
    bool kernel;
};
bool cz_state_read(struct cz_state *s);

#endif
