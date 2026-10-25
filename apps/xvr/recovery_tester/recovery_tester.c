/* recovery_tester: tests the red recovery screen of itelxOS 2.0 with a
 * fault made on purpose, and puts the system back as it was.
 *
 *   recovery_tester           start a test, or check the one under way
 *   recovery_tester cancel    undo the fault now (also in the recovery
 *                             screen's temporary terminal, after disk.open)
 *
 * 1. It asks y/n, keeps the good copy of /system/xvinit.xvr (the service
 *    manager) in /users/recovery_tester - /users is what "Use backup"
 *    keeps, it replaces /kernel, /system and /boot - and damages the file
 *    on the disk: 64 bytes in its middle are inverted, so its checksum no
 *    longer matches and the kernel refuses to start it.
 * 2. At the next start the system cannot run: it marks itself failed and
 *    restarts, and the boot manager shows the red recovery screen. The
 *    user repairs it there (Use backup, or Use manual).
 * 3. Started again on the repaired system, recovery_tester checks the
 *    repaired file. When the repair worked it puts the good copy back over
 *    the repaired file at once. When it did not, it writes why, with the
 *    logs, to report-ar.txt and report-en.txt in /users/recovery_tester.
 *
 * itelx has no user accounts: every program runs with the owner's (root's)
 * rights. The XVR asks for itelx 2.0 (min_os). */
#include <itx.h>

#define RED "\x1b[91m"
#define GREEN "\x1b[92m"
#define YELLOW "\x1b[93m"
#define CYAN "\x1b[96m"
#define RESET "\x1b[0m"

/* itelx 2.0 (abi/itx_abi.h of itelxOS 2.0; the SDK here is 1.4) */
#define SYS_BOOTSTAT 53
#define SYS_CMDLINE  70
#define BST_OP_READ  0
struct itx_bootstat {
    char magic[8];
    uint32_t state;           /* 0 completed, 1 booting, 2 failed, 3 recovery asked for */
    uint32_t tries;
    uint64_t last_ok;
    uint64_t changed;
    char reason[160];
    char kernel[32];
    uint32_t mode;            /* 0 normal, 1 recovery backup, 2 recovery manual */
    uint32_t pad;
};

#define TARGET   "/system/xvinit.xvr"
#define DIR      "/users/recovery_tester"
#define GOOD     "xvinit.xvr.good"
#define STATE    "state.txt"
#define DAMAGE   64

static char root[24];         /* "", "/media/broken-os" or "/old": the system disk in a recovery start */

static void path(char *out, size_t len, const char *name) {
    if (name[0] == '/') snprintf(out, len, "%s%s", root, name);
    else snprintf(out, len, "%s" DIR "/%s", root, name);
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

static uint32_t crc32(const void *data, size_t len) {
    const uint8_t *p = data;
    uint32_t c = 0xFFFFFFFFu;
    while (len--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & -(c & 1));
    }
    return ~c;
}

static void date(long t, char *out, size_t len) {
    if (t <= 0) { snprintf(out, len, "-"); return; }
    long days = t / 86400, sec = t % 86400;
    long z = days + 719468, era = z / 146097, doe = z - era * 146097, yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long y = yoe + era * 400, doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
    long d = doy - (153 * mp + 2) / 5 + 1, m = mp < 10 ? mp + 3 : mp - 9;
    if (m <= 2) y++;
    snprintf(out, len, "%04ld-%02ld-%02ld %02ld:%02ld:%02ld", y, m, d, sec / 3600, sec / 60 % 60, sec % 60);
}

static bool recovery_start(void) {
    char cl[512];
    long n = itx_syscall(SYS_CMDLINE, (long)cl, sizeof cl - 1, 0, 0, 0, 0);
    if (n <= 0) return false;
    cl[n < (long)sizeof cl ? n : (long)sizeof cl - 1] = 0;
    return strstr(cl, "itelx.recovery") != NULL;
}

