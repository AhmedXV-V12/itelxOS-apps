/* cz: the shared part of the customizer installers and recustomize (cz.h). */
#include "cz.h"

/* ------------------------------------------------------------- CRC-32 */

static uint32_t crc_tab[256];

void cz_crc_init(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_tab[i] = c;
    }
}

uint32_t cz_crc(uint32_t crc, const void *data, size_t len) {
    const unsigned char *p = data;
    crc = ~crc;
    while (len--) crc = crc_tab[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

bool cz_file_crc(const char *path, uint32_t *crc, uint64_t *size) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return false;
    struct itx_stat st;
    if (fstat(fd, &st) || st.type != VT_FILE) { close(fd); return false; }
    static unsigned char buf[65536];
    uint32_t c = 0;
    uint64_t n = 0;
    for (;;) {
        ssize_t r = read(fd, buf, sizeof buf);
        if (r < 0) { close(fd); return false; }
        if (r == 0) break;
        c = cz_crc(c, buf, (size_t)r);
        n += (uint64_t)r;
    }
    close(fd);
    *crc = c;
    *size = n;
    return true;
}

/* ------------------------------------------------------------- payload */

const struct cz_head *cz_head(void) { return (const struct cz_head *)cz_payload; }
const struct cz_entry *cz_entries(void) { return (const struct cz_entry *)(cz_payload + sizeof(struct cz_head)); }
const unsigned char *cz_data(const struct cz_entry *e) { return cz_payload + e->offset; }

const struct cz_entry *cz_find(const char *path) {
    const struct cz_entry *e = cz_entries();
    for (uint32_t i = 0; i < cz_head()->count; i++)
        if (!strcmp(e[i].path, path)) return &e[i];
    return NULL;
}

bool cz_verify(char *why, size_t len) {
    const struct cz_head *h = cz_head();
    size_t span = (size_t)(cz_payload_end - cz_payload);
    if (span < sizeof *h || memcmp(h->magic, "ITXCZ001", 8) || !h->count || h->count > 4096 ||
        sizeof *h + (size_t)h->count * sizeof(struct cz_entry) > span) {
        snprintf(why, len, "the files inside are missing or damaged");
        return false;
    }
    const struct cz_entry *e = cz_entries();
    for (uint32_t i = 0; i < h->count; i++) {
        if (e[i].path[sizeof e[i].path - 1] || (uint64_t)e[i].offset + e[i].size > span ||
            cz_crc(0, cz_data(&e[i]), e[i].size) != e[i].crc) {
            snprintf(why, len, "%.100s inside it is damaged", e[i].path);
            return false;
        }
    }
    return true;
}

/* -------------------------------------------------------------- checks */

int cz_version_num(const char *v) {
    const char *p = strchr(v, ' ');
    p = p ? p + 1 : v;
    int major = (int)strtol(p, (char **)&p, 10), minor = 0;
    if (*p == '.') minor = (int)strtol(p + 1, NULL, 10);
    return major << 8 | minor;
}

bool cz_system_ok(void) {
    struct itx_sysinfo si;
    if (sysinfo(&si)) { printf(CZ_RED "cannot read what system this is" CZ_RESET "\n"); return false; }
    int v = cz_version_num(si.version);
    if (v == 0x0104) return true;
    printf(CZ_RED "this is %s; customizer apps are made for itelx 1.4" CZ_RESET "\n", si.version);
    if (v < 0x0104) printf("Update to itelx 1.4 first (its update.xvr), then run this again.\n");
    return false;
}

/* ------------------------------------------------------------- writing */

void cz_join(char *out, size_t len, const char *root, const char *path) {
    snprintf(out, len, "%s%s", strcmp(root, "/") ? root : "", path);
}

void cz_make_dirs(const char *file) {
    char p[ITX_PATH_MAX];
    strlcpy(p, file, sizeof p);
    for (char *s = p + 1; *s; s++) {
        if (*s != '/') continue;
        *s = 0;
        struct itx_stat st;
        if (stat(p, &st)) mkdir(p);
        *s = '/';
    }
}

int cz_install(const char *dst, const void *data, uint32_t size, uint32_t crc, char *why, size_t wlen) {
    uint32_t c;
    uint64_t sz;
    if (cz_file_crc(dst, &c, &sz) && c == crc && sz == size) return F_SAME;
    cz_make_dirs(dst);
    char tmp[ITX_PATH_MAX];
    snprintf(tmp, sizeof tmp, "%s.new", dst);
    remove(tmp);
    int fd = open(tmp, O_WRONLY | O_CREATE | O_TRUNC);
    if (fd == -EROFS) return F_ROFS;
    if (fd < 0) { snprintf(why, wlen, "%s: %s", tmp, strerror(fd)); return F_FAIL; }
    const unsigned char *d = data;
    uint32_t done = 0;
    while (done < size) {
        uint32_t n = size - done > 65536 ? 65536 : size - done;
        ssize_t w = write(fd, d + done, n);
        if (w <= 0) {
            close(fd);
            remove(tmp);
            if (w == -EROFS) return F_ROFS;
            snprintf(why, wlen, "%s: %s", tmp, w < 0 ? strerror((int)w) : "disk full");
            return F_FAIL;
        }
        done += (uint32_t)w;
    }
    close(fd);
    sync();
    if (!cz_file_crc(tmp, &c, &sz) || c != crc || sz != size) {
        remove(tmp);
        snprintf(why, wlen, "%s: what was written does not read back the same", tmp);
        return F_FAIL;
    }
    /* the old file stays as NAME.old until the new one is in place */
    char old[ITX_PATH_MAX];
    snprintf(old, sizeof old, "%s.old", dst);
    struct itx_stat st;
    bool had = stat(dst, &st) == 0;
    if (had) {
        remove(old);
        int r = rename(dst, old);
        if (r) { remove(tmp); snprintf(why, wlen, "%s: %s", dst, strerror(r)); return r == -EROFS ? F_ROFS : F_FAIL; }
    }
    int r = rename(tmp, dst);
    if (r) {
        if (had) rename(old, dst);
        remove(tmp);
        snprintf(why, wlen, "%s: %s", dst, strerror(r));
        return F_FAIL;
    }
    if (had) remove(old);
    sync();
    return F_DONE;
}

int cz_copy_file(const char *from, const char *to, uint32_t *crc) {
    int in = open(from, O_RDONLY);
    if (in < 0) return in;
    int out = open(to, O_WRONLY | O_CREATE | O_TRUNC);
    if (out < 0) { close(in); return out; }
    static unsigned char buf[65536];
    uint32_t c = 0;
    int r = 0;
    for (;;) {
        ssize_t n = read(in, buf, sizeof buf);
        if (n == 0) break;
        if (n < 0) { r = (int)n; break; }
        if (write(out, buf, (size_t)n) != n) { r = -ENOSPC; break; }
        c = cz_crc(c, buf, (size_t)n);
    }
    close(in);
    int e = close(out);
    if (crc) *crc = c;
    return r ? r : e;
}

/* --------------------------------------------------------- the machine */

bool cz_find_boot(char *out, size_t len) {
    struct itx_diskinfo dk[32];
    int n = disklist(dk, 32);
    for (int i = 0; i < n; i++) {
        char *b = strstr(dk[i].use, " (boot files)");
        if (!b) continue;
        size_t l = (size_t)(b - dk[i].use);
        if (l >= len) l = len - 1;
        memcpy(out, dk[i].use, l);
        out[l] = 0;
        return true;
    }
    return false;
}

bool cz_boot_writable(const char *boot) {
    char probe[ITX_PATH_MAX];
    snprintf(probe, sizeof probe, "%s/boot/czprobe.tmp", boot);
    int fd = open(probe, O_WRONLY | O_CREATE | O_TRUNC);
    if (fd < 0) return false;
    close(fd);
    remove(probe);
    return true;
}

bool cz_find_stick(char *out, size_t len, const char *not_this) {
    struct itx_diskinfo dk[32];
    int n = disklist(dk, 32);
    for (int i = 0; i < n; i++) {
        if (!dk[i].usb_id || strncmp(dk[i].use, "/media/", 7)) continue;
        char path[64];
        strlcpy(path, dk[i].use, sizeof path);
        char *sp = strchr(path, ' ');
        if (sp) *sp = 0;
        if (not_this && !strcmp(path, not_this)) continue;
        struct itx_fsinfo fi;
        if (fsinfo(path, &fi) || (fi.flags & ST_RDONLY)) continue;
        strlcpy(out, path, len);
        return true;
    }
    return false;
}

static bool read_answer(char *line, size_t len) {
    flush();
    ssize_t n = read(0, line, len - 1);
    if (n <= 0) { line[0] = 0; return false; }
    line[n] = 0;
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == ' ' || line[n - 1] == '\r')) line[--n] = 0;
    return true;
}

