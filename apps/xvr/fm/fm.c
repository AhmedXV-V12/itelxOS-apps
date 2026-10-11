/* fm: a file manager for itelx, driven by the arrow keys.
 *
 *   fm [folder]
 *
 *   arrows, PgUp/PgDn, Home/End   choose
 *   Enter or Right                open: a folder, a program (.xvr/.xvc runs),
 *                                 any other file is shown
 *   Backspace or Left             the folder above
 *   c copy    m move or rename    d delete    n new folder
 *   i install a program into /dex    e edit (with edit.xvr)
 *   g go to a folder    r refresh    q or Esc quit
 *
 * Copying and moving work between disks too (e.g. to a USB stick in
 * /media). Folders are copied and deleted with everything in them. What
 * itelx itself needs (/kernel, /boot, /system, the top folders) cannot be
 * deleted or moved here.
 */
#include <ui.h>

typedef struct {
    char name[ITX_NAME_MAX + 1];
    uint32_t type;
    uint64_t size;
} entry;

static char cwd[ITX_PATH_MAX] = "/";
static entry *ents;
static int nents, cap, sel, top;
static char message[256];

/* ------------------------------------------------------------- paths */

static void join(char *out, size_t len, const char *dir, const char *name) {
    if (!strcmp(dir, "/")) snprintf(out, len, "/%s", name);
    else snprintf(out, len, "%s/%s", dir, name);
}

static void parent(char *path) {
    char *s = strrchr(path, '/');
    if (!s || s == path) strcpy(path, "/");
    else *s = 0;
}

static const char *base_name(const char *p) {
    const char *s = strrchr(p, '/');
    return s ? s + 1 : p;
}

/* strtok on '/', without the C library's strtok */
static char *tok_next;
static char *strtok_simple(char *s) {
    if (s) tok_next = s;
    if (!tok_next) return NULL;
    while (*tok_next == '/') tok_next++;
    if (!*tok_next) { tok_next = NULL; return NULL; }
    char *start = tok_next;
    while (*tok_next && *tok_next != '/') tok_next++;
    if (*tok_next) *tok_next++ = 0;
    else tok_next = NULL;
    return start;
}

/* an absolute path from what the user typed */
static void resolve(char *out, size_t len, const char *typed) {
    char tmp[ITX_PATH_MAX];
    if (typed[0] == '/') strlcpy(tmp, typed, sizeof tmp);
    else join(tmp, sizeof tmp, cwd, typed);
    /* . and .. parts */
    char res[ITX_PATH_MAX] = "/";
    for (char *part = strtok_simple(tmp); part; part = strtok_simple(NULL)) {
        if (!strcmp(part, ".")) continue;
        if (!strcmp(part, "..")) { parent(res); continue; }
        char j[ITX_PATH_MAX];
        join(j, sizeof j, res, part);
        strlcpy(res, j, sizeof res);
    }
    strlcpy(out, res, len);
}

static bool protected_path(const char *p) {
    static const char *const keep[] = { "/", "/kernel", "/boot", "/system", "/dex", "/media", "/ram" };
    for (size_t i = 0; i < sizeof keep / sizeof keep[0]; i++) if (!strcmp(p, keep[i])) return true;
    static const char *const under[] = { "/kernel/", "/boot/", "/system/", "/ram/" };
    for (size_t i = 0; i < sizeof under / sizeof under[0]; i++)
        if (!strncmp(p, under[i], strlen(under[i]))) return true;
    /* a mounted volume itself: /media/NAME */
    if (!strncmp(p, "/media/", 7) && !strchr(p + 7, '/')) return true;
    return false;
}

/* --------------------------------------------------------- the folder */

static int cmp(const entry *a, const entry *b) {
    bool da = a->type == VT_DIR, db = b->type == VT_DIR;
    if (da != db) return da ? -1 : 1;
    return strcmp(a->name, b->name);
}

static void load(void) {
    nents = 0;
    int fd = open(cwd, O_RDONLY);
    if (fd < 0) {
        snprintf(message, sizeof message, C_RED "%s: %s" C_RESET, cwd, strerror(fd));
        return;
    }
    struct itx_dirent de;
    while (readdir(fd, &de) == 1) {
        if (!strcmp(de.name, ".") || !strcmp(de.name, "..")) continue;
        if (nents == cap) {
            int nc = cap ? cap * 2 : 64;
            entry *n = realloc(ents, sizeof(entry) * (size_t)nc);
            if (!n) break;
            ents = n;
            cap = nc;
        }
        strlcpy(ents[nents].name, de.name, sizeof ents[nents].name);
        ents[nents].type = de.type;
        ents[nents].size = de.size;
        nents++;
    }
    close(fd);
    for (int i = 1; i < nents; i++) {                 /* insertion sort */
        entry k = ents[i];
        int j = i - 1;
        while (j >= 0 && cmp(&ents[j], &k) > 0) { ents[j + 1] = ents[j]; j--; }
        ents[j + 1] = k;
    }
    if (sel >= nents) sel = nents ? nents - 1 : 0;
}

