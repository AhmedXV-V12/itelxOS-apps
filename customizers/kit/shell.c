/* kit: what a program that takes the terminal's place needs (kit.h).
 *
 * init starts /system/terminal.xvr and starts it again when it ends; with
 * a customizer that is Noter, School mode or Work mode. There is no
 * command line then, so this program has to offer what the terminal would:
 * restarting and powering off the computer, restoring the system
 * (recustomize), and USB sticks that bring an itelx update. */
#include "kit.h"

/* ---------------------------------------------------------- programs */

int k_run(const char *path, const char *const *args, int nargs) {
    const char *argv[16];
    int n = 0;
    if (ends_with(path, ".xvc")) {
        argv[n++] = "xvc";
        argv[n++] = path;
    } else {
        argv[n++] = path;
    }
    for (int i = 0; i < nargs && n < 15; i++) argv[n++] = args[i];
    k_end();
    printf("\x1b[2J");
    flush();
    int pid = spawn(ends_with(path, ".xvc") ? "/system/xvc.xvr" : path, argv, n, SPAWN_FG);
    int status = -1;
    if (pid < 0) {
        char why[96] = "";
        last_error(why, sizeof why);
        printf(K_RED "%s cannot start: %s %s" K_RESET "\n", path, strerror(pid), why);
    } else {
        waitpid(pid, &status, 0);
        tty_setfg(0);
    }
    printf("\n" K_DIM "press any key to go back" K_RESET);
    flush();
    k_begin();
    k_key();
    return status;
}

/* ------------------------------------------------------------ sticks */

int k_sticks(char out[][64], int max) {
    struct itx_diskinfo dk[32];
    int n = disklist(dk, 32), k = 0;
    for (int i = 0; i < n && k < max; i++) {
        if (!dk[i].usb_id || strncmp(dk[i].use, "/media/", 7)) continue;
        strlcpy(out[k], dk[i].use, 64);
        char *sp = strchr(out[k], ' ');
        if (sp) *sp = 0;
        k++;
    }
    return k;
}

static char seen[16][64];
static int nseen = -1;
static long last_look;

void k_watch_media(void) {
    long now = uptime_ms();
    if (nseen >= 0 && now - last_look < 700) return;
    last_look = now;
    char vols[16][64];
    int n = k_sticks(vols, 16);
    if (nseen < 0) {                                  /* what was there when the program started */
        for (int i = 0; i < n; i++) strlcpy(seen[i], vols[i], 64);
        nseen = n;
        return;
    }
    for (int i = 0; i < n; i++) {
        bool known = false;
        for (int j = 0; j < nseen; j++) if (!strcmp(seen[j], vols[i])) known = true;
        if (known) continue;
        char u[96];
        snprintf(u, sizeof u, "%s/update.xvr", vols[i]);
        struct itx_stat st;
        if (stat(u, &st) == 0) {
            /* itelx asks "are you need update ?" now: the terminal goes to
               the kernel's question (and to the update), not to raw keys */
            k_end();
            printf("\x1b[2J\n" K_CYAN " A USB stick with an itelx update was plugged in (%s)." K_RESET "\n\n"
                   " itelx asks: " K_YEL "are you need update ?" K_RESET "   type yes or no, then Enter.\n"
                   " When it is done, press Enter here to go back.\n\n", vols[i]);
            flush();
            char line[64];
            read(0, line, sizeof line);
            k_begin();
        }
    }
    nseen = n < 16 ? n : 16;
    for (int i = 0; i < nseen; i++) strlcpy(seen[i], vols[i], 64);
}

/* ------------------------------------------------------- system menu */

