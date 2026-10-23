/* recustomize: puts itelx back as it was right after it was installed.
 *
 * Every customizer app installs it as /system/recustomize.xvr, and it is
 * a file of its own in the release (recustomize.xvr): type recustomize in
 * a terminal, choose "Restore the system" in Noter, School mode or Work
 * mode, or copy it to a USB stick as update.xvr.
 *
 * Inside it are the files of itelx 1.4 as the ODE installer writes them
 * (tools/mkfactory.py). It
 *   1. puts back every system file that is not the original (the terminal,
 *      init, the XVC interpreter, the programs in /dex, ...)
 *   2. puts back the original kernel when a customizer replaced it; that
 *      needs an update the user accepted, so then it goes by a USB stick
 *      as the customizers do (cz.h)
 *   3. deletes everything else on the system disk: the customizer's apps
 *      and every file the user made, before or after the customizer
 *      (documents, journals, databases, the vault, installed apps)
 * What the installer wrote for this computer stays: /boot/cmdline (where
 * the system disk is), /system/hostname and /system/owner. Nothing is done
 * before the user types YES.
 */
#include "cz.h"

static const char *const keep[] = { "/boot/cmdline", "/system/hostname", "/system/owner" };

static char self[ITX_PATH_MAX];
static char mpoints[16][64];
static int nmpoints;

struct plist { char **v; int n, cap; uint64_t bytes; };

static void padd(struct plist *l, const char *p, uint64_t size) {
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 256;
        char **nv = realloc(l->v, sizeof(char *) * l->cap);
        if (!nv) { printf(CZ_RED "out of memory" CZ_RESET "\n"); exit(1); }
        l->v = nv;
    }
    l->v[l->n++] = strdup(p);
    l->bytes += size;
}

static bool is_factory_file(const char *p) {
    const struct cz_entry *e = cz_find(p);
    return e && (e->targets & T_ITFS);
}

static bool is_factory_dir(const char *p) {
    if (!strcmp(p, "/")) return true;
    const struct cz_entry *e = cz_entries();
    size_t l = strlen(p);
    for (uint32_t i = 0; i < cz_head()->count; i++) {
        if ((e[i].targets & T_DIR) && !strcmp(e[i].path, p)) return true;
        if ((e[i].targets & T_ITFS) && !strncmp(e[i].path, p, l) && e[i].path[l] == '/') return true;
    }
    return false;
}

static bool is_kept(const char *p) {
    for (size_t i = 0; i < sizeof keep / sizeof keep[0]; i++) if (!strcmp(keep[i], p)) return true;
    return false;
}

static bool is_mount_point(const char *p) {
    for (int i = 0; i < nmpoints; i++) if (!strcmp(mpoints[i], p)) return true;
    return false;
}

static void read_mounts(void) {
    char buf[2048];
    long n = mounts(buf, sizeof buf - 1);
    if (n <= 0) return;
    buf[n] = 0;
    for (char *l = buf; *l && nmpoints < 16;) {
        char *end = strchr(l, '\n');
        if (end) *end = 0;
        char *sp = strchr(l, ' ');
        if (sp) *sp = 0;
        if (strcmp(l, "/")) strlcpy(mpoints[nmpoints++], l, sizeof mpoints[0]);
        if (!end) break;
        l = end + 1;
    }
}

/* what is on the system disk that the original does not have */
static void scan(const char *dir, struct plist *files, struct plist *dirs) {
    int fd = open(dir, O_RDONLY);
    if (fd < 0) return;
    struct itx_dirent de;
    struct plist sub = { 0 };
    while (readdir(fd, &de) == 1) {
        char p[ITX_PATH_MAX];
        snprintf(p, sizeof p, "%s/%s", strcmp(dir, "/") ? dir : "", de.name);
        if (de.type == VT_DIR) {
            if (!is_mount_point(p)) padd(&sub, p, 0);
        } else if (!is_factory_file(p) && !is_kept(p)) {
            padd(files, p, de.size);
        }
    }
    close(fd);
    for (int i = 0; i < sub.n; i++) {
        scan(sub.v[i], files, dirs);
        if (!is_factory_dir(sub.v[i])) padd(dirs, sub.v[i], 0);   /* after what is inside it */
        free(sub.v[i]);
    }
    free(sub.v);
}

/* the top folder of a path: /home/noter/x -> /home */
static void top_of(const char *p, char *out, size_t len) {
    const char *s = strchr(p + 1, '/');
    size_t n = s ? (size_t)(s - p) : strlen(p);
    if (n >= len) n = len - 1;
    memcpy(out, p, n);
    out[n] = 0;
    if (!s) strlcpy(out, "/", len);
}

