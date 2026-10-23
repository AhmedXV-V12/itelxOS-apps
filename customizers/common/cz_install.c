/* The installer of a customizer app: noter.xvr, school_mode.xvr,
 * work_mode.xvr, file_saver.xvr and data_place.xvr are all this program,
 * each with its own payload (cz.h).
 *
 *   run it from the terminal:    noter          (after install noter.xvr)
 *                                /media/STICK/noter.xvr
 *   or from a USB stick:         the file copied to the stick as update.xvr;
 *                                itelx asks "are you need update ?" -> yes
 *
 * What it does, in this order, after the user's yes:
 *   1. checks every file inside it (CRC-32) and that this is itelx 1.4
 *   2. refuses when another customizer is installed (recustomize first)
 *   3. a customizer with a kernel: when this process may not write the
 *      boot partition (it was not started as an accepted update), it puts
 *      itself on a USB stick as update.xvr and stops there
 *   4. the files for the system disk, each written next to the old one
 *      (NAME.new), read back, checked, then put in its place
 *   5. the kernel on the boot partition, last
 *   6. /system/customizer.txt: what is installed (recustomize reads it)
 *   7. restarts the terminal (now the customizer's program), or the
 *      computer when the kernel changed
 * Running it again on the same customizer repairs it: files that are
 * already right are left alone.
 */
#include "cz.h"

static char self[ITX_PATH_MAX];

static const char *kib(uint64_t bytes, char *out, size_t len) {
    snprintf(out, len, "%lu KiB", (unsigned long)((bytes + 1023) / 1024));
    return out;
}

static void print_text(const char *path) {
    const struct cz_entry *e = cz_find(path);
    if (!e) return;
    flush();
    write(1, cz_data(e), e->size);
}

static bool boot_same(const char *boot) {
    const struct cz_entry *e = cz_entries();
    for (uint32_t i = 0; i < cz_head()->count; i++) {
        if (!(e[i].targets & T_BOOT)) continue;
        char p[ITX_PATH_MAX];
        uint32_t c;
        uint64_t sz;
        cz_join(p, sizeof p, boot, e[i].path);
        if (!cz_file_crc(p, &c, &sz) || c != e[i].crc || sz != e[i].size) return false;
    }
    return true;
}

static void write_state(bool kernel) {
    const struct cz_head *h = cz_head();
    const struct cz_entry *e = cz_entries();
    size_t cap = 4096 + (size_t)h->count * 120, o = 0;
    char *t = malloc(cap), when[24];
    if (!t) return;
    cz_today(when, sizeof when);
    o += snprintf(t + o, cap - o, "# what itelx is customized as; recustomize puts itelx back as installed\n"
                  "id=%s\ntitle=%s\nversion=%u.%u\ninstalled=%s\nkernel=%s\n", h->id, h->title,
                  h->version >> 8, h->version & 255, when, kernel ? "yes" : "no");
    for (uint32_t i = 0; i < h->count && o < cap - 120; i++)
        if (e[i].targets & (T_ITFS | T_DIR)) o += snprintf(t + o, cap - o, "%s=%s\n", e[i].targets & T_DIR ? "dir" : "file", e[i].path);
    write_file(CZ_STATE, t, o, false);
    free(t);
    sync();
}

