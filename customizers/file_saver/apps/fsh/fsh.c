/* The terminal of File saver: the itelx terminal (term.h) with the vault's
 * commands and the network's. Installed as /system/terminal.xvr.
 *
 *   vault                    the organizer (full screen)
 *   store PATH [FOLDER]      a file or folder into the vault (by its kind,
 *                            or into FOLDER); a replaced file keeps its
 *                            earlier version
 *   organize [FOLDER]        loose files of the Inbox (or FOLDER) into their folders
 *   find TEXT                files of the vault whose name has TEXT
 *   space                    what each folder of the vault takes
 *   check | reindex          SHA-256 of every file against the index | a new index
 *   password                 set or change the vault password
 *   lock PATH | unlock PATH  encrypt a file (or every file of a folder) / back
 *   trash [PATH | restore NAME | empty]
 *   versions FILE            a file's earlier versions; restore one with copy
 *   backup [STICK] | restore [STICK]   the vault to / from a USB stick
 *   share [FOLDER] [PORT]    the folder over the network, read only (HTTP)
 *   net, ping, dns, fetch    the network (libnet.h)
 */
#include <term.h>
#include <libnet.h>
#include <vault.h>

void term_start(void) {
    vault_init();
    vault_index_update();
    chdir(VAULT);
}

void term_banner(void) {
    struct itx_sysinfo si;
    sysinfo(&si);
    printf("\n\x1b[96mFile saver 1.0\x1b[0m  -  %s.  Your files live in \x1b[93m/vault\x1b[0m: type \x1b[93mvault\x1b[0m for the\n"
           "organizer, \x1b[93mhelp\x1b[0m for the commands.\n", si.version);
    struct itx_netinfo ni[4];
    int n = net_info(ni, 4);
    if (n > 0) printf("network: %s (%s)%s\n", ni[0].name, ni[0].model, ni[0].flags & NETF_LINK ? "" : ", no cable");
    else if (n == 0) printf("network: no card this kernel has a driver for (type net)\n");
}

void term_prompt(const char *cwd) { printf("vault:%s> ", cwd); }

void term_help(void) {
    printf("\nFile saver:\n"
           "  vault                    the organizer of the vault (full screen)\n"
           "  store PATH [FOLDER]      a file or a folder into the vault (sorted by its kind)\n"
           "  organize [FOLDER]        the files of the Inbox into their folders\n"
           "  find TEXT                files of the vault with TEXT in the name\n"
           "  space                    what each folder of the vault takes\n"
           "  check | reindex          compare every file with its SHA-256 | make a new index\n"
           "  password                 set or change the vault password\n"
           "  lock PATH | unlock PATH  encrypt a file or a folder's files / decrypt\n"
           "  trash [PATH]             move to the trash | list it; trash restore NAME; trash empty\n"
           "  versions FILE            the earlier versions of a file\n"
           "  backup [STICK]           the vault to a USB stick (only what changed)\n"
           "  restore [STICK]          the vault back from a USB stick\n"
           "  share [FOLDER] [PORT]    the folder to the network, read only (a browser can open it)\n");
    net_help();
}

/* ------------------------------------------------------------ helpers */

/* a password typed with the keys hidden */
static bool read_hidden(const char *prompt, char *out, size_t len) {
    printf("%s", prompt);
    flush();
    tty_mode(TTY_MODE_RAW);
    size_t n = 0;
    bool ok = false;
    for (;;) {
        struct itx_key k;
        if (tty_key(&k) < 0) break;
        if (k.code == '\n' || k.code == '\r') { ok = true; break; }
        if (k.code == KEY_ESC || (k.code == 3 && (k.mods & 2))) break;
        if (k.code == '\b' || k.code == 0x7F) {
            if (n) { while (n > 0 && ((unsigned char)out[n - 1] & 0xC0) == 0x80) n--; if (n) n--; printf("\b \b"); flush(); }
            continue;
        }
        if (k.code >= 0x20 && k.code < 0x110000 && n + 5 < len) {
            n += (size_t)utf8_encode(k.code, out + n);
            printf("*");
            flush();
        }
    }
    out[n] = 0;
    tty_mode(TTY_MODE_COOKED);
    printf("\n");
    return ok;
}