static void summary(const struct plist *files) {
    struct { char top[64]; int n; uint64_t bytes; } t[24];
    int nt = 0;
    for (int i = 0; i < files->n; i++) {
        char top[64];
        top_of(files->v[i], top, sizeof top);
        struct itx_stat st;
        uint64_t size = stat(files->v[i], &st) == 0 ? st.size : 0;
        int k = 0;
        while (k < nt && strcmp(t[k].top, top)) k++;
        if (k == nt) {
            if (nt == 24) continue;
            strlcpy(t[nt].top, top, sizeof t[nt].top);
            t[nt].n = 0;
            t[nt].bytes = 0;
            nt++;
        }
        t[k].n++;
        t[k].bytes += size;
    }
    for (int k = 0; k < nt; k++)
        printf("    %-20s %5d file(s)  %lu KiB\n", t[k].top, t[k].n, (unsigned long)((t[k].bytes + 1023) / 1024));
}

static bool boot_differs(const char *boot) {
    const struct cz_entry *e = cz_entries();
    for (uint32_t i = 0; i < cz_head()->count; i++) {
        if (!(e[i].targets & T_BOOT)) continue;
        char p[ITX_PATH_MAX];
        uint32_t c;
        uint64_t sz;
        cz_join(p, sizeof p, boot, e[i].path);
        if (!cz_file_crc(p, &c, &sz) || c != e[i].crc || sz != e[i].size) return true;
    }
    return false;
}