static void go(const char *path, const char *select) {
    struct itx_stat st;
    int r = stat(path, &st);
    if (r || st.type != VT_DIR) {
        snprintf(message, sizeof message, C_RED "%s: %s" C_RESET, path, r ? strerror(r) : "not a folder");
        return;
    }
    strlcpy(cwd, path, sizeof cwd);
    chdir(cwd);
    sel = top = 0;
    load();
    if (select)
        for (int i = 0; i < nents; i++)
            if (!strcmp(ents[i].name, select)) sel = i;
}

static void human(uint64_t n, char *out, size_t len) {
    if (n < 1024) snprintf(out, len, "%lu B", (unsigned long)n);
    else if (n < 1048576) snprintf(out, len, "%lu.%lu KiB", (unsigned long)(n / 1024), (unsigned long)(n % 1024 * 10 / 1024));
    else if (n < 1073741824) snprintf(out, len, "%lu.%lu MiB", (unsigned long)(n >> 20), (unsigned long)((n & 0xFFFFF) * 10 >> 20));
    else snprintf(out, len, "%lu.%lu GiB", (unsigned long)(n >> 30), (unsigned long)((n & 0x3FFFFFFF) * 10 >> 30));
}

/* ---------------------------------------------------------- drawing */

static int list_rows(void) {
    int cols, rows;
    ui_size(&cols, &rows);
    int n = rows - 6;
    return n < 5 ? 5 : n;
}

static void draw(void) {
    int rows = list_rows();
    if (sel < top) top = sel;
    if (sel >= top + rows) top = sel - rows + 1;
    ui_frame();
    ui_printf(C_CYAN "FM" C_RESET "  %s   " C_DIM "%d item(s)" C_RESET "\n", cwd, nents);
    if (!nents) ui_put(C_DIM "  (empty)" C_RESET "\n");
    for (int i = top; i < nents && i < top + rows; i++) {
        entry *e = &ents[i];
        const char *col = e->type == VT_DIR ? C_CYAN : ends_with(e->name, ".xvr") ? C_GREEN :
                          ends_with(e->name, ".xvc") ? C_YELLOW : e->type == VT_DEV ? C_DIM : "";
        char size[32] = "";
        if (e->type == VT_DIR) strcpy(size, "folder");
        else if (e->type == VT_DEV) strcpy(size, "device");
        else human(e->size, size, sizeof size);
        int pad = 40 - (int)utf8_len(e->name) - (e->type == VT_DIR);
        if (pad < 1) pad = 1;
        ui_printf("%s%s%s%s" C_RESET "%*s%s\n", i == sel ? C_YELLOW "> " C_RESET : "  ", col, e->name,
                  e->type == VT_DIR ? "/" : "", pad, "", size);
    }
    if (top + rows < nents) ui_printf(C_DIM "  ... %d more" C_RESET "\n", nents - top - rows);
    ui_put("\n" C_DIM "Enter open  Backspace up  c copy  m move  d delete  n new folder  i install  "
           "e edit  g go to  q quit" C_RESET "\n");
    ui_printf("%s\n", message);
    ui_show();
    message[0] = 0;
}

/* ------------------------------------------------- copy, move, delete */

static int copy_file(const char *from, const char *to) {
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
        if (write(out, buf, (size_t)n) != n) { r = -ENOSPC; break; }
    }
    close(in);
    int c = close(out);
    return r ? r : c;
}

static int copy_tree(const char *from, const char *to, int *files) {
    struct itx_stat st;
    int r = stat(from, &st);
    if (r) return r;
    if (st.type != VT_DIR) {
        r = copy_file(from, to);
        if (!r) (*files)++;
        return r;
    }
    r = mkdir(to);
    if (r && r != -EEXIST) return r;
    int fd = open(from, O_RDONLY);
    if (fd < 0) return fd;
    struct itx_dirent de;
    while ((r = readdir(fd, &de)) == 1) {
        if (!strcmp(de.name, ".") || !strcmp(de.name, "..")) continue;
        char a[ITX_PATH_MAX], b[ITX_PATH_MAX];
        join(a, sizeof a, from, de.name);
        join(b, sizeof b, to, de.name);
        r = copy_tree(a, b, files);
        if (r) break;
    }
    close(fd);
    return r < 0 ? r : 0;
}