/* the system's boot partition in /media ("(boot files)" in the disk list) */
static bool boot_mount(char *out, size_t len) {
    struct itx_diskinfo dk[32];
    int n = disklist(dk, 32);
    for (int i = 0; i < n; i++) {
        char *b = strstr(dk[i].use, " (boot files)");
        if (!b) continue;
        snprintf(out, len, "%.*s", (int)(b - dk[i].use), dk[i].use);
        return true;
    }
    return false;
}

/* ------------------------------------------------------------ the test state */

struct state {
    char target[ITX_PATH_MAX];
    unsigned long size, offset;
    uint32_t crc;
    long started;
    char kernel[32];
};

static bool state_read(struct state *s) {
    char p[ITX_PATH_MAX];
    path(p, sizeof p, STATE);
    size_t len;
    char *t = read_file(p, &len, 4096);
    if (!t) return false;
    memset(s, 0, sizeof *s);
    for (char *l = t; l && *l;) {
        char *e = strchr(l, '\n');
        if (e) *e = 0;
        char *v = strchr(l, '=');
        if (v) {
            *v++ = 0;
            if (!strcmp(l, "target")) strlcpy(s->target, v, sizeof s->target);
            else if (!strcmp(l, "size")) s->size = strtoul(v, NULL, 10);
            else if (!strcmp(l, "offset")) s->offset = strtoul(v, NULL, 10);
            else if (!strcmp(l, "crc")) s->crc = (uint32_t)strtoul(v, NULL, 16);
            else if (!strcmp(l, "started")) s->started = atol(v);
            else if (!strcmp(l, "kernel")) strlcpy(s->kernel, v, sizeof s->kernel);
        }
        l = e ? e + 1 : NULL;
    }
    free(t);
    return s->target[0] && s->size;
}

static int state_write(const struct state *s) {
    char p[ITX_PATH_MAX], t[512];
    path(p, sizeof p, STATE);
    int n = snprintf(t, sizeof t, "target=%s\nsize=%lu\ncrc=%08x\noffset=%lu\nstarted=%ld\nkernel=%s\n",
                     s->target, s->size, s->crc, s->offset, s->started, s->kernel);
    return write_file(p, t, (size_t)n, false);
}

/* the folder with its files (the report files go only with "cancel") */
static void state_clear(bool reports_too) {
    char p[ITX_PATH_MAX];
    const char *names[] = { STATE, GOOD, GOOD ".new", "report-ar.txt", "report-en.txt" };
    for (int i = 0; i < 5; i++) {
        if (i >= 3 && !reports_too) break;
        path(p, sizeof p, names[i]);
        remove(p);
    }
    char t[ITX_PATH_MAX];
    path(t, sizeof t, TARGET ".new");
    remove(t);
    path(p, sizeof p, DIR);
    remove(p);                                   /* only when it is empty */
}

/* the good copy over the file on the disk; 0 when it is there and checked */
static int put_back(const struct state *s, char *why, size_t whylen) {
    char good[ITX_PATH_MAX], tgt[ITX_PATH_MAX], tmp[ITX_PATH_MAX];
    path(good, sizeof good, GOOD);
    path(tgt, sizeof tgt, s->target);
    snprintf(tmp, sizeof tmp, "%s.new", tgt);
    size_t len;
    char *d = read_file(good, &len, 64u << 20);
    if (!d) { snprintf(why, whylen, "the good copy %s cannot be read", good); return -ENOENT; }
    if (len != s->size || crc32(d, len) != s->crc) {
        free(d);
        snprintf(why, whylen, "the good copy %s is damaged (size or CRC-32 is not the one kept)", good);
        return -ECORRUPT;
    }
    int r = write_file(tmp, d, len, false);
    free(d);
    if (r) { snprintf(why, whylen, "%s cannot be written: %s", tmp, strerror(r)); remove(tmp); return r; }
    remove(tgt);
    r = rename(tmp, tgt);
    if (r) { snprintf(why, whylen, "%s cannot take the place of %s: %s", tmp, tgt, strerror(r)); return r; }
    sync();
    d = read_file(tgt, &len, 64u << 20);
    bool same = d && len == s->size && crc32(d, len) == s->crc;
    free(d);
    if (!same) { snprintf(why, whylen, "%s does not read back as the good copy", tgt); return -EIO; }
    return 0;
}