static void about(const char *app, const char *text) {
    struct itx_sysinfo si;
    struct itx_fsinfo fi;
    char buf[2048], tz[32];
    sysinfo(&si);
    int o = snprintf(buf, sizeof buf, "%s\n\n", text);
    o += snprintf(buf + o, sizeof buf - o, "system      %s, %s start\n", si.version, si.firmware == 2 ? "UEFI" : "BIOS");
    o += snprintf(buf + o, sizeof buf - o, "processor   %s\n", si.cpu);
    o += snprintf(buf + o, sizeof buf - o, "memory      %lu MiB, %lu MiB free\n", (unsigned long)(si.total_kib / 1024),
                  (unsigned long)(si.free_kib / 1024));
    if (fsinfo("/", &fi) == 0 && fi.free_bytes != ~0ULL)
        o += snprintf(buf + o, sizeof buf - o, "disk        %lu MiB, %lu MiB free\n", (unsigned long)(fi.total_bytes >> 20),
                      (unsigned long)(fi.free_bytes >> 20));
    o += snprintf(buf + o, sizeof buf - o, "up          %lu min\n", (unsigned long)(si.uptime_ms / 60000));
    int t = k_tz_minutes();
    snprintf(tz, sizeof tz, "UTC%c%d:%02d", t < 0 ? '-' : '+', (t < 0 ? -t : t) / 60, (t < 0 ? -t : t) % 60);
    o += snprintf(buf + o, sizeof buf - o, "time zone   %s\n", tz);
    size_t len;
    char *who = read_file("/system/owner", &len, 256), *host = read_file("/system/hostname", &len, 256);
    if (who) o += snprintf(buf + o, sizeof buf - o, "owner       %s", who);
    if (host) o += snprintf(buf + o, sizeof buf - o, "computer    %s", host);
    free(who);
    free(host);
    k_message(app, buf);
}

static void set_tz(void) {
    char b[32];
    if (!k_ask("Hours from UTC (for example 2, +3, -5:30; 0 when the clock already shows local time): ", b, sizeof b)) return;
    const char *p = b;
    int sign = 1;
    if (*p == '+') p++;
    else if (*p == '-') { sign = -1; p++; }
    char *end;
    long h = strtol(p, &end, 10), m = 0;
    if (end == p) { k_flash("not a time zone: %s", b); return; }
    if (*end == ':') m = strtol(end + 1, &end, 10);
    if (*end || h > 14 || m > 59) { k_flash("not a time zone: %s", b); return; }
    k_tz_set((int)(sign * (h * 60 + m)));
    k_flash("time zone set");
}

void k_system_menu(const char *app, const char *text) {
    static const char *const items[] = {
        "About this computer",
        "Time zone",
        "Restart the computer",
        "Power off",
        "Restore the system: itelx as it was installed (recustomize)",
        "Back",
    };
    for (;;) {
        char title[64];
        snprintf(title, sizeof title, "%s - system", app);
        int c = k_menu(title, items, 6, 0);
        if (c < 0 || c == 5) return;
        if (c == 0) about(app, text);
        else if (c == 1) { set_tz(); return; }
        else if (c == 2) { if (k_confirm("Restart the computer?")) { sync(); reboot(); } }
        else if (c == 3) { if (k_confirm("Power off?")) { sync(); poweroff(); } }
        else if (c == 4) {
            struct itx_stat st;
            if (stat("/system/recustomize.xvr", &st)) { k_flash("/system/recustomize.xvr is missing"); return; }
            /* recustomize asks for YES itself and says what it deletes */
            k_run("/system/recustomize.xvr", NULL, 0);
            return;
        }
    }
}

/* -------------------------------------------------------- the passcode */

#define LOCK_ITER 100000

/* a passcode typed with the keys hidden (dots on the screen) */
static bool read_secret(const char *title, const char *prompt, char *out, size_t len) {
    size_t n = 0;
    out[0] = 0;
    for (;;) {
        k_frame();
        k_header(title, NULL);
        k_pad_to(k_rows / 2 - 2);
        char dots[256] = "";
        for (size_t i = 0; i < n && i < 60; i++) strlcat(dots, "\xe2\x80\xa2", sizeof dots);
        k_line("    %s", prompt);
        k_line("%s", "");
        k_line("    " K_YEL "%s" K_RESET "\xe2\x96\x81", dots);
        k_footer("Enter take   Backspace correct   Esc cancel");
        k_show();
        int k = k_key_wait(1000);
        if (k == K_NONE) { k_watch_media(); continue; }
        if (k == K_ENTER) return true;
        if (k == K_ESC) return false;
        if (k == K_BS) {
            while (n > 0 && ((unsigned char)out[n - 1] & 0xC0) == 0x80) n--;
            if (n > 0) n--;
            out[n] = 0;
            continue;
        }
        if (k >= 0x20 && k < 0x110000 && n + 5 < len) {
            n += (size_t)utf8_encode((uint32_t)k, out + n);
            out[n] = 0;
        }
    }
}