static bool need_key(void) {
    if (vault_unlocked()) return true;
    if (!vault_has_password()) {
        printf("The vault has no password yet: set one with \x1b[93mpassword\x1b[0m first.\n");
        return false;
    }
    char p[128];
    if (!read_hidden("vault password: ", p, sizeof p)) return false;
    int r = vault_open(p);
    memset(p, 0, sizeof p);
    if (r == -EACCES) { printf("\x1b[91mwrong password\x1b[0m\n"); return false; }
    if (r) { printf("\x1b[91mthe vault key: %s\x1b[0m\n", strerror(r)); return false; }
    return true;
}

static bool pick_stick(const char *given, char *out, size_t len) {
    if (given) { strlcpy(out, given, len); return true; }
    char vols[8][64];
    int n = k_sticks(vols, 8);
    if (!n) { printf("plug in a USB stick (FAT32), or name it: backup /media/NAME\n"); return false; }
    if (n > 1) printf("more than one stick; using %s (name another one to choose)\n", vols[0]);
    strlcpy(out, vols[0], len);
    return true;
}

static void progress(const char *path) {
    printf("  %s\n", path);
    flush();
}

static void report(const char *path, const char *what) { printf("  \x1b[91m%s\x1b[0m  %s\n", what, path); }

struct find_ctx { const char *text; int n; };

static void find_in(const char *dir, struct find_ctx *c) {
    int fd = open(dir, O_RDONLY);
    if (fd < 0) return;
    struct itx_dirent de;
    char (*sub)[256] = malloc(64 * 256);
    int ns = 0;
    while (readdir(fd, &de) == 1) {
        char p[ITX_PATH_MAX];
        snprintf(p, sizeof p, "%s/%s", dir, de.name);
        if (de.type == VT_DIR) { if (!vault_hidden(de.name) && ns < 64) strlcpy(sub[ns++], p, 256); continue; }
        if (k_strcasestr(de.name, c->text)) {
            char h[32];
            term_human(de.size, h, sizeof h);
            printf("  %10s  %s\n", h, p);
            c->n++;
        }
    }
    close(fd);
    for (int i = 0; i < ns; i++) find_in(sub[i], c);
    free(sub);
}

static uint64_t du(const char *dir, int *files) {
    uint64_t s = 0;
    int fd = open(dir, O_RDONLY);
    if (fd < 0) return 0;
    struct itx_dirent de;
    char (*sub)[256] = malloc(64 * 256);
    int ns = 0;
    while (readdir(fd, &de) == 1) {
        if (de.type == VT_DIR) { if (ns < 64) snprintf(sub[ns++], 256, "%s/%s", dir, de.name); continue; }
        s += de.size;
        (*files)++;
    }
    close(fd);
    for (int i = 0; i < ns; i++) s += du(sub[i], files);
    free(sub);
    return s;
}

static int lock_tree(const char *p, bool lock, int *n) {
    struct itx_stat st;
    int r = stat(p, &st);
    if (r) return r;
    if (st.type == VT_FILE) {
        if (lock == ends_with(p, LOCKED_EXT)) return 0;     /* already as wanted */
        r = lock ? vault_lock_file(p) : vault_unlock_file(p);
        if (!r) { (*n)++; printf("  %s %s\n", lock ? "locked" : "unlocked", p); flush(); }
        else printf("  \x1b[91m%s: %s\x1b[0m\n", p, r == -EACCES ? "not this vault's key, or the file was changed" : strerror(r));
        return 0;
    }
    int fd = open(p, O_RDONLY);
    if (fd < 0) return fd;
    char (*names)[256] = malloc(256 * 256);
    int k = 0;
    struct itx_dirent de;
    while (readdir(fd, &de) == 1 && k < 256) strlcpy(names[k++], de.name, 256);
    close(fd);
    for (int i = 0; i < k; i++) {
        char q[ITX_PATH_MAX];
        snprintf(q, sizeof q, "%s/%s", p, names[i]);
        if (!vault_hidden(names[i])) lock_tree(q, lock, n);
    }
    free(names);
    return 0;
}

/* -------------------------------------------------------- the commands */