/* --------------------------------------------------------------- start */

static int start_test(void) {
    struct itx_sysinfo si;
    sysinfo(&si);
    if (strncmp(si.version, "itelx 2", 7)) {
        printf(RED "recovery_tester: needs itelx 2.0 or newer (this is %s)" RESET "\n", si.version);
        return 1;
    }
    struct itx_bootstat bs;
    char mnt[64], img[96];
    struct itx_stat st;
    if (itx_syscall(SYS_BOOTSTAT, BST_OP_READ, (long)&bs, 0, 0, 0, 0) || !boot_mount(mnt, sizeof mnt)) {
        printf(RED "recovery_tester: the boot partition and its boot status were not found; the red\n"
               "screen could not come up. Nothing was changed." RESET "\n");
        return 1;
    }
    snprintf(img, sizeof img, "%s/recovery/recovery.img", mnt);
    if (stat(img, &st) || !st.size) {
        printf(RED "recovery_tester: the backup system (%s) is missing; \"Use backup\" could not\n"
               "repair anything. Nothing was changed." RESET "\n", img);
        return 1;
    }
    char why[96];
    if (xvr_check(TARGET, why, sizeof why)) {
        printf(RED "recovery_tester: %s is already not a valid program (%s). Nothing was changed." RESET "\n",
               TARGET, why);
        return 1;
    }
    size_t len;
    char *d = read_file(TARGET, &len, 64u << 20);
    if (!d || len < 4096) { free(d); printf(RED "recovery_tester: %s cannot be read" RESET "\n", TARGET); return 1; }

    printf(CYAN "recovery_tester: a test of the red recovery screen" RESET "\n\n");
    printf("1. The good copy of %s (%lu bytes) goes to %s/%s\n", TARGET, (unsigned long)len, DIR, GOOD);
    printf("2. %s is damaged on purpose: the system cannot start with it\n", TARGET);
    printf("3. The next start shows the red recovery screen; repair the system there\n");
    printf("   (Use backup keeps /users and puts the backup system on the disk)\n");
    printf("4. Run recovery_tester again on the repaired system: it puts the good copy back,\n");
    printf("   or writes why the repair failed to %s/report-ar.txt and report-en.txt\n\n", DIR);
    printf(YELLOW "After Use backup the computer runs the backup system; the full itelxOS comes\n"
           "back with update.xvr. Before restarting, \"recovery_tester cancel\" undoes it." RESET "\n\n");
    if (!yes("Start the test?")) { free(d); printf("Nothing was changed.\n"); return 1; }

    struct state s;
    memset(&s, 0, sizeof s);
    strlcpy(s.target, TARGET, sizeof s.target);
    s.size = len;
    s.crc = crc32(d, len);
    s.offset = (len / 2) & ~63ul;
    s.started = unix_time();
    strlcpy(s.kernel, si.version, sizeof s.kernel);

    mkdir(DIR);
    char good[ITX_PATH_MAX];
    path(good, sizeof good, GOOD);
    int r = write_file(good, d, len, false);
    free(d);
    size_t glen;
    char *g = r ? NULL : read_file(good, &glen, 64u << 20);
    bool ok = g && glen == s.size && crc32(g, glen) == s.crc;
    free(g);
    if (!ok || state_write(&s)) {
        printf(RED "recovery_tester: the good copy could not be kept in %s (%s). Nothing was changed." RESET "\n",
               DIR, r ? strerror(r) : "it does not read back the same");
        state_clear(false);
        return 1;
    }
    sync();
    printf("  good copy kept: %s (CRC-32 %08x)\n", good, s.crc);

    /* the fault: 64 bytes inverted in the middle of the file */
    int fd = open(TARGET, O_RDWR);
    uint8_t buf[DAMAGE];
    r = fd < 0 ? fd : 0;
    if (!r && (lseek(fd, (long)s.offset, SEEK_SET) != (long)s.offset || read(fd, buf, DAMAGE) != DAMAGE)) r = -EIO;
    if (!r) {
        for (int i = 0; i < DAMAGE; i++) buf[i] ^= 0xFF;
        if (lseek(fd, (long)s.offset, SEEK_SET) != (long)s.offset || write(fd, buf, DAMAGE) != DAMAGE) r = -EIO;
    }
    if (fd >= 0) close(fd);
    sync();
    if (r || !xvr_check(TARGET, why, sizeof why)) {
        printf(RED "recovery_tester: %s could not be damaged (%s); it is put back." RESET "\n", TARGET,
               r ? strerror(r) : "it still checks as valid");
        put_back(&s, why, sizeof why);
        state_clear(false);
        return 1;
    }
    printf("  %s damaged at byte %lu: %s\n\n", TARGET, s.offset, why);
    printf(GREEN "The fault is in place." RESET " At the next start the red recovery screen comes up.\n");
    if (yes("Restart now?")) {
        sync();
        reboot();
    }
    printf("Restart when you are ready, or undo it with: recovery_tester cancel\n");
    return 0;
}