int main(int argc, char **argv) {
    cz_crc_init();
    /* started by the update manager: argv is "update", the stick */
    bool as_update = argc > 1 && !strcmp(argv[0], "update");
    const char *stick = as_update ? argv[1] : NULL;
    if (as_update) snprintf(self, sizeof self, "%s/update.xvr", stick);
    else if (argv[0][0] == '/') strlcpy(self, argv[0], sizeof self);
    else {
        char cwd[ITX_PATH_MAX];
        getcwd(cwd, sizeof cwd);
        snprintf(self, sizeof self, "%s/%s", strcmp(cwd, "/") ? cwd : "", argv[0]);
    }

    char why[200], sz[24];
    if (!cz_verify(why, sizeof why)) {
        printf(CZ_RED "this customizer app is damaged: %s; nothing was changed" CZ_RESET "\n", why);
        return 1;
    }
    const struct cz_head *h = cz_head();
    const struct cz_entry *e = cz_entries();
    printf("\n" CZ_CYAN "%s %u.%u" CZ_RESET " - an itelx customizer app\n\n", h->title, h->version >> 8, h->version & 255);
    print_text("/about.txt");
    printf("\n");
    if (!cz_system_ok()) return 1;

    struct cz_state st;
    if (cz_state_read(&st)) {
        if (strcmp(st.id, h->id)) {
            printf(CZ_YELLOW "itelx is already customized as %s %s (since %s)." CZ_RESET "\n"
                   "Run " CZ_YELLOW "recustomize" CZ_RESET " first: it puts itelx back as it was installed.\n",
                   st.title, st.version, st.installed);
            return 1;
        }
        printf("%s is installed already (since %s): installing again repairs it.\n", st.title, st.installed);
    }

    uint64_t itfs_bytes = 0, boot_bytes = 0;
    int nfiles = 0;
    bool kernel = false, terminal = false;
    for (uint32_t i = 0; i < h->count; i++) {
        if (e[i].targets & T_ITFS) { itfs_bytes += e[i].size; nfiles++; }
        if (e[i].targets & T_BOOT) { boot_bytes += e[i].size; kernel = true; }
        if ((e[i].targets & T_ITFS) && !strcmp(e[i].path, "/system/terminal.xvr")) terminal = true;
    }
    struct itx_fsinfo fi;
    if (fsinfo("/", &fi) == 0 && fi.free_bytes != ~0ULL && fi.free_bytes < itfs_bytes * 2 + (1 << 20)) {
        printf(CZ_RED "the system disk has too little free space (%lu KiB, %lu KiB needed); nothing was changed" CZ_RESET "\n",
               (unsigned long)(fi.free_bytes / 1024), (unsigned long)((itfs_bytes * 2 + (1 << 20)) / 1024));
        return 1;
    }
    char boot[ITX_PATH_MAX] = "";
    bool kernel_todo = false;
    if (kernel) {
        if (!cz_find_boot(boot, sizeof boot)) {
            printf(CZ_RED "the system's boot partition was not found in /media; nothing was changed" CZ_RESET "\n");
            return 1;
        }
        kernel_todo = !boot_same(boot);
        if (kernel_todo && !cz_boot_writable(boot)) {
            printf("%s brings its own itelx kernel. itelx 1.4 lets only an update you accept\n"
                   "from a USB stick write the kernel, so it installs from a stick.\n\n", h->title);
            if (as_update) {
                printf(CZ_RED "this update may not write the boot partition; nothing was changed" CZ_RESET "\n");
                return 1;
            }
            return cz_to_stick(self, h->title) ? 0 : 1;
        }
    }

    printf("What it does:\n");
    printf("  %d files on the system disk (%s)\n", nfiles, kib(itfs_bytes, sz, sizeof sz));
    if (terminal) printf("  its own program instead of the terminal (/system/terminal.xvr)\n");
    if (kernel) printf("  the itelx kernel with more drivers on the boot partition (%s)%s\n", kib(boot_bytes, sz, sizeof sz),
                       kernel_todo ? "" : ", already there");
    printf("  recustomize (/system/recustomize.xvr) puts itelx back as it was installed\n\n");
    if (!cz_ask_yes("Install it?")) {
        printf("nothing was changed\n");
        return 0;
    }

    /* 1. the system disk */
    int changed = 0, same = 0;
    for (uint32_t i = 0; i < h->count; i++) {
        if (e[i].targets & T_DIR) {
            struct itx_stat s;
            char p[ITX_PATH_MAX];
            snprintf(p, sizeof p, "%s/x", e[i].path);
            cz_make_dirs(p);
            if (stat(e[i].path, &s)) mkdir(e[i].path);
            continue;
        }
        if (!(e[i].targets & T_ITFS)) continue;
        int r = cz_install(e[i].path, cz_data(&e[i]), e[i].size, e[i].crc, why, sizeof why);
        if (r == F_SAME) { same++; continue; }
        if (r != F_DONE) {
            printf(CZ_RED "  %s: %s" CZ_RESET "\n", e[i].path, r == F_ROFS ? "read only" : why);
            printf(CZ_RED "stopped. Files already written stay; run it again, or recustomize." CZ_RESET "\n");
            return 1;
        }
        changed++;
        printf("  %-34s %s\n", e[i].path, kib(e[i].size, sz, sizeof sz));
    }

    /* 2. the kernel, last */
    bool kernel_new = false;
    for (uint32_t i = 0; i < h->count && kernel_todo; i++) {
        if (!(e[i].targets & T_BOOT)) continue;
        char p[ITX_PATH_MAX];
        cz_join(p, sizeof p, boot, e[i].path);
        int r = cz_install(p, cz_data(&e[i]), e[i].size, e[i].crc, why, sizeof why);
        if (r == F_SAME) { same++; continue; }
        if (r != F_DONE) {
            printf(CZ_RED "  %s: %s" CZ_RESET "\n", p, r == F_ROFS ? "read only" : why);
            printf(CZ_RED "the kernel could not be written; the old one stays and still starts. Run it again." CZ_RESET "\n");
            return 1;
        }
        kernel_new = true;
        changed++;
        printf("  %-34s %s\n", p, kib(e[i].size, sz, sizeof sz));
    }

    /* 3. what is installed */
    write_state(kernel);
    if (stick) cz_clean_stick(stick);
    printf(CZ_GREEN "%s is installed" CZ_RESET " (%d file(s) written, %d already right).\n", h->title, changed, same);
    print_text("/after.txt");
    if (kernel_new) {
        printf("The new kernel starts with the next start.\n");
        if (cz_ask_yes("Restart now?")) { sync(); reboot(); }
        return 0;
    }
    if (terminal && cz_ask_yes("Start it now?")) {
        sync();
        flush();
        cz_restart_terminal();
    }
    return 0;
}