bool cz_ask_yes(const char *q) {
    printf("%s (yes/no) ", q);
    char line[32];
    if (!read_answer(line, sizeof line)) return false;
    return !strcmp(line, "yes") || !strcmp(line, "y") || !strcmp(line, "Yes") || !strcmp(line, "YES");
}

bool cz_ask_word(const char *q, const char *word) {
    printf("%s ", q);
    char line[64];
    if (!read_answer(line, sizeof line)) return false;
    return !strcmp(line, word);
}

int cz_restart_terminal(void) {
    struct itx_procinfo p[80];
    int n = proclist(p, 80), init = 0, done = 0;
    for (int i = 0; i < n; i++) if (!p[i].kernel && !strcmp(p[i].name, "init.xvr")) init = p[i].pid;
    for (int i = 0; i < n; i++) {
        if (p[i].kernel || p[i].ppid != init || p[i].pid == getpid()) continue;
        if (strcmp(p[i].name, "terminal.xvr")) continue;
        if (kill(p[i].pid) == 0) done++;
    }
    return done;
}

int cz_stop_others(void) {
    struct itx_procinfo p[80];
    int n = proclist(p, 80), done = 0;
    /* this program, the ones that started it (the terminal), and init stay */
    int keep[16], nk = 0, pid = getpid();
    for (int guard = 0; pid > 0 && nk < 16 && guard < 16; guard++) {
        keep[nk++] = pid;
        int parent = 0;
        for (int i = 0; i < n; i++) if (p[i].pid == pid) parent = p[i].ppid;
        pid = parent;
    }
    for (int i = 0; i < n; i++) {
        if (p[i].kernel || p[i].state == PS_ZOMBIE || !strcmp(p[i].name, "init.xvr")) continue;
        bool k = false;
        for (int j = 0; j < nk; j++) if (keep[j] == p[i].pid) k = true;
        if (!k && kill(p[i].pid) == 0) done++;
    }
    if (done) sleep_ms(500);
    return done;
}