/* --------------------------------------------------------------- check */

struct facts {
    bool target_there, target_valid, target_original, good_ok, backup_system;
    char target_why[96];
    unsigned long target_size;
    uint32_t target_crc;
    struct itx_bootstat bs;
    int bs_err;
    char recovered[256];
};

static void gather(const struct state *s, struct facts *f) {
    memset(f, 0, sizeof *f);
    char p[ITX_PATH_MAX];
    size_t len;
    path(p, sizeof p, s->target);
    char *d = read_file(p, &len, 64u << 20);
    if (d) {
        f->target_there = true;
        f->target_size = len;
        f->target_crc = crc32(d, len);
        f->target_original = len == s->size && f->target_crc == s->crc;
        free(d);
        f->target_valid = xvr_check(p, f->target_why, sizeof f->target_why) == 0;
        if (f->target_valid) strlcpy(f->target_why, "valid", sizeof f->target_why);
    } else {
        strlcpy(f->target_why, "missing", sizeof f->target_why);
    }
    path(p, sizeof p, GOOD);
    d = read_file(p, &len, 64u << 20);
    f->good_ok = d && len == s->size && crc32(d, len) == s->crc;
    free(d);
    struct itx_stat st;
    path(p, sizeof p, "/system/backup-system.txt");
    f->backup_system = stat(p, &st) == 0;
    f->bs_err = (int)itx_syscall(SYS_BOOTSTAT, BST_OP_READ, (long)&f->bs, 0, 0, 0, 0);
    f->bs.reason[sizeof f->bs.reason - 1] = 0;
    f->bs.kernel[sizeof f->bs.kernel - 1] = 0;
    path(p, sizeof p, "/users");
    int fd = open(p, O_RDONLY);
    struct itx_dirent de;
    while (fd >= 0 && readdir(fd, &de) == 1)
        if (!strncmp(de.name, "recovered-", 10) && strlen(f->recovered) + strlen(de.name) + 3 < sizeof f->recovered) {
            if (f->recovered[0]) strlcat(f->recovered, ", ", sizeof f->recovered);
            strlcat(f->recovered, de.name, sizeof f->recovered);
        }
    if (fd >= 0) close(fd);
}

static const char *bs_state_en(uint32_t s) {
    return s == 0 ? "the last start completed" : s == 1 ? "a start is under way" : s == 2 ? "failed" :
           s == 3 ? "recovery screen asked for" : "unknown";
}
static const char *bs_state_ar(uint32_t s) {
    return s == 0 ? "\u0622\u062e\u0631 \u062a\u0634\u063a\u064a\u0644 \u0627\u0643\u062a\u0645\u0644" :
           s == 1 ? "\u062a\u0634\u063a\u064a\u0644 \u062c\u0627\u0631\u064d" :
           s == 2 ? "\u0641\u0634\u0644" :
           s == 3 ? "\u0637\u064f\u0644\u0628\u062a \u0634\u0627\u0634\u0629 \u0627\u0644\u0627\u0633\u062a\u0631\u062f\u0627\u062f" :
                    "\u063a\u064a\u0631 \u0645\u0639\u0631\u0648\u0641";
}