static int delete_tree(const char *path, int *files) {
    struct itx_stat st;
    int r = stat(path, &st);
    if (r) return r;
    if (st.type == VT_DIR) {
        for (;;) {                          /* the first entry, again and again: the folder changes */
            int fd = open(path, O_RDONLY);
            if (fd < 0) return fd;
            struct itx_dirent de;
            int got;
            do got = readdir(fd, &de);
            while (got == 1 && (!strcmp(de.name, ".") || !strcmp(de.name, "..")));
            close(fd);
            if (got < 0) return got;
            if (got == 0) break;
            char sub[ITX_PATH_MAX];
            join(sub, sizeof sub, path, de.name);
            r = delete_tree(sub, files);
            if (r) return r;
        }
    } else {
        (*files)++;
    }
    return remove(path);
}

/* where a copy or a move goes: into a folder that exists, else that name */
static void target(char *out, size_t len, const char *typed, const char *name) {
    char t[ITX_PATH_MAX];
    resolve(t, sizeof t, typed);
    struct itx_stat st;
    if (!stat(t, &st) && st.type == VT_DIR) join(out, len, t, name);
    else strlcpy(out, t, len);
}

static bool inside(const char *path, const char *dir) {
    size_t n = strlen(dir);
    return !strncmp(path, dir, n) && (path[n] == '/' || path[n] == 0);
}

static void do_copy(entry *e, bool move) {
    char typed[ITX_PATH_MAX], from[ITX_PATH_MAX], to[ITX_PATH_MAX];
    join(from, sizeof from, cwd, e->name);
    if (e->type == VT_DEV) { strcpy(message, "a USB device cannot be copied or moved"); return; }
    if (move && protected_path(from)) { snprintf(message, sizeof message, C_RED "itelx needs %s: it stays" C_RESET, from); return; }
    draw();
    printf("%s %s to (a folder, or a new name; e.g. /media/STICK): ", move ? "move" : "copy", e->name);
    if (!ui_ask("", typed, sizeof typed) || !typed[0]) { strcpy(message, "nothing done"); return; }
    target(to, sizeof to, typed, e->name);
    if (!strcmp(from, to)) { strcpy(message, "that is the same place"); return; }
    if (e->type == VT_DIR && inside(to, from)) { strcpy(message, C_RED "a folder cannot go into itself" C_RESET); return; }
    struct itx_stat st;
    if (!stat(to, &st)) {
        draw();
        printf("%s exists. Replace it? (y/n) ", to);
        flush();
        int k = ui_key();
        if (k != 'y' && k != 'Y') { strcpy(message, "nothing done"); return; }
        if (st.type == VT_DIR || e->type == VT_DIR) {
            int n = 0, r = delete_tree(to, &n);
            if (r) { snprintf(message, sizeof message, C_RED "%s: %s" C_RESET, to, strerror(r)); return; }
        }
    }
    int files = 0, r;
    if (move) {
        r = rename(from, to);
        if (r == -EXDEV || r == -ENOSYS) {                 /* another disk: copy, then delete */
            r = copy_tree(from, to, &files);
            if (!r) { int n = 0; r = delete_tree(from, &n); }
        }
    } else {
        r = copy_tree(from, to, &files);
    }
    sync();
    if (r) snprintf(message, sizeof message, C_RED "%s: %s" C_RESET, e->name, strerror(r));
    else snprintf(message, sizeof message, C_GREEN "%s %s to %s" C_RESET, move ? "moved" : "copied", e->name, to);
    load();
}

static void do_delete(entry *e) {
    char path[ITX_PATH_MAX];
    join(path, sizeof path, cwd, e->name);
    if (protected_path(path)) { snprintf(message, sizeof message, C_RED "itelx needs %s: it stays" C_RESET, path); return; }
    draw();
    if (e->type == VT_DIR) printf(C_YELLOW "delete the folder %s and everything in it? (y/n) " C_RESET, e->name);
    else printf(C_YELLOW "delete %s? (y/n) " C_RESET, e->name);
    flush();
    int k = ui_key();
    if (k != 'y' && k != 'Y') { strcpy(message, "nothing deleted"); return; }
    int files = 0, r = delete_tree(path, &files);
    sync();
    if (r) snprintf(message, sizeof message, C_RED "%s: %s" C_RESET, e->name, strerror(r));
    else snprintf(message, sizeof message, "deleted %s%s", e->name, e->type == VT_DIR ? " and what was in it" : "");
    load();
}

