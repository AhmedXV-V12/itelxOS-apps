/* vault: the organizer of File saver's vault, full screen.
 *
 *   arrows, PgUp/PgDn  choose          Enter  open the folder / show the file
 *   Backspace, Left    the folder above
 *   c  copy in from a USB stick        e  copy out to a USB stick
 *   n  new folder   r  rename   m  move   d  to the trash   t  the trash
 *   l  lock / unlock (encrypt)          i  details (SHA-256, versions)
 *   o  sort the Inbox   s  sort by name, size, date, kind   /  find
 *   k  check every file   b  back up to a USB stick   Esc  back to the terminal
 */
#include <vault.h>

struct ent { char name[256]; uint32_t type; uint64_t size, mtime; };

static char cwd[ITX_PATH_MAX] = VAULT;
static struct ent *ents;
static int nents, cap, sortby;
static char filter[64];

static const char *const sorts[] = { "name", "size", "date", "kind" };

static int cmp(const void *a, const void *b) {
    const struct ent *x = a, *y = b;
    if ((x->type == VT_DIR) != (y->type == VT_DIR)) return x->type == VT_DIR ? -1 : 1;
    if (sortby == 1 && x->size != y->size) return x->size > y->size ? -1 : 1;
    if (sortby == 2 && x->mtime != y->mtime) return x->mtime > y->mtime ? -1 : 1;
    if (sortby == 3) {
        const char *ex = strrchr(x->name, '.'), *ey = strrchr(y->name, '.');
        int c = strcasecmp(ex ? ex : "", ey ? ey : "");
        if (c) return c;
    }
    return strcasecmp(x->name, y->name);
}

static void load(const char *dir, bool hidden) {
    nents = 0;
    int fd = open(dir, O_RDONLY);
    if (fd < 0) return;
    struct itx_dirent de;
    while (readdir(fd, &de) == 1) {
        if (!hidden && vault_hidden(de.name)) continue;
        if (ends_with(de.name, ".part") || ends_with(de.name, ".from")) continue;
        if (filter[0] && de.type != VT_DIR && !k_strcasestr(de.name, filter)) continue;
        if (nents == cap) { cap = cap ? cap * 2 : 128; ents = realloc(ents, sizeof *ents * cap); }
        struct ent *e = &ents[nents++];
        strlcpy(e->name, de.name, sizeof e->name);
        e->type = de.type;
        e->size = de.size;
        char p[ITX_PATH_MAX];
        struct itx_stat st;
        snprintf(p, sizeof p, "%s/%s", dir, de.name);
        e->mtime = stat(p, &st) == 0 ? st.mtime : 0;
    }
    close(fd);
    qsort(ents, (size_t)nents, sizeof *ents, cmp);
}

static void human(uint64_t n, char *out, size_t len) {
    if (n < 1024) snprintf(out, len, "%lu B", (unsigned long)n);
    else if (n < 1048576) snprintf(out, len, "%lu KiB", (unsigned long)(n / 1024));
    else if (n < 1073741824ULL) snprintf(out, len, "%lu MiB", (unsigned long)(n >> 20));
    else snprintf(out, len, "%lu GiB", (unsigned long)(n >> 30));
}

static void parent(char *p) {
    char *s = strrchr(p, '/');
    if (s && s != p) *s = 0;
}

