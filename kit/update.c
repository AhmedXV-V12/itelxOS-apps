/* update.xvr of the itelxOS apps USB kit.
 *
 * The stick holds this file at its top and the programs in a folder dex/
 * next to it (.xvr executables and .xvc programs). When the stick is
 * plugged in, itelx asks "are you need update ?"; on yes the kernel starts
 * this program with the stick's path as argument 1, and it copies every
 * program from dex/ on the stick into /dex, where the terminal finds them
 * by name. An .xvr file the kernel would not run is skipped, with the
 * reason. Programs already in /dex with the same name are replaced.
 *
 * Built with: xvrlink --update (see the Makefile).
 */
#include <itx.h>

static int copy(const char *from, const char *to, unsigned long *bytes) {
    int in = open(from, O_RDONLY);
    if (in < 0) return in;
    int out = open(to, O_WRONLY | O_CREATE | O_TRUNC);
    if (out < 0) { close(in); return out; }
    static char buf[65536];
    int r = 0;
    for (;;) {
        ssize_t n = read(in, buf, sizeof buf);
        if (n == 0) break;
        if (n < 0) { r = (int)n; break; }
        if (write(out, buf, n) != n) { r = -ENOSPC; break; }
        *bytes += (unsigned long)n;
    }
    close(in);
    int c = close(out);
    return r ? r : c;
}

int main(int argc, char **argv) {
    const char *vol = argc > 1 ? argv[1] : ".";
    char dir[ITX_PATH_MAX];
    snprintf(dir, sizeof dir, "%s/dex", vol);
    printf("\x1b[96mitelxOS apps - installing from %s\x1b[0m\n", vol);
    int fd = open(dir, O_RDONLY);
    if (fd < 0) {
        printf("nothing to install: %s has no dex folder\n", vol);
        return 1;
    }
    struct itx_dirent de;
    int done = 0, failed = 0;
    unsigned long total = 0;
    char names[2048] = "";
    while (readdir(fd, &de) == 1) {
        bool xvr = ends_with(de.name, ".xvr"), xvc = ends_with(de.name, ".xvc");
        if (de.type != VT_FILE || (!xvr && !xvc)) continue;
        char from[ITX_PATH_MAX], to[ITX_PATH_MAX];
        snprintf(from, sizeof from, "%s/%s", dir, de.name);
        snprintf(to, sizeof to, "/dex/%s", de.name);
        if (xvr) {
            char why[96] = "";
            if (xvr_check(from, why, sizeof why)) {
                printf("  \x1b[93mskipped %s: %s\x1b[0m\n", de.name, why);
                failed++;
                continue;
            }
        }
        unsigned long bytes = 0;
        int r = copy(from, to, &bytes);
        if (r) {
            printf("  \x1b[91m%s: %s\x1b[0m\n", de.name, strerror(r));
            failed++;
            continue;
        }
        printf("  installed %-22s %7lu bytes\n", to, bytes);
        total += bytes;
        done++;
        de.name[de.name_len - 4] = 0;
        if (strlen(names) + strlen(de.name) + 2 < sizeof names) {
            if (names[0]) strlcat(names, " ", sizeof names);
            strlcat(names, de.name, sizeof names);
        }
    }
    close(fd);
    sync();
    printf("update done: %d program(s) installed (%lu KiB)%s\n", done, (total + 1023) / 1024,
           failed ? ", some skipped" : "");
    if (done) printf("run them by name: %s\n", names);
    printf("start with: \x1b[93mwelcome\x1b[0m   (then eject the stick: eject %s)\n", vol);
    return failed ? 1 : 0;
}
