/* formater: wipes the system disk of itelxOS 2.0, all but the kernel and
 * the boot files.
 *
 *   formater
 *
 * Everything at the top of the disk goes - /system, /dex, /users and any
 * other file or folder - except:
 *   /kernel   the kernel and its modules
 *   /boot     the boot files
 *   /media    USB sticks and other disks: never touched
 *   /ram      memory, not the disk
 * and any other disk mounted at the top (it is not the system disk).
 *
 * It asks y/n first. Without /system the computer cannot start the system
 * any more: at the next start itelx finds that, the boot manager shows the
 * red recovery screen, and "Use backup" there puts the backup system back
 * (then update.xvr brings the full itelxOS). itelx has no user accounts:
 * every program runs with the owner's (root's) rights, so formater can
 * remove all of it. The XVR asks for itelx 2.0 (min_os): itelx 1.x has no
 * recovery screen, and its kernel refuses to start formater. */
#include <itx.h>

#define RED "\x1b[91m"
#define GREEN "\x1b[92m"
#define YELLOW "\x1b[93m"
#define RESET "\x1b[0m"

#define SYS_CMDLINE 70                           /* itelx 2.0 */

static int removed, failed;

static bool kept_name(const char *n) {
    return !strcmp(n, "kernel") || !strcmp(n, "boot") || !strcmp(n, "media") || !strcmp(n, "ram");
}

static bool yes(const char *question) {
    printf("%s (y/n) ", question);
    flush();
    char line[16];
    ssize_t n = read(0, line, sizeof line - 1);
    if (n <= 0) return false;
    line[n] = 0;
    char *p = line;
    while (isspace((unsigned char)*p)) p++;
    return *p == 'y' || *p == 'Y';
}

/* a start of the backup system from the red screen ("Use manual") */
static bool recovery_start(void) {
    char cl[512];
    long n = itx_syscall(SYS_CMDLINE, (long)cl, sizeof cl - 1, 0, 0, 0, 0);
    if (n <= 0) return false;
    cl[n < (long)sizeof cl ? n : (long)sizeof cl - 1] = 0;
    return strstr(cl, "itelx.recovery") != NULL;
}

/* the backup system the red screen puts back: \recovery\recovery.img on
 * the system's boot partition (in /media, "(boot files)" in the disk list) */
static bool backup_present(void) {
    struct itx_diskinfo dk[32];
    int n = disklist(dk, 32);
    for (int i = 0; i < n; i++) {
        char *b = strstr(dk[i].use, " (boot files)");
        if (!b) continue;
        char p[ITX_PATH_MAX];
        struct itx_stat st;
        snprintf(p, sizeof p, "%.*s/recovery/recovery.img", (int)(b - dk[i].use), dk[i].use);
        return stat(p, &st) == 0 && st.size > 0;
    }
    return false;
}

/* a file, or a folder with everything in it */
static int del_tree(const char *p) {
    struct itx_stat st;
    int r = stat(p, &st);
    if (r) return r;
    if (st.type == VT_DIR) {
        for (;;) {
            int fd = open(p, O_RDONLY);
            if (fd < 0) return fd;
            struct itx_dirent de;
            int got = readdir(fd, &de);
            close(fd);
            if (got != 1) break;
            char c[ITX_PATH_MAX];
            snprintf(c, sizeof c, "%s/%s", p, de.name);
            r = del_tree(c);
            if (r) return r;                    /* stop: the same entry would come again */
        }
    }
    r = remove(p);
    if (!r) removed++;
    return r;
}

/* the names at the top of the disk that go */
static int to_remove(char names[][ITX_NAME_MAX + 1], int max) {
    int nn = 0;
    int fd = open("/", O_RDONLY);
    if (fd < 0) return fd;
    struct itx_dirent de;
    while (readdir(fd, &de) == 1 && nn < max) {
        if (kept_name(de.name)) continue;
        /* another disk mounted here (fs.mount) is not the system disk */
        char p[ITX_PATH_MAX];
        struct itx_fsinfo fi;
        snprintf(p, sizeof p, "/%s", de.name);
        if (fsinfo(p, &fi) == 0 && strcmp(fi.mount, "/")) continue;
        strlcpy(names[nn++], de.name, sizeof names[0]);
    }
    close(fd);
    return nn;
}

int main(int argc, char **argv) {
    if (argc > 1) {
        printf("formater wipes the system disk except /kernel, /boot, /media and /ram.\n"
               "usage: formater\n");
        return argc == 2 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) ? 0 : 2;
    }
    if (recovery_start()) {
        printf(YELLOW "formater: this is the temporary terminal of the recovery screen; the system\n"
               "disk is not the root here. Nothing was changed." RESET "\n");
        return 1;
    }
    static char names[128][ITX_NAME_MAX + 1];
    int nn = to_remove(names, 128);
    if (nn < 0) { printf(RED "formater: / cannot be read: %s" RESET "\n", strerror(nn)); return 1; }
    bool backup = backup_present();

    printf(RED "formater: this wipes the system disk." RESET "\n\n");
    printf("Removed, with everything inside:\n");
    for (int i = 0; i < nn; i++) printf("  /%s\n", names[i]);
    if (!nn) printf("  (nothing: only the kept folders are left)\n");
    printf("Kept: /kernel  /boot  /media (USB sticks)  /ram (memory)\n\n");
    printf(YELLOW "Without /system the system cannot start: the next start shows the red\n"
           "recovery screen, where \"Use backup\" puts the backup system back." RESET "\n");
    if (backup) printf("The backup system is on the boot partition (\\recovery\\recovery.img).\n\n");
    else printf(RED "The backup system was NOT found on the boot partition: after this, only a\n"
                "new installation (the ODE installer) brings itelxOS back." RESET "\n\n");
    if (!nn) return 0;
    if (!yes("Wipe the system disk?")) { printf("Nothing was changed.\n"); return 1; }

    chdir("/");                                  /* this program's folder must not hold its folder */
    for (int i = 0; i < nn; i++) {
        char p[ITX_PATH_MAX];
        snprintf(p, sizeof p, "/%s", names[i]);
        int before = removed;
        int r = del_tree(p);
        if (r) {
            failed++;
            printf(YELLOW "  /%s: %s%s" RESET "\n", names[i], strerror(r),
                   r == -EBUSY ? " (a program is in it: go to / in the terminal and run formater again)" : "");
        } else {
            printf("  /%s removed (%d entries)\n", names[i], removed - before);
        }
    }
    /* more than 128 names at the top: the rest, as long as some go */
    for (int pass = 0; pass < 64 && !failed; pass++) {
        int more = to_remove(names, 128);
        if (more <= 0) break;
        int before = removed;
        for (int i = 0; i < more; i++) {
            char p[ITX_PATH_MAX];
            snprintf(p, sizeof p, "/%s", names[i]);
            if (del_tree(p)) failed++;
        }
        if (removed == before) break;
    }
    sync();
    printf("\n");
    if (failed) printf(YELLOW "formater: done, %d folder(s) could not be removed completely (above)." RESET "\n", failed);
    else printf(GREEN "formater: done, %d entries removed. /kernel, /boot, /media and /ram are as they were." RESET "\n", removed);
    if (yes("Restart now?")) {
        sync();
        reboot();
    }
    printf("The programs that run now stay in memory until the computer restarts.\n");
    return failed ? 1 : 0;
}