/* the two report files; reasons[] in both languages */
static void reports(const struct state *s, const struct facts *f, int n, const char *const *en, const char *const *ar) {
    static char klog[8192], st[512], b[16384];
    char now[32], started[32], lastok[32], changed[32], p[ITX_PATH_MAX], sp[ITX_PATH_MAX];
    date(unix_time(), now, sizeof now);
    date(s->started, started, sizeof started);
    date((long)f->bs.last_ok, lastok, sizeof lastok);
    date((long)f->bs.changed, changed, sizeof changed);
    long kl = klog_read(klog, sizeof klog - 1);
    klog[kl > 0 ? kl : 0] = 0;
    path(sp, sizeof sp, STATE);
    size_t sl;
    char *stt = read_file(sp, &sl, sizeof st - 1);
    snprintf(st, sizeof st, "%s", stt ? stt : "-\n");
    free(stt);
    struct itx_sysinfo si;
    sysinfo(&si);

    for (int lang = 0; lang < 2; lang++) {
        int o = 0;
#define OUT(...) (o += snprintf(b + o, o < (int)sizeof b ? sizeof b - o : 0, __VA_ARGS__))
        if (!lang) {
            OUT("recovery_tester report\n\nresult: the repair FAILED\nwritten: %s\ntest started: %s\nfile: %s\n\n", now, started, s->target);
            OUT("Why:\n");
            for (int i = 0; i < n; i++) OUT("  - %s\n", en[i]);
            OUT("\nWhat was found:\n");
            OUT("  the file: %s, %lu bytes, CRC-32 %08x (%s)\n", f->target_there ? "there" : "missing", f->target_size,
                f->target_crc, f->target_why);
            OUT("  the file is the original: %s\n", f->target_original ? "yes" : "no");
            OUT("  the good copy (%s/%s): %s\n", DIR, GOOD, f->good_ok ? "intact" : "missing or damaged");
            OUT("  the backup system on the disk: %s\n", f->backup_system ? "yes (Use backup ran)" : "no");
            OUT("  folders kept by Use backup: %s\n", f->recovered[0] ? f->recovered : "none");
            OUT("  kernel now: %s (the test started on %s)\n", si.version, s->kernel);
            if (f->bs_err) OUT("  boot status: cannot be read (%s)\n", strerror(f->bs_err));
            else OUT("  boot status: %s, %u tries, last completed start %s, changed %s, kernel %s, reason \"%s\"\n",
                     bs_state_en(f->bs.state), f->bs.tries, lastok, changed, f->bs.kernel, f->bs.reason);
            OUT("\nWhat to do: \"recovery_tester cancel\" puts the good copy back by hand (also in the\n"
                "recovery screen's temporary terminal, after disk.open).\n");
            OUT("\nLogs\n---- %s\n%s", STATE, st);
            OUT("---- kernel log (the last %ld bytes)\n%s\n", kl > 0 ? kl : 0, klog);
        } else {
            OUT("\u062a\u0642\u0631\u064a\u0631 recovery_tester\n\n");
            OUT("\u0627\u0644\u0646\u062a\u064a\u062c\u0629: \u0627\u0644\u0625\u0635\u0644\u0627\u062d \u0641\u0634\u0644\n");
            OUT("\u0648\u0642\u062a \u0627\u0644\u062a\u0642\u0631\u064a\u0631: %s\n\u0628\u062f\u0627\u064a\u0629 \u0627\u0644\u0627\u062e\u062a\u0628\u0627\u0631: %s\n\u0627\u0644\u0645\u0644\u0641: %s\n\n", now, started, s->target);
            OUT("\u0627\u0644\u0623\u0633\u0628\u0627\u0628:\n");
            for (int i = 0; i < n; i++) OUT("  - %s\n", ar[i]);
            OUT("\n\u0645\u0627 \u0648\u064f\u062c\u062f:\n");
            OUT("  \u0627\u0644\u0645\u0644\u0641: %s\u060c %lu \u0628\u0627\u064a\u062a\u060c CRC-32 %08x (%s)\n",
                f->target_there ? "\u0645\u0648\u062c\u0648\u062f" : "\u0645\u0641\u0642\u0648\u062f", f->target_size, f->target_crc, f->target_why);
            OUT("  \u0627\u0644\u0645\u0644\u0641 \u0647\u0648 \u0627\u0644\u0623\u0635\u0644\u064a: %s\n", f->target_original ? "\u0646\u0639\u0645" : "\u0644\u0627");
            OUT("  \u0627\u0644\u0646\u0633\u062e\u0629 \u0627\u0644\u0633\u0644\u064a\u0645\u0629 (%s/%s): %s\n", DIR, GOOD,
                f->good_ok ? "\u0633\u0644\u064a\u0645\u0629" : "\u0645\u0641\u0642\u0648\u062f\u0629 \u0623\u0648 \u062a\u0627\u0644\u0641\u0629");
            OUT("  \u0646\u0638\u0627\u0645 \u0627\u0644\u0646\u0633\u062e\u0629 \u0627\u0644\u0627\u062d\u062a\u064a\u0627\u0637\u064a\u0629 \u0639\u0644\u0649 \u0627\u0644\u0642\u0631\u0635: %s\n",
                f->backup_system ? "\u0646\u0639\u0645 (\u0627\u0634\u062a\u063a\u0644 Use backup)" : "\u0644\u0627");
            OUT("  \u0645\u062c\u0644\u062f\u0627\u062a \u062d\u0641\u0638\u0647\u0627 Use backup: %s\n", f->recovered[0] ? f->recovered : "\u0644\u0627 \u064a\u0648\u062c\u062f");
            OUT("  \u0627\u0644\u0646\u0648\u0627\u0629 \u0627\u0644\u0622\u0646: %s (\u0628\u062f\u0623 \u0627\u0644\u0627\u062e\u062a\u0628\u0627\u0631 \u0639\u0644\u0649 %s)\n", si.version, s->kernel);
            if (f->bs_err) OUT("  \u062d\u0627\u0644\u0629 \u0627\u0644\u062a\u0634\u063a\u064a\u0644: \u0644\u0627 \u062a\u064f\u0642\u0631\u0623 (%s)\n", strerror(f->bs_err));
            else OUT("  \u062d\u0627\u0644\u0629 \u0627\u0644\u062a\u0634\u063a\u064a\u0644: %s\u060c %u \u0645\u062d\u0627\u0648\u0644\u0627\u062a\u060c \u0622\u062e\u0631 \u062a\u0634\u063a\u064a\u0644 \u0645\u0643\u062a\u0645\u0644 %s\u060c \u062a\u063a\u064a\u0651\u0631\u062a %s\u060c \u0627\u0644\u0646\u0648\u0627\u0629 %s\u060c \u0627\u0644\u0633\u0628\u0628 \"%s\"\n",
                     bs_state_ar(f->bs.state), f->bs.tries, lastok, changed, f->bs.kernel, f->bs.reason);
            OUT("\n\u0627\u0644\u062d\u0644: \"recovery_tester cancel\" \u064a\u0631\u062c\u0651\u0639 \u0627\u0644\u0646\u0633\u062e\u0629 \u0627\u0644\u0633\u0644\u064a\u0645\u0629 \u064a\u062f\u0648\u064a\u064b\u0627 (\u0648\u064a\u0634\u062a\u063a\u0644 \u0623\u064a\u0636\u064b\u0627 \u0641\u064a\n"
                "\u0627\u0644\u0637\u0631\u0641\u064a\u0629 \u0627\u0644\u0645\u0624\u0642\u062a\u0629 \u0644\u0634\u0627\u0634\u0629 \u0627\u0644\u0627\u0633\u062a\u0631\u062f\u0627\u062f \u0628\u0639\u062f disk.open).\n");
            OUT("\n\u0627\u0644\u0633\u062c\u0644\u0627\u062a\n---- %s\n%s", STATE, st);
            OUT("---- \u0633\u062c\u0644 \u0627\u0644\u0646\u0648\u0627\u0629 (\u0622\u062e\u0631 %ld \u0628\u0627\u064a\u062a)\n%s\n", kl > 0 ? kl : 0, klog);
        }
#undef OUT
        if (o >= (int)sizeof b) o = (int)sizeof b - 1;
        path(p, sizeof p, lang ? "report-ar.txt" : "report-en.txt");
        int r = write_file(p, b, (size_t)o, false);
        printf("  %s: %s\n", p, r ? strerror(r) : "written");
    }
    sync();
}