/* the value after "key=" up to the end of its line */
static void value(const char *t, const char *key, char *out, size_t len) {
    out[0] = 0;
    const char *p = strstr(t, key);
    if (!p) return;
    p += strlen(key);
    size_t n = 0;
    while (p[n] && p[n] != '\n' && n + 1 < len) { out[n] = p[n]; n++; }
    out[n] = 0;
}

static int unhex(const char *s, uint8_t *out, size_t max) {
    size_t n = 0;
    for (; s[0] && s[1] && n < max; s += 2) {
        int a = isdigit(s[0]) ? s[0] - '0' : (s[0] | 32) - 'a' + 10, b = isdigit(s[1]) ? s[1] - '0' : (s[1] | 32) - 'a' + 10;
        if (a < 0 || a > 15 || b < 0 || b > 15) return -1;
        out[n++] = (uint8_t)(a << 4 | b);
    }
    return (int)n;
}

/* the passcode file: salt=<hex> iter=<n> hash=<hex>, one per line */
static bool check(const char *file, const char *pass) {
    size_t len;
    char *t = read_file(file, &len, 1024);
    if (!t) return true;
    char salt_hex[80], iter_s[16], hash_hex[80];
    value(t, "salt=", salt_hex, sizeof salt_hex);
    value(t, "iter=", iter_s, sizeof iter_s);
    value(t, "hash=", hash_hex, sizeof hash_hex);
    free(t);
    uint8_t salt[32], want[32], got[32];
    int sl = unhex(salt_hex, salt, sizeof salt);
    long iter = atol(iter_s);
    if (sl <= 0 || unhex(hash_hex, want, sizeof want) != 32 || iter < 1 || iter > 10000000) return false;
    k_pbkdf2_sha256(pass, strlen(pass), salt, (size_t)sl, (uint32_t)iter, got, 32);
    uint8_t diff = 0;
    for (int i = 0; i < 32; i++) diff |= got[i] ^ want[i];
    return diff == 0;
}

bool k_lock(const char *file, const char *app) {
    struct itx_stat st;
    if (stat(file, &st)) return true;
    int fails = 0;
    for (;;) {
        char pass[128], title[96];
        snprintf(title, sizeof title, "%s - locked", app);
        if (!read_secret(title, fails ? "Wrong passcode. Passcode:" : "Passcode:", pass, sizeof pass)) continue;
        if (check(file, pass)) { memset(pass, 0, sizeof pass); return true; }
        memset(pass, 0, sizeof pass);
        fails++;
        if (fails >= 3) sleep_ms(1000L * (fails - 2) * 2);   /* slower after each wrong try */
    }
}

void k_lock_set(const char *file) {
    char a[128], b[128];
    struct itx_stat st;
    bool has = stat(file, &st) == 0;
    if (has) {
        if (!read_secret("Passcode", "The passcode now:", a, sizeof a)) return;
        if (!check(file, a)) { k_flash("that is not the passcode"); return; }
    }
    if (!read_secret("Passcode", "New passcode (empty: no passcode):", a, sizeof a)) return;
    if (!a[0]) {
        if (has) { remove(file); k_flash("the passcode is removed"); }
        return;
    }
    if (!read_secret("Passcode", "The new passcode again:", b, sizeof b)) return;
    if (strcmp(a, b)) { k_flash("the two passcodes were not the same; nothing changed"); return; }
    uint8_t salt[16], hash[32];
    k_random(salt, sizeof salt);
    k_pbkdf2_sha256(a, strlen(a), salt, sizeof salt, LOCK_ITER, hash, 32);
    char sh[40], hh[70], text[200];
    k_hex(sh, salt, sizeof salt);
    k_hex(hh, hash, sizeof hash);
    int n = snprintf(text, sizeof text, "salt=%s\niter=%d\nhash=%s\n", sh, LOCK_ITER, hh);
    memset(a, 0, sizeof a);
    memset(b, 0, sizeof b);
    k_flash(k_save_file(file, text, (size_t)n) ? "the passcode is set: it is asked for at every start" : "the passcode could not be saved");
}