static bool ask_password(void) {
    if (vault_unlocked()) return true;
    if (!vault_has_password()) { k_flash("set a vault password first (in the terminal: password)"); return false; }
    char p[128];
    k_frame();
    k_header("vault - password", NULL);
    k_footer("");
    /* the line is typed with the terminal's editing; its keys show as dots there */
    k_end();
    printf("\r\x1b[K" K_YEL "vault password: " K_RESET);
    flush();
    tty_mode(TTY_MODE_RAW);
    size_t n = 0;
    bool ok = false;
    for (;;) {
        struct itx_key k;
        if (tty_key(&k) < 0) break;
        if (k.code == '\n' || k.code == '\r') { ok = true; break; }
        if (k.code == KEY_ESC) break;
        if ((k.code == '\b' || k.code == 0x7F) && n) { n--; printf("\b \b"); flush(); continue; }
        if (k.code >= 0x20 && k.code < 0x110000 && n + 5 < sizeof p) { n += (size_t)utf8_encode(k.code, p + n); printf("*"); flush(); }
    }
    p[n] = 0;
    k_begin();
    if (!ok) return false;
    k_frame();
    k_header("vault", NULL);
    k_pad_to(k_rows / 2);
    k_line("        opening the vault key ...");
    k_show();
    int r = vault_open(p);
    memset(p, 0, sizeof p);
    if (r == -EACCES) { k_flash("wrong password"); return false; }
    if (r) { k_flash("the vault key: %s", strerror(r)); return false; }
    return true;
}

/* a file or folder on a USB stick, chosen with the keys; false on Esc */
static bool pick_from_stick(char *out, size_t len) {
    char vols[8][64];
    int n = k_sticks(vols, 8);
    if (!n) { k_flash("plug in a USB stick (FAT32) first"); return false; }
    char dir[ITX_PATH_MAX];
    if (n == 1) strlcpy(dir, vols[0], sizeof dir);
    else {
        const char *items[8];
        for (int i = 0; i < n; i++) items[i] = vols[i];
        int c = k_menu("vault - copy in from which stick?", items, n, 0);
        if (c < 0) return false;
        strlcpy(dir, vols[c], sizeof dir);
    }
    char root[ITX_PATH_MAX];
    strlcpy(root, dir, sizeof root);
    struct k_list l = { 0, 0, 0 };
    for (;;) {
        load(dir, true);
        l.n = nents;
        k_frame();
        k_header("vault - copy in", dir);
        k_line(" " K_DIM "Enter: a file is copied in, a folder is opened.  a: the whole folder you are in." K_RESET);
        k_rule();
        int rows = k_rows - 5;
        k_list_fit(&l, rows);
        for (int i = l.top; i < nents && i < l.top + rows; i++) {
            char h[32] = "", nm[300];
            if (ents[i].type != VT_DIR) human(ents[i].size, h, sizeof h);
            k_fit(nm, sizeof nm, ents[i].name, k_cols - 20);
            k_line("%s %9s  %s%s%s" K_RESET, i == l.sel ? K_YEL "\xe2\x96\xb6" : " ", h, ents[i].type == VT_DIR ? K_CYAN : "", nm,
                   ents[i].type == VT_DIR ? "/" : "");
        }
        if (!nents) k_line("   " K_DIM "(empty)" K_RESET);
        k_footer("Enter take or open   Backspace up   a this whole folder   Esc cancel");
        k_show();
        int k = k_key();
        if (k_list_key(&l, k, rows)) continue;
        if (k == K_ESC) return false;
        if (k == 'a') { strlcpy(out, dir, len); return true; }
        if ((k == K_BS || k == KEY_LEFT) && strcmp(dir, root)) { parent(dir); l.sel = 0; continue; }
        if (k == K_ENTER && nents) {
            char p[ITX_PATH_MAX];
            snprintf(p, sizeof p, "%s/%s", dir, ents[l.sel].name);
            if (ents[l.sel].type == VT_DIR) { strlcpy(dir, p, sizeof dir); l.sel = 0; continue; }
            strlcpy(out, p, len);
            return true;
        }
    }
}

static void show_file(const char *path, const struct ent *e) {
    size_t len;
    char *t = e->size <= 256 * 1024 ? read_file(path, &len, 256 * 1024) : NULL;
    bool text = t != NULL;
    for (size_t i = 0; t && i < len; i++) if (!t[i]) { text = false; break; }
    if (text) { k_message(path, t); free(t); return; }
    free(t);
    char m[512], h[32];
    human(e->size, h, sizeof h);
    snprintf(m, sizeof m, "%s\n\n%s, %s.\nNot a text file (or larger than 256 KiB): i shows its details, e copies it\n"
             "to a USB stick to open it on another computer.", path, h, vault_category(e->name));
    k_message("vault", m);
}