static void do_mkdir(void) {
    char name[ITX_NAME_MAX + 1], path[ITX_PATH_MAX];
    draw();
    if (!ui_ask("name of the new folder: ", name, sizeof name) || !name[0]) { strcpy(message, "nothing done"); return; }
    if (strchr(name, '/')) { strcpy(message, "a name without /"); return; }
    join(path, sizeof path, cwd, name);
    int r = mkdir(path);
    if (r) snprintf(message, sizeof message, C_RED "%s: %s" C_RESET, name, strerror(r));
    else { snprintf(message, sizeof message, "created %s/", name); load(); go(cwd, name); }
}

static void do_install(entry *e) {
    bool xvr = ends_with(e->name, ".xvr"), xvc = ends_with(e->name, ".xvc");
    if (!xvr && !xvc) { strcpy(message, "only programs (.xvr and .xvc) are installed"); return; }
    char from[ITX_PATH_MAX], to[ITX_PATH_MAX];
    join(from, sizeof from, cwd, e->name);
    join(to, sizeof to, "/dex", e->name);
    if (!strcmp(from, to)) { strcpy(message, "it is installed: this is /dex"); return; }
    if (xvr) {
        char why[96] = "";
        if (xvr_check(from, why, sizeof why)) { snprintf(message, sizeof message, C_RED "not installed: %s" C_RESET, why); return; }
    }
    int r = copy_file(from, to);
    sync();
    char name[ITX_NAME_MAX + 1];
    strlcpy(name, e->name, sizeof name);
    name[strlen(name) - 4] = 0;
    if (r) snprintf(message, sizeof message, C_RED "%s: %s" C_RESET, e->name, strerror(r));
    else snprintf(message, sizeof message, C_GREEN "installed %s - in the terminal: %s" C_RESET, to, name);
}

/* ------------------------------------------------------- open, view */

static void run(const char *prog, const char *arg1) {
    const char *args[3];
    int n = 0;
    const char *path = prog;
    if (ends_with(prog, ".xvc")) { path = "/system/xvc.xvr"; args[n++] = "xvc"; }
    args[n++] = prog;
    if (arg1) args[n++] = arg1;
    ui_end();
    printf("\x1b[2J");
    flush();
    int pid = spawn(path, args, n, SPAWN_FG);
    int status = 0;
    if (pid < 0) {
        char why[96] = "";
        last_error(why, sizeof why);
        printf(C_RED "%s: %s" C_RESET "\n", prog, why[0] ? why : strerror(pid));
    } else {
        waitpid(pid, &status, 0);
        tty_setfg(0);
    }
    printf("\n" C_DIM "[%s ended%s] a key goes back to fm" C_RESET, base_name(prog), status ? ", not with 0" : "");
    flush();
    ui_begin();
    ui_key();
    load();
}

static void view(const char *path, const char *name) {
    size_t len;
    char *data = read_file(path, &len, 4u << 20);
    if (!data) { snprintf(message, sizeof message, C_RED "%s: cannot read it (larger than 4 MiB?)" C_RESET, name); return; }
    for (size_t i = 0; i < len; i++)
        if (!data[i]) {
            free(data);
            snprintf(message, sizeof message, "%s is not a text file (%lu bytes)", name, (unsigned long)len);
            return;
        }
    /* the lines, in place */
    int nl = 1;
    for (size_t i = 0; i < len; i++) if (data[i] == '\n') nl++;
    char **ln = malloc(sizeof(char *) * (size_t)nl);
    if (!ln) { free(data); return; }
    int n = 0;
    ln[n++] = data;
    for (size_t i = 0; i < len; i++)
        if (data[i] == '\n') {
            data[i] = 0;
            if (i > 0 && data[i - 1] == '\r') data[i - 1] = 0;
            if (i + 1 < len) ln[n++] = data + i + 1;
        }
    int rows = list_rows() + 2, first = 0;
    for (;;) {
        if (first > n - rows) first = n - rows;
        if (first < 0) first = 0;
        ui_frame();
        ui_printf(C_CYAN "%s" C_RESET "   " C_DIM "lines %d-%d of %d   arrows/PgUp/PgDn/Home/End move, q back" C_RESET "\n",
                  name, first + 1, first + rows < n ? first + rows : n, n);
        for (int i = first; i < n && i < first + rows; i++) ui_printf(C_DIM "%5d" C_RESET "  %s\n", i + 1, ln[i]);
        ui_show();
        int k = ui_key();
        if (k < 0 || k == 'q' || k == 'Q' || k == 27 || k == '\b' || k == KEY_LEFT) break;
        if (k == KEY_DOWN || k == 'j' || k == '\n') first++;
        else if (k == KEY_UP || k == 'k') first--;
        else if (k == KEY_PGDN || k == ' ') first += rows;
        else if (k == KEY_PGUP) first -= rows;
        else if (k == KEY_HOME) first = 0;
        else if (k == KEY_END) first = n;
    }
    free(ln);
    free(data);
}