int main(int argc, char **argv) {
    cz_crc_init();
    bool as_update = argc > 1 && !strcmp(argv[0], "update");
    const char *stick = as_update ? argv[1] : NULL;
    if (as_update) snprintf(self, sizeof self, "%s/update.xvr", stick);
    else if (argv[0][0] == '/') strlcpy(self, argv[0], sizeof self);
    else {
        char cwd[ITX_PATH_MAX];
        getcwd(cwd, sizeof cwd);
        snprintf(self, sizeof self, "%s/%s", strcmp(cwd, "/") ? cwd : "", argv[0]);
    }
    char why[200];
    printf("\n" CZ_CYAN "recustomize" CZ_RESET " - puts itelx back as it was right after it was installed\n\n");
    if (!cz_verify(why, sizeof why)) {
        printf(CZ_RED "recustomize is damaged: %s; nothing was changed" CZ_RESET "\n", why);
        return 1;
    }
    if (!cz_system_ok()) return 1;

    struct cz_state st;
    if (cz_state_read(&st)) printf("itelx is now: " CZ_YELLOW "%s %s" CZ_RESET " (installed %s)\n", st.title, st.version, st.installed);
    else printf("itelx is not customized now; the original files are put back all the same.\n");

    char boot[ITX_PATH_MAX];
    bool have_boot = cz_find_boot(boot, sizeof boot);
    bool kernel = have_boot && boot_differs(boot);
    bool writable = have_boot && cz_boot_writable(boot);
    if (kernel && !writable) {
        printf("\nThe kernel is not the original one. itelx 1.4 lets only an update you accept\n"
               "from a USB stick write the kernel, so recustomize goes by a stick.\n\n");
        if (as_update) {
            printf(CZ_RED "this update may not write the boot partition; nothing was changed" CZ_RESET "\n");
            return 1;
        }
        return cz_to_stick(self, "recustomize") ? 0 : 1;
    }

    /* the plan */
    read_mounts();
    struct plist files = { 0 }, dirs = { 0 }, restore = { 0 };
    scan("/", &files, &dirs);
    const struct cz_head *h = cz_head();
    const struct cz_entry *e = cz_entries();
    for (uint32_t i = 0; i < h->count; i++) {
        if (!(e[i].targets & T_ITFS)) continue;
        uint32_t c;
        uint64_t sz;
        if (!cz_file_crc(e[i].path, &c, &sz) || c != e[i].crc || sz != e[i].size) padd(&restore, e[i].path, e[i].size);
    }
    printf("\nWhat it does:\n");
    printf("  puts back %d system file(s) that are not the original ones\n", restore.n);
    printf("  the kernel: %s\n", kernel ? "the original one is put back" : "already the original one");
    if (files.n) {
        printf("  " CZ_RED "deletes for good" CZ_RESET " %d file(s) and %d folder(s) the original system does not have:\n", files.n, dirs.n);
        summary(&files);
    } else {
        printf("  nothing to delete: there is no file the original system does not have\n");
    }
    printf("  stays: /boot/cmdline, /system/hostname, /system/owner (written for this computer)\n\n");
    if (!restore.n && !kernel && !files.n && !dirs.n) {
        printf(CZ_GREEN "itelx is already as it was installed." CZ_RESET "\n");
        if (stick) cz_clean_stick(stick);
        return 0;
    }
    if (!cz_ask_word("Type " CZ_YELLOW "YES" CZ_RESET " to put itelx back as it was installed:", "YES")) {
        printf("nothing was changed\n");
        return 0;
    }

    /* 1. the system files: the terminal comes back first */
    int bad = 0;
    for (uint32_t i = 0; i < h->count; i++) {
        if (e[i].targets & T_DIR) {
            struct itx_stat s;
            if (stat(e[i].path, &s)) mkdir(e[i].path);
            continue;
        }
        if (!(e[i].targets & T_ITFS)) continue;
        int r = cz_install(e[i].path, cz_data(&e[i]), e[i].size, e[i].crc, why, sizeof why);
        if (r == F_DONE) printf("  put back %s\n", e[i].path);
        else if (r != F_SAME) { printf(CZ_RED "  %s: %s" CZ_RESET "\n", e[i].path, r == F_ROFS ? "read only" : why); bad++; }
    }
    if (bad) {
        printf(CZ_RED "stopped before anything was deleted; run recustomize again." CZ_RESET "\n");
        return 1;
    }

    /* 2. the kernel */
    if (kernel) {
        for (uint32_t i = 0; i < h->count; i++) {
            if (!(e[i].targets & T_BOOT)) continue;
            char p[ITX_PATH_MAX];
            cz_join(p, sizeof p, boot, e[i].path);
            int r = cz_install(p, cz_data(&e[i]), e[i].size, e[i].crc, why, sizeof why);
            if (r == F_DONE) printf("  put back %s\n", p);
            else if (r != F_SAME) {
                printf(CZ_RED "  %s: %s" CZ_RESET "\n", p, r == F_ROFS ? "read only" : why);
                printf(CZ_RED "the kernel could not be put back; nothing was deleted. Run recustomize again." CZ_RESET "\n");
                return 1;
            }
        }
    }
    if (writable) {
        static const char *const left[] = { "vmlinuz.old", "vmlinuz.new", "initrd.img.old", "initrd.img.new" };
        for (size_t i = 0; i < sizeof left / sizeof left[0]; i++) {
            char p[ITX_PATH_MAX];
            snprintf(p, sizeof p, "%s/boot/%s", boot, left[i]);
            remove(p);
        }
    }

    /* 3. everything else; programs still running (a server in the
       background, say) are stopped first: they hold files open */
    int stopped = cz_stop_others();
    if (stopped) printf("  stopped %d program(s) that were still running\n", stopped);
    int gone = 0, kept = 0;
    for (int i = 0; i < files.n; i++) {
        if (!strcmp(files.v[i], CZ_STATE) || !strcmp(files.v[i], "/system/recustomize.xvr")) continue;   /* last */
        if (remove(files.v[i]) == 0) gone++;
        else kept++;
    }
    bool busy = false;
    for (int i = 0; i < dirs.n; i++) {
        int r = remove(dirs.v[i]);
        if (r == -EBUSY) busy = true;
        else if (r) kept++;
    }
    if (busy && as_update) {
        /* a folder is some program's working folder: the terminal's (a
           customizer's terminal starts in its own folder). As an update this
           program has the terminal to itself: the terminal starts again (as
           the original one now, in /) and the folders can go. */
        cz_restart_terminal();
        sleep_ms(1000);
        busy = false;
    }
    for (int i = 0; i < dirs.n; i++) {
        struct itx_stat st;
        if (stat(dirs.v[i], &st)) continue;
        int r = remove(dirs.v[i]);
        if (r == -EBUSY) { busy = true; printf(CZ_YELLOW "  %s is in use (a program works in it)" CZ_RESET "\n", dirs.v[i]); }
        else if (r) kept++;
    }
    if (!busy) {
        /* last: until here, recustomize can run again */
        if (remove("/system/recustomize.xvr") == 0) gone++;
        if (remove(CZ_STATE) == 0) gone++;
    }
    sync();
    if (stick) cz_clean_stick(stick);
    printf(CZ_GREEN "itelx is as it was installed" CZ_RESET " (%d file(s) put back, %d deleted%s).\n", restore.n, gone,
           kept || busy ? "; some could not be deleted" : "");
    if (busy) printf("A folder in use stays: go out of it (cd= /) and run recustomize again to delete it.\n");
    if (cz_ask_yes("Restart now (needed to start the original terminal and kernel)?")) { sync(); reboot(); }
    cz_restart_terminal();
    return 0;
}