bool term_command(int argc, char **argv) {
    const char *c = argv[0];
    if (!strcmp(c, "net")) { cmd_net(argc, argv); return true; }
    if (!strcmp(c, "ping")) { cmd_ping(argc, argv); return true; }
    if (!strcmp(c, "dns")) { cmd_dns(argc, argv); return true; }
    if (!strcmp(c, "fetch")) { cmd_fetch(argc, argv); return true; }
    if (!strcmp(c, "vault")) {
        char *a[] = { "/dex/vault.xvr", NULL };
        term_exec("/dex/vault.xvr", 1, a);
        return true;
    }
    if (!strcmp(c, "share")) {
        char *a[4] = { "/dex/share.xvr", argc > 1 ? argv[1] : (char *)term_cwd(), argc > 2 ? argv[2] : "8080", NULL };
        term_exec("/dex/share.xvr", 3, a);
        return true;
    }
    if (!strcmp(c, "store")) {
        if (argc < 2) { printf("use: store PATH [FOLDER]   e.g. store /media/STICK/photos   store report.pdf /vault/Documents/work\n"); return true; }
        char folder[ITX_PATH_MAX], dst[ITX_PATH_MAX];
        if (argc > 2) {
            term_absolute(argv[2], folder, sizeof folder);
            if (strncmp(folder, VAULT, strlen(VAULT))) { printf("the folder has to be in /vault\n"); return true; }
            k_mkdirs(folder);
        }
        int files;
        uint64_t bytes;
        int r = vault_store(argv[1], argc > 2 ? folder : NULL, dst, sizeof dst, &files, &bytes);
        char h[32];
        term_human(bytes, h, sizeof h);
        if (r) term_err(argv[1], r);
        else if (!files) printf("%s is in the vault already, unchanged (%s)\n", argv[1], dst);
        else printf("stored %d file(s), %s: %s\n", files, h, dst);
        return true;
    }
    if (!strcmp(c, "organize")) {
        char dir[ITX_PATH_MAX];
        if (argc > 1) term_absolute(argv[1], dir, sizeof dir);
        else strlcpy(dir, VAULT "/Inbox", sizeof dir);
        int moved;
        int r = vault_organize(dir, &moved);
        if (r) term_err(dir, r);
        else printf("%d file(s) of %s put into their folders\n", moved, dir);
        return true;
    }
    if (!strcmp(c, "find")) {
        if (argc < 2) { printf("use: find TEXT\n"); return true; }
        struct find_ctx f = { argv[1], 0 };
        find_in(VAULT, &f);
        printf("%d file(s)\n", f.n);
        return true;
    }
    if (!strcmp(c, "space")) {
        uint64_t all = 0;
        int nall = 0;
        for (int i = 0; i < vault_nfolders; i++) {
            char p[64], h[32];
            int files = 0;
            snprintf(p, sizeof p, VAULT "/%s", vault_folders[i]);
            uint64_t s = du(p, &files);
            all += s;
            nall += files;
            term_human(s, h, sizeof h);
            printf("  %-10s %5d file(s)  %10s\n", vault_folders[i], files, h);
        }
        static const char *const hidden[] = { VVERS, VTRASH };
        for (int i = 0; i < 2; i++) {
            int files = 0;
            char h[32];
            uint64_t s = du(hidden[i], &files);
            term_human(s, h, sizeof h);
            printf("  \x1b[90m%-10s %5d file(s)  %10s\x1b[0m\n", i ? "trash" : "versions", files, h);
        }
        char h[32];
        term_human(all, h, sizeof h);
        struct itx_fsinfo fi;
        printf("the vault: %d file(s), %s", nall, h);
        if (fsinfo("/", &fi) == 0 && fi.free_bytes != ~0ULL) { term_human(fi.free_bytes, h, sizeof h); printf("; the disk has %s free", h); }
        printf("\n");
        return true;
    }
    if (!strcmp(c, "check")) {
        printf("reading every file of the vault ...\n");
        flush();
        struct vcheck r;
        vault_check(&r, report);
        printf("%d file(s) as indexed, \x1b[%sm%d changed, %d missing\x1b[0m, %d new\n", r.ok, r.changed || r.missing ? "91" : "92",
               r.changed, r.missing, r.added);
        if (r.changed || r.missing || r.added) printf("(if these changes are yours, reindex takes them as they are now)\n");
        return true;
    }
    if (!strcmp(c, "reindex")) {
        remove(VINDEX);
        int n = vault_index_update();
        printf("index made: %d file(s) hashed (SHA-256)\n", n);
        return true;
    }
    if (!strcmp(c, "password")) {
        char old[128] = "", a[128], b[128];
        if (vault_has_password() && !read_hidden("the password now: ", old, sizeof old)) return true;
        if (!read_hidden("new password: ", a, sizeof a) || !a[0]) { printf("no password given; nothing changed\n"); return true; }
        if (!read_hidden("the new password again: ", b, sizeof b) || strcmp(a, b)) { printf("the two were not the same; nothing changed\n"); return true; }
        printf("working (the password is stretched 200000 times) ...\n");
        flush();
        int r = vault_set_password(old, a);
        memset(old, 0, sizeof old);
        memset(a, 0, sizeof a);
        memset(b, 0, sizeof b);
        if (r == -EACCES) printf("\x1b[91mthat is not the password\x1b[0m\n");
        else if (r) term_err("password", r);
        else printf("\x1b[92mthe vault password is set\x1b[0m. Without it, locked files cannot be opened: keep it safe.\n");
        return true;
    }
    if (!strcmp(c, "lock") || !strcmp(c, "unlock")) {
        if (argc < 2) { printf("use: %s FILE|FOLDER\n", c); return true; }
        if (!need_key()) return true;
        int n = 0;
        int r = lock_tree(argv[1], c[0] == 'l', &n);
        if (r) term_err(argv[1], r);
        else printf("%d file(s) %sed\n", n, c);
        return true;
    }
    if (!strcmp(c, "trash")) {
        if (argc > 2 && !strcmp(argv[1], "restore")) {
            char where[ITX_PATH_MAX];
            int r = vault_trash_restore(argv[2], where, sizeof where);
            if (r) term_err(argv[2], r);
            else printf("back at %s\n", where);
            return true;
        }
        if (argc > 1 && !strcmp(argv[1], "empty")) {
            int n;
            vault_trash_empty(&n);
            printf("the trash is empty (%d deleted for good)\n", n);
            return true;
        }
        if (argc > 1) {
            int r = vault_trash(argv[1]);
            if (r) term_err(argv[1], r);
            else printf("%s is in the trash (trash restore NAME brings it back)\n", argv[1]);
            return true;
        }
        int fd = open(VTRASH, O_RDONLY);
        struct itx_dirent de;
        int n = 0;
        while (fd >= 0 && readdir(fd, &de) == 1) {
            if (ends_with(de.name, ".from")) continue;
            char f[ITX_PATH_MAX];
            snprintf(f, sizeof f, VTRASH "/%s.from", de.name);
            size_t l;
            char *from = read_file(f, &l, ITX_PATH_MAX);
            printf("  %-40s from %s\n", de.name, from ? from : "?");
            free(from);
            n++;
        }
        if (fd >= 0) close(fd);
        printf("%d in the trash\n", n);
        return true;
    }
    if (!strcmp(c, "versions")) {
        if (argc < 2) { printf("use: versions FILE\n"); return true; }
        char p[ITX_PATH_MAX], names[32][128];
        term_absolute(argv[1], p, sizeof p);
        int n = vault_versions(p, names, 32);
        for (int i = 0; i < n; i++) printf("  " VVERS "%s\n", names[i]);
        printf("%d earlier version(s)%s\n", n, n ? "; copy one back with: copy <it> <where>" : "");
        return true;
    }
    if (!strcmp(c, "backup") || !strcmp(c, "restore")) {
        char stick[ITX_PATH_MAX];
        if (!pick_stick(argc > 1 ? argv[1] : NULL, stick, sizeof stick)) return true;
        int copied = 0, same = 0;
        uint64_t bytes = 0;
        int r = c[0] == 'b' ? vault_backup(stick, &copied, &same, &bytes, progress) : vault_restore(stick, &copied, &same, progress);
        if (r == -ENOENT && c[0] == 'r') { printf("%s has no vault-backup folder\n", stick); return true; }
        if (r) term_err(stick, r);
        char h[32];
        term_human(bytes, h, sizeof h);
        if (c[0] == 'b') printf("backup: %d file(s) copied (%s), %d already the same\n", copied, h, same);
        else printf("restore: %d file(s) copied, %d already the same\n", copied, same);
        if (c[0] == 'b') printf("then: eject %s before unplugging it\n", stick);
        return true;
    }
    return false;
}