static int check(const struct state *s) {
    char now[32], started[32];
    date(s->started, started, sizeof started);
    long boot = unix_time() - uptime_ms() / 1000;
    if (!root[0] && boot <= s->started) {
        printf(CYAN "recovery_tester: the test from %s waits for a restart" RESET "\n", started);
        printf("%s is damaged; the next start shows the red recovery screen.\n", s->target);
        if (yes("Restart now?")) { sync(); reboot(); }
        printf("Or undo it with: recovery_tester cancel\n");
        return 0;
    }
    struct facts f;
    gather(s, &f);
    if (root[0]) {
        printf(CYAN "recovery_tester: the recovery screen's temporary terminal" RESET "\n");
        printf("The test from %s: %s is %s.\n", started, s->target, f.target_valid ? "valid again" : f.target_why);
        printf("The system is repaired from here with Use backup, or by hand: \"recovery_tester cancel\"\n"
               "puts the good copy back. Then start the system and run recovery_tester again.\n");
        return 0;
    }

    const char *en[6], *ar[6];
    int n = 0;
    char why[200];
    if (!f.target_there) {
        en[n] = "the damaged file was not replaced: it is not on the disk any more";
        ar[n++] = "\u0627\u0644\u0645\u0644\u0641 \u0627\u0644\u062a\u0627\u0644\u0641 \u0644\u0645 \u064a\u064f\u0633\u062a\u0628\u062f\u0644: \u0644\u0645 \u064a\u0639\u062f \u0645\u0648\u062c\u0648\u062f\u064b\u0627 \u0639\u0644\u0649 \u0627\u0644\u0642\u0631\u0635";
    } else if (!f.target_valid) {
        en[n] = "the file is still damaged: the repair did not replace it";
        ar[n++] = "\u0627\u0644\u0645\u0644\u0641 \u0645\u0627 \u0632\u0627\u0644 \u062a\u0627\u0644\u0641\u064b\u0627: \u0627\u0644\u0625\u0635\u0644\u0627\u062d \u0644\u0645 \u064a\u0633\u062a\u0628\u062f\u0644\u0647";
    }
    if (!f.good_ok) {
        en[n] = "the good copy in /users/recovery_tester is missing or damaged, so it cannot be put back";
        ar[n++] = "\u0627\u0644\u0646\u0633\u062e\u0629 \u0627\u0644\u0633\u0644\u064a\u0645\u0629 \u0641\u064a /users/recovery_tester \u0645\u0641\u0642\u0648\u062f\u0629 \u0623\u0648 \u062a\u0627\u0644\u0641\u0629\u060c \u0641\u0644\u0627 \u064a\u0645\u0643\u0646 \u0625\u0631\u062c\u0627\u0639\u0647\u0627";
    }
    if (!f.bs_err && f.bs.last_ok && (long)f.bs.last_ok < s->started) {
        en[n] = "no start has completed since the test began (boot status)";
        ar[n++] = "\u0644\u0645 \u064a\u0643\u062a\u0645\u0644 \u0623\u064a \u062a\u0634\u063a\u064a\u0644 \u0645\u0646\u0630 \u0628\u062f\u0627\u064a\u0629 \u0627\u0644\u0627\u062e\u062a\u0628\u0627\u0631 (\u062d\u0627\u0644\u0629 \u0627\u0644\u062a\u0634\u063a\u064a\u0644)";
    }
    if (!n) {
        /* repaired: the good copy goes back over the repaired file */
        printf(CYAN "recovery_tester: the test from %s" RESET "\n", started);
        printf("  %s was repaired (%s, %lu bytes)%s\n", s->target, f.target_why, f.target_size,
               f.backup_system ? " by Use backup: the backup system is on the disk" : "");
        if (f.recovered[0]) printf("  files kept by Use backup: /users/%s\n", f.recovered);
        int r = put_back(s, why, sizeof why);
        if (!r) {
            state_clear(true);                   /* reports of an earlier check are out of date */
            date(unix_time(), now, sizeof now);
            printf("  the good copy is back in %s (CRC-32 %08x)\n\n", s->target, s->crc);
            printf(GREEN "recovery_tester: PASSED - the red recovery screen repaired the system, and the\n"
                   "original file is back." RESET "\n");
            if (f.backup_system)
                printf("The computer runs the backup system now: update.xvr brings the full itelxOS back.\n");
            return 0;
        }
        en[n] = why;
        ar[n++] = "\u062a\u0639\u0630\u0651\u0631 \u0625\u0631\u062c\u0627\u0639 \u0627\u0644\u0646\u0633\u062e\u0629 \u0627\u0644\u0633\u0644\u064a\u0645\u0629 \u0645\u0643\u0627\u0646 \u0627\u0644\u0645\u0644\u0641 \u0627\u0644\u0645\u064f\u0635\u0644\u064e\u062d (\u0627\u0644\u062a\u0641\u0627\u0635\u064a\u0644 \u0628\u0627\u0644\u0625\u0646\u062c\u0644\u064a\u0632\u064a\u0629 \u0641\u064a report-en.txt)";
    }
    printf(RED "recovery_tester: FAILED - the test from %s" RESET "\n", started);
    for (int i = 0; i < n; i++) printf("  - %s\n", en[i]);
    printf("The reasons and the logs:\n");
    reports(s, &f, n, en, ar);
    return 1;
}