static void details(const char *path, const struct ent *e) {
    char buf[4096], h[32], hex[70];
    uint8_t sha[32];
    human(e->size, h, sizeof h);
    int o = snprintf(buf, sizeof buf, "%s\n\nsize        %s (%lu bytes)\nkind        %s\n", path, h, (unsigned long)e->size,
                     e->type == VT_DIR ? "folder" : vault_category(e->name));
    if (e->type != VT_DIR) {
        k_frame();
        k_header("vault - details", NULL);
        k_pad_to(k_rows / 2);
        k_line("        reading the file for its SHA-256 ...");
        k_show();
        if (!vault_sha256_file(path, sha, NULL)) { k_hex(hex, sha, 32); o += snprintf(buf + o, sizeof buf - o, "SHA-256     %s\n", hex); }
        o += snprintf(buf + o, sizeof buf - o, "locked      %s\n", ends_with(e->name, LOCKED_EXT) ? "yes (encrypted; l unlocks it)" : "no");
        char v[16][128];
        int n = vault_versions(path, v, 16);
        o += snprintf(buf + o, sizeof buf - o, "versions    %d earlier\n", n);
        for (int i = 0; i < n; i++) o += snprintf(buf + o, sizeof buf - o, "            " VVERS "%s\n", v[i]);
    }
    k_message("vault - details", buf);
}

static void trash_view(void) {
    struct k_list l = { 0, 0, 0 };
    for (;;) {
        load(VTRASH, true);
        l.n = nents;
        k_frame();
        k_header("vault - the trash", NULL);
        k_rule();
        int rows = k_rows - 4;
        k_list_fit(&l, rows);
        for (int i = l.top; i < nents && i < l.top + rows; i++) {
            char h[32], nm[300];
            human(ents[i].size, h, sizeof h);
            k_fit(nm, sizeof nm, ents[i].name, k_cols - 20);
            k_line("%s %9s  %s", i == l.sel ? K_YEL "\xe2\x96\xb6" : " ", h, nm);
        }
        if (!nents) k_line("   " K_DIM "the trash is empty" K_RESET);
        k_footer("Enter put it back   x empty the trash   Esc back");
        k_show();
        int k = k_key();
        if (k_list_key(&l, k, rows)) continue;
        if (k == K_ESC || k == 'q') return;
        if (k == K_ENTER && nents) {
            char where[ITX_PATH_MAX];
            int r = vault_trash_restore(ents[l.sel].name, where, sizeof where);
            k_flash(r ? "could not put it back: %s" : "back at %s", r ? strerror(r) : where);
        } else if (k == 'x' && nents && k_confirm("Delete everything in the trash for good?")) {
            int n;
            vault_trash_empty(&n);
            k_flash("%d deleted for good", n);
        }
    }
}

static char check_out[16384];
static size_t check_len;

static void check_report(const char *path, const char *what) {
    if (check_len < sizeof check_out - 600)
        check_len += (size_t)snprintf(check_out + check_len, sizeof check_out - check_len, "%-28s %s\n", what, path);
}

static void check_view(void) {
    k_frame();
    k_header("vault - check", NULL);
    k_pad_to(k_rows / 2);
    k_line("        reading every file of the vault and comparing its SHA-256 ...");
    k_show();
    check_len = 0;
    check_out[0] = 0;
    char *out = check_out;
    size_t o;
    struct vcheck r;
    vault_check(&r, check_report);
    o = check_len;
    char head[256];
    snprintf(head, sizeof head, "%d file(s) as indexed, %d changed, %d missing, %d new.\n%s\n", r.ok, r.changed, r.missing, r.added,
             r.changed || r.missing ? "Changed or missing files were not changed by File saver: check them."
                                    : "Every indexed file is exactly as it was.");
    char *all = malloc(strlen(head) + o + 2);
    strcpy(all, head);
    strcat(all, out);
    k_message("vault - check", all);
    free(all);
}