void cz_today(char *out, size_t len) {
    long t = unix_time();
    long days = t / 86400, secs = t % 86400;
    /* days since 1970 to a civil date (Howard Hinnant's algorithm) */
    long z = days + 719468, era = (z >= 0 ? z : z - 146096) / 146097;
    long doe = z - era * 146097, yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long y = yoe + era * 400, doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
    long d = doy - (153 * mp + 2) / 5 + 1, m = mp < 10 ? mp + 3 : mp - 9;
    if (m <= 2) y++;
    snprintf(out, len, "%04ld-%02ld-%02ld %02ld:%02ld", y, m, d, secs / 3600, secs / 60 % 60);
}

/* -------------------------------------------- the update stick (kernel) */

bool cz_to_stick(const char *self, const char *title) {
    char stick[ITX_PATH_MAX], dst[ITX_PATH_MAX], old[ITX_PATH_MAX], mark[ITX_PATH_MAX];
    /* already the update on a stick, started by hand from the terminal */
    if (!strncmp(self, "/media/", 7) && ends_with(self, "/update.xvr")) {
        printf("This is the update on a USB stick: unplug the stick, plug it in again and\n"
               "answer " CZ_YELLOW "yes" CZ_RESET " to " CZ_YELLOW "are you need update ?" CZ_RESET "\n");
        return true;
    }
    if (!cz_find_stick(stick, sizeof stick, NULL)) {
        printf(CZ_YELLOW "Plug in a USB stick (FAT32) and run this again." CZ_RESET "\n"
               "The stick only carries this installer for a moment: it is put on the stick as\n"
               "update.xvr, and the update itelx starts from it may write the kernel.\n");
        return false;
    }
    snprintf(dst, sizeof dst, "%s/update.xvr", stick);
    snprintf(old, sizeof old, "%s/update.xvr.old", stick);
    snprintf(mark, sizeof mark, "%s/" CZ_STICK_MARK, stick);
    uint32_t mine, theirs;
    uint64_t ms, ts;
    if (!cz_file_crc(self, &mine, &ms)) {
        printf(CZ_RED "cannot read this program (%s)" CZ_RESET "\n", self);
        return false;
    }
    bool had_old = false;
    struct itx_stat st;
    if (stat(dst, &st) == 0 && !(cz_file_crc(dst, &theirs, &ts) && theirs == mine && ts == ms)) {
        if (stat(mark, &st) == 0) {
            remove(dst);                              /* another customizer this tool put there */
        } else {
            printf("%s already holds an update.xvr.\n", stick);
            if (!cz_ask_yes("Put it aside as update.xvr.old for now (it comes back afterwards)?")) return false;
            remove(old);
            int r = rename(dst, old);
            if (r) { printf(CZ_RED "%s: %s" CZ_RESET "\n", dst, strerror(r)); return false; }
            had_old = true;
        }
    }
    if (stat(mark, &st) == 0) {
        size_t len;
        char *m = read_file(mark, &len, 4096);
        if (m && strstr(m, "previous=update.xvr.old")) had_old = true;
        free(m);
    }
    printf("putting %s on %s as update.xvr ...\n", title, stick);
    uint32_t c = 0;
    int r = cz_copy_file(self, dst, &c);
    if (r || c != mine || !cz_file_crc(dst, &theirs, &ts) || theirs != mine) {
        printf(CZ_RED "the stick could not be written (%s)" CZ_RESET "\n", r ? strerror(r) : "it does not read back the same");
        remove(dst);
        if (had_old) rename(old, dst);
        return false;
    }
    char note[512];
    snprintf(note, sizeof note,
             "This stick carries the itelx customizer \"%s\" as update.xvr.\n"
             "Plugged into itelx 1.4 it asks \"are you need update ?\"; on yes the customizer\n"
             "installs, and then removes update.xvr and this file from the stick again.\n"
             "%s", title, had_old ? "previous=update.xvr.old\n" : "");
    write_file(mark, note, strlen(note), false);
    sync();
    int e = eject(stick);
    printf(CZ_GREEN "ready." CZ_RESET " Now:\n"
           "  1. %s the stick and plug it in again\n"
           "  2. itelx asks " CZ_YELLOW "are you need update ?" CZ_RESET ": answer " CZ_YELLOW "yes" CZ_RESET "\n"
           "  3. %s installs from there, the kernel too\n", e ? "unplug" : "the stick is written out: unplug", title);
    return true;
}