static void open_entry(entry *e) {
    char path[ITX_PATH_MAX];
    join(path, sizeof path, cwd, e->name);
    if (e->type == VT_DIR) go(path, NULL);
    else if (ends_with(e->name, ".xvr") || ends_with(e->name, ".xvc")) run(path, NULL);
    else if (e->type == VT_DEV) strcpy(message, "a USB device: nothing to open (ls /media shows them)");
    else view(path, e->name);
}

/* ------------------------------------------------------------- main */

int main(int argc, char **argv) {
    char start[ITX_PATH_MAX];
    getcwd(cwd, sizeof cwd);
    if (argc > 1) {
        resolve(start, sizeof start, argv[1]);
        struct itx_stat st;
        if (stat(start, &st) || st.type != VT_DIR) { printf("fm: %s: not a folder\n", argv[1]); return 2; }
    } else {
        strlcpy(start, cwd, sizeof start);
    }
    go(start, NULL);
    ui_begin();
    for (;;) {
        draw();
        int k = ui_key();
        if (k < 0 || k == 'q' || k == 'Q' || k == 27) break;
        int rows = list_rows();
        entry *e = nents ? &ents[sel] : NULL;
        if (k == KEY_DOWN || k == 'j') { if (sel + 1 < nents) sel++; }
        else if (k == KEY_UP || k == 'k') { if (sel > 0) sel--; }
        else if (k == KEY_PGDN) { sel += rows; if (sel >= nents) sel = nents ? nents - 1 : 0; }
        else if (k == KEY_PGUP) { sel -= rows; if (sel < 0) sel = 0; }
        else if (k == KEY_HOME) sel = 0;
        else if (k == KEY_END) sel = nents ? nents - 1 : 0;
        else if (k == '\b' || k == KEY_LEFT) {
            if (strcmp(cwd, "/")) {
                char up[ITX_PATH_MAX], was[ITX_NAME_MAX + 1];
                strlcpy(up, cwd, sizeof up);
                strlcpy(was, base_name(cwd), sizeof was);
                parent(up);
                go(up, was);
            }
        } else if (k == '\n' || k == KEY_RIGHT) { if (e) open_entry(e); }
        else if (k == 'c' || k == 'C') { if (e) do_copy(e, false); }
        else if (k == 'm' || k == 'M') { if (e) do_copy(e, true); }
        else if (k == 'd' || k == 'D' || k == KEY_DELETE) { if (e) do_delete(e); }
        else if (k == 'n' || k == 'N') do_mkdir();
        else if (k == 'i' || k == 'I') { if (e) do_install(e); }
        else if (k == 'e' || k == 'E') {
            struct itx_stat st;
            if (!e || e->type != VT_FILE) strcpy(message, "choose a file to edit");
            else if (stat("/dex/edit.xvr", &st)) strcpy(message, "edit is not installed (/dex/edit.xvr)");
            else { char p[ITX_PATH_MAX]; join(p, sizeof p, cwd, e->name); run("/dex/edit.xvr", p); }
        } else if (k == 'g' || k == 'G') {
            char typed[ITX_PATH_MAX], path[ITX_PATH_MAX];
            draw();
            if (ui_ask("go to: ", typed, sizeof typed) && typed[0]) { resolve(path, sizeof path, typed); go(path, NULL); }
        } else if (k == 'r' || k == 'R') { load(); strcpy(message, "refreshed"); }
        else strcpy(message, "arrows choose, Enter opens, q quits");
    }
    ui_end();
    printf("\x1b[2J");
    flush();
    return 0;
}