static void export_one(const char *path) {
    char vols[8][64];
    int n = k_sticks(vols, 8);
    if (!n) { k_flash("plug in a USB stick (FAT32) first"); return; }
    int c = 0;
    if (n > 1) {
        const char *items[8];
        for (int i = 0; i < n; i++) items[i] = vols[i];
        c = k_menu("vault - copy out to which stick?", items, n, 0);
        if (c < 0) return;
    }
    char dst[ITX_PATH_MAX];
    snprintf(dst, sizeof dst, "%s/vault-export", vols[c]);
    k_mkdirs(dst);
    int files;
    uint64_t bytes;
    /* vault_store copies anywhere when it is given the folder */
    char out[ITX_PATH_MAX];
    int r = vault_store(path, dst, out, sizeof out, &files, &bytes);
    sync();
    if (r) k_flash("could not copy: %s", strerror(r));
    else k_flash("copied to %s (%d file(s))", out, files);
}

int main(int argc, char **argv) {
    vault_init();
    k_begin();
    struct k_list l = { 0, 0, 0 };
    for (;;) {
        load(cwd, false);
        l.n = nents;
        k_frame();
        char right[64];
        snprintf(right, sizeof right, "sorted by %s%s%s", sorts[sortby], filter[0] ? ", only *" : "", filter[0] ? filter : "");
        k_header(cwd, right);
        if (!strcmp(cwd, VAULT)) k_line(" " K_DIM "Your vault. New files go to Inbox; o sorts them into their folders by kind." K_RESET);
        else k_line(" " K_DIM "Backspace: the folder above" K_RESET);
        k_rule();
        int rows = k_rows - 5;
        k_list_fit(&l, rows);
        for (int i = l.top; i < nents && i < l.top + rows; i++) {
            struct ent *e = &ents[i];
            char h[32] = "", d[24] = "", nm[300];
            if (e->type != VT_DIR) human(e->size, h, sizeof h);
            if (e->mtime) k_fmt_day(d, sizeof d, (long)((e->mtime + (uint64_t)k_tz_minutes() * 60) / 86400));
            bool locked = ends_with(e->name, LOCKED_EXT);
            char shown[300];
            snprintf(shown, sizeof shown, "%s%s", e->name, e->type == VT_DIR ? "/" : "");
            k_fit(nm, sizeof nm, shown, k_cols - 46);
            k_line("%s %-10s %9s  %-10s %s %s%s" K_RESET, i == l.sel ? K_YEL "\xe2\x96\xb6" : " ", e->type == VT_DIR ? "folder" : locked ? "locked" : vault_category(e->name),
                   h, d, K_DIM K_SEP K_RESET, e->type == VT_DIR ? K_CYAN : locked ? K_YEL : "", nm);
        }
        if (!nents) k_line("   " K_DIM "(empty: c copies files in from a USB stick)" K_RESET);
        k_footer("Enter open  c copy in  e copy out  n folder  r rename  m move  d trash  l lock  i details  o sort Inbox  s sort  / find  k check  t trash  b backup  Esc back");
        k_show();
        int k = k_key();
        if (k_list_key(&l, k, rows)) continue;
        struct ent *e = nents ? &ents[l.sel] : NULL;
        char path[ITX_PATH_MAX] = "";
        if (e) snprintf(path, sizeof path, "%s/%s", cwd, e->name);
        if (k == K_ESC || k == 'q') { k_end(); printf("\x1b[2J"); return 0; }
        if ((k == K_BS || k == KEY_LEFT) && strcmp(cwd, VAULT)) { parent(cwd); l.sel = 0; filter[0] = 0; continue; }
        if ((k == K_ENTER || k == KEY_RIGHT) && e) {
            if (e->type == VT_DIR) { strlcpy(cwd, path, sizeof cwd); l.sel = 0; filter[0] = 0; }
            else show_file(path, e);
            continue;
        }
        switch (k) {
        case 'c': {
            char from[ITX_PATH_MAX], out[ITX_PATH_MAX];
            if (!pick_from_stick(from, sizeof from)) break;
            k_frame();
            k_header("vault - copy in", NULL);
            k_pad_to(k_rows / 2);
            k_line("        copying %s ...", from);
            k_show();
            int files;
            uint64_t bytes;
            int r = vault_store(from, strcmp(cwd, VAULT) ? cwd : NULL, out, sizeof out, &files, &bytes);
            char h[32];
            human(bytes, h, sizeof h);
            if (r) k_flash("could not copy: %s", strerror(r));
            else if (files) k_flash("copied in: %s (%d file(s), %s)", out, files, h);
            else k_flash("already in the vault, unchanged: %s", out);
            break;
        }
        case 'e': if (e) export_one(path); break;
        case 'n': {
            char b[128], p[ITX_PATH_MAX];
            if (!k_ask("Name of the new folder: ", b, sizeof b) || strchr(b, '/')) break;
            snprintf(p, sizeof p, "%s/%s", cwd, b);
            int r = mkdir(p);
            if (r) k_flash("%s: %s", b, strerror(r));
            break;
        }
        case 'r': {
            char b[256], p[ITX_PATH_MAX];
            if (!e || !k_ask("New name: ", b, sizeof b) || strchr(b, '/')) break;
            snprintf(p, sizeof p, "%s/%s", cwd, b);
            int r = rename(path, p);
            if (r) k_flash("%s", strerror(r));
            else vault_index_update();
            break;
        }
        case 'm': {
            if (!e) break;
            const char *items[16];
            for (int i = 0; i < vault_nfolders; i++) items[i] = vault_folders[i];
            int c = k_menu("vault - move to which folder?", items, vault_nfolders, 0);
            if (c < 0) break;
            char to[ITX_PATH_MAX];
            snprintf(to, sizeof to, VAULT "/%s/%s", vault_folders[c], e->name);
            int r = rename(path, to);
            if (r) k_flash("%s", strerror(r));
            else { vault_index_update(); k_flash("moved to %s", to); }
            break;
        }
        case 'd': case KEY_DELETE:
            if (e && k_confirm("Move it to the trash?")) { int r = vault_trash(path); if (r) k_flash("%s", strerror(r)); }
            break;
        case 'l':
            if (!e || e->type == VT_DIR) { k_flash("choose a file (a folder: lock it in the terminal)"); break; }
            if (!ask_password()) break;
            {
                k_frame();
                k_header("vault", NULL);
                k_pad_to(k_rows / 2);
                k_line("        %s %s ...", ends_with(e->name, LOCKED_EXT) ? "decrypting" : "encrypting", e->name);
                k_show();
                int r = ends_with(e->name, LOCKED_EXT) ? vault_unlock_file(path) : vault_lock_file(path);
                if (r == -EACCES) k_flash("not this vault's key, or the file was changed");
                else if (r) k_flash("%s", strerror(r));
                else k_flash(ends_with(e->name, LOCKED_EXT) ? "unlocked" : "locked: only the vault password opens it");
            }
            break;
        case 'i': if (e) details(path, e); break;
        case 'o': {
            int moved;
            vault_organize(VAULT "/Inbox", &moved);
            k_flash("%d file(s) of the Inbox put into their folders", moved);
            break;
        }
        case 's': sortby = (sortby + 1) % 4; break;
        case '/': if (!k_ask("Only the names with: ", filter, sizeof filter)) filter[0] = 0; l.sel = 0; break;
        case 'k': check_view(); break;
        case 't': trash_view(); break;
        case 'b': {
            char vols[8][64];
            if (!k_sticks(vols, 8)) { k_flash("plug in a USB stick (FAT32) first"); break; }
            k_frame();
            k_header("vault - backup", vols[0]);
            k_pad_to(k_rows / 2);
            k_line("        copying what changed to %s/vault-backup ...", vols[0]);
            k_show();
            int copied, same;
            uint64_t bytes;
            int r = vault_backup(vols[0], &copied, &same, &bytes, NULL);
            char h[32];
            human(bytes, h, sizeof h);
            if (r) k_flash("backup stopped: %s", strerror(r));
            else k_flash("backup: %d file(s) copied (%s), %d were the same", copied, h, same);
            break;
        }
        }
    }
}