static int cancel(void) {
    struct state s;
    if (!state_read(&s)) { printf("recovery_tester: no test is under way.\n"); return 0; }
    char why[200];
    int r = put_back(&s, why, sizeof why);
    if (r) { printf(RED "recovery_tester: %s" RESET "\n", why); return 1; }
    state_clear(true);
    printf(GREEN "recovery_tester: the test is undone: the good copy is back in %s%s." RESET "\n", root, s.target);
    return 0;
}

int main(int argc, char **argv) {
    bool want_cancel = argc == 2 && !strcmp(argv[1], "cancel");
    if (argc > 2 || (argc == 2 && !want_cancel)) {
        printf("recovery_tester tests the red recovery screen with a fault made on purpose.\n"
               "usage: recovery_tester           start a test, or check the one under way\n"
               "       recovery_tester cancel    undo the fault now\n");
        return argc == 2 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) ? 0 : 2;
    }
    if (recovery_start()) {
        /* disk.open puts the system disk at /media/broken-os in the
           recovery system of Use manual (itelx 2.1), at /old before */
        struct itx_stat st;
        if (stat("/media/broken-os" DIR "/" STATE, &st) == 0) strlcpy(root, "/media/broken-os", sizeof root);
        else if (stat("/old" DIR "/" STATE, &st) == 0) strlcpy(root, "/old", sizeof root);
        else {
            printf(YELLOW "recovery_tester: this is the recovery screen's terminal. Open the system disk\n"
                   "first (disk.open), then run recovery_tester again." RESET "\n");
            return 1;
        }
    }
    if (want_cancel) return cancel();
    struct state s;
    if (state_read(&s)) return check(&s);
    if (root[0]) { printf("recovery_tester: no test is under way on the system disk.\n"); return 0; }
    return start_test();
}