void cz_clean_stick(const char *stick) {
    char mark[ITX_PATH_MAX], dst[ITX_PATH_MAX], old[ITX_PATH_MAX];
    snprintf(mark, sizeof mark, "%s/" CZ_STICK_MARK, stick);
    struct itx_stat st;
    if (stat(mark, &st)) return;                      /* not prepared by a customizer */
    size_t len;
    char *m = read_file(mark, &len, 4096);
    bool had_old = m && strstr(m, "previous=update.xvr.old");
    free(m);
    snprintf(dst, sizeof dst, "%s/update.xvr", stick);
    snprintf(old, sizeof old, "%s/update.xvr.old", stick);
    remove(dst);
    if (had_old) rename(old, dst);
    remove(mark);
    sync();
    printf("the stick is as it was (%s)\n", had_old ? "its own update.xvr is back" : "update.xvr removed");
}

/* -------------------------------------------- what is installed (state) */

static void field(const char *text, const char *key, char *out, size_t len) {
    out[0] = 0;
    size_t kl = strlen(key);
    for (const char *p = text; p && *p;) {
        if (!strncmp(p, key, kl) && p[kl] == '=') {
            p += kl + 1;
            size_t n = 0;
            while (p[n] && p[n] != '\n') n++;
            if (n >= len) n = len - 1;
            memcpy(out, p, n);
            out[n] = 0;
            return;
        }
        p = strchr(p, '\n');
        if (p) p++;
    }
}

bool cz_state_read(struct cz_state *s) {
    memset(s, 0, sizeof *s);
    size_t len;
    char *t = read_file(CZ_STATE, &len, 1 << 20);
    if (!t) return false;
    char k[8];
    field(t, "id", s->id, sizeof s->id);
    field(t, "title", s->title, sizeof s->title);
    field(t, "version", s->version, sizeof s->version);
    field(t, "installed", s->installed, sizeof s->installed);
    field(t, "kernel", k, sizeof k);
    s->kernel = !strcmp(k, "yes");
    free(t);
    return s->id[0] != 0;
}
