/* XVC built-in functions. Each one checks its arguments' types and reports
 * a runtime error naming itself when they are wrong. */
#include "xvc.h"

#define ARGERR(fn, msg) do { vm_runtime_error(m, "%s(): %s", fn, msg); return v_nil(); } while (0)

static ostr *need_str(vm *m, const char *fn, value v, int n) {
    if (!is_obj(v, O_STR)) {
        vm_runtime_error(m, "%s(): argument %d must be a string, not %s", fn, n, type_name(v));
        return NULL;
    }
    return AS_STR(v);
}

static bool need_int(vm *m, const char *fn, value v, int n, int64_t *out) {
    if (v.t != V_INT) {
        vm_runtime_error(m, "%s(): argument %d must be an int, not %s", fn, n, type_name(v));
        return false;
    }
    *out = v.as.i;
    return true;
}

static value mkstr(vm *m, const char *s, size_t n) {
    ostr *o = str_new(m, s, n);
    return o ? v_obj(o) : v_nil();
}

static void print_values(int argc, value *argv) {
    static char buf[1 << 16];
    for (int i = 0; i < argc; i++) {
        if (is_obj(argv[i], O_STR)) {
            flush();
            write(1, AS_STR(argv[i])->s, AS_STR(argv[i])->len);
        } else {
            int n = value_to_string(argv[i], buf, sizeof buf, false);
            if (n >= (int)sizeof buf) n = sizeof buf - 1;
            flush();
            write(1, buf, (size_t)n);
        }
        if (i + 1 < argc) write(1, " ", 1);
    }
}

static value n_print(vm *m, int argc, value *argv) { print_values(argc, argv); write(1, "\n", 1); return v_nil(); }
static value n_write(vm *m, int argc, value *argv) { print_values(argc, argv); return v_nil(); }

static value n_input(vm *m, int argc, value *argv) {
    if (argc) print_values(1, argv);
    char buf[4096];
    ssize_t n = read(0, buf, sizeof buf);
    if (n <= 0) return v_nil();
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) n--;
    return mkstr(m, buf, (size_t)n);
}

static value n_len(vm *m, int argc, value *argv) {
    value v = argv[0];
    if (is_obj(v, O_STR)) return v_int((int64_t)AS_STR(v)->chars);
    if (is_obj(v, O_LIST)) return v_int((int64_t)AS_LIST(v)->len);
    if (is_obj(v, O_MAP)) return v_int((int64_t)AS_MAP(v)->count);
    if (is_obj(v, O_RANGE)) {
        orange *r = (orange *)v.as.o;
        int64_t n = r->step > 0 ? (r->end - r->start + r->step - 1) / r->step : (r->start - r->end - r->step - 1) / -r->step;
        return v_int(n < 0 ? 0 : n);
    }
    vm_runtime_error(m, "len(): a %s has no length", type_name(v));
    return v_nil();
}

static value n_str(vm *m, int argc, value *argv) {
    if (is_obj(argv[0], O_STR)) return argv[0];
    char *buf = malloc(1 << 16);
    if (!buf) ARGERR("str", "out of memory");
    int n = value_to_string(argv[0], buf, 1 << 16, true);
    if (n >= (1 << 16)) n = (1 << 16) - 1;
    value r = mkstr(m, buf, (size_t)n);
    free(buf);
    return r;
}

static value n_int(vm *m, int argc, value *argv) {
    value v = argv[0];
    if (v.t == V_INT) return v;
    if (v.t == V_BOOL) return v_int(v.as.b);
    if (is_obj(v, O_STR)) {
        const char *s = AS_STR(v)->s;
        while (isspace((unsigned char)*s)) s++;
        if (!*s) return v_nil();
        char *end;
        long r = strtol(s, &end, 0);
        while (isspace((unsigned char)*end)) end++;
        if (*end) return v_nil();                      /* not a number */
        return v_int(r);
    }
    vm_runtime_error(m, "int(): cannot convert a %s", type_name(v));
    return v_nil();
}

static value n_type(vm *m, int argc, value *argv) { const char *t = type_name(argv[0]); return mkstr(m, t, strlen(t)); }

static value n_chr(vm *m, int argc, value *argv) {
    int64_t c;
    if (!need_int(m, "chr", argv[0], 1, &c)) return v_nil();
    if (c < 0 || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) ARGERR("chr", "not a Unicode code point");
    char b[4];
    return mkstr(m, b, (size_t)utf8_encode((uint32_t)c, b));
}

static value n_ord(vm *m, int argc, value *argv) {
    ostr *s = need_str(m, "ord", argv[0], 1);
    if (!s) return v_nil();
    if (!s->len) ARGERR("ord", "empty string");
    uint32_t cp;
    utf8_decode(s->s, s->len, &cp);
    return v_int(cp);
}

static value n_case(vm *m, value v, bool up, const char *fn) {
    ostr *s = need_str(m, fn, v, 1);
    if (!s) return v_nil();
    char *b = malloc(s->len + 1);
    if (!b) ARGERR(fn, "out of memory");
    for (size_t i = 0; i < s->len; i++) b[i] = (char)(up ? toupper((unsigned char)s->s[i]) : tolower((unsigned char)s->s[i]));
    value r = mkstr(m, b, s->len);
    free(b);
    return r;
}
static value n_upper(vm *m, int argc, value *argv) { return n_case(m, argv[0], true, "upper"); }
static value n_lower(vm *m, int argc, value *argv) { return n_case(m, argv[0], false, "lower"); }

static value n_trim(vm *m, int argc, value *argv) {
    ostr *s = need_str(m, "trim", argv[0], 1);
    if (!s) return v_nil();
    size_t a = 0, b = s->len;
    while (a < b && isspace((unsigned char)s->s[a])) a++;
    while (b > a && isspace((unsigned char)s->s[b - 1])) b--;
    return mkstr(m, s->s + a, b - a);
}

/* character index -> byte offset */
static size_t coff(ostr *s, int64_t ci) {
    size_t o = 0;
    int64_t c = 0;
    for (; o < s->len; o++) {
        if (((uint8_t)s->s[o] & 0xC0) != 0x80) { if (c == ci) return o; c++; }
    }
    return s->len;
}

static value n_substr(vm *m, int argc, value *argv) {
    ostr *s = need_str(m, "substr", argv[0], 1);
    int64_t start, count = INT64_MAX;
    if (!s || !need_int(m, "substr", argv[1], 2, &start)) return v_nil();
    if (argc > 2 && !need_int(m, "substr", argv[2], 3, &count)) return v_nil();
    if (start < 0) start += (int64_t)s->chars;
    if (start < 0) start = 0;
    if (start > (int64_t)s->chars) start = (int64_t)s->chars;
    if (count < 0) count = 0;
    int64_t end = count > (int64_t)s->chars - start ? (int64_t)s->chars : start + count;
    size_t a = coff(s, start), b = coff(s, end);
    return mkstr(m, s->s + a, b - a);
}

static value n_find(vm *m, int argc, value *argv) {
    ostr *s = need_str(m, "find", argv[0], 1), *t = need_str(m, "find", argv[1], 2);
    if (!s || !t) return v_nil();
    const char *p = strstr(s->s, t->s);
    if (!p) return v_int(-1);
    int64_t ci = 0;
    for (const char *q = s->s; q < p; q++) if (((uint8_t)*q & 0xC0) != 0x80) ci++;
    return v_int(ci);
}

static value n_replace(vm *m, int argc, value *argv) {
    ostr *s = need_str(m, "replace", argv[0], 1), *a = need_str(m, "replace", argv[1], 2), *b = need_str(m, "replace", argv[2], 3);
    if (!s || !a || !b) return v_nil();
    if (!a->len) return argv[0];
    size_t cap = s->len + 16, len = 0;
    char *out = malloc(cap);
    if (!out) ARGERR("replace", "out of memory");
    for (size_t i = 0; i < s->len;) {
        bool hit = i + a->len <= s->len && !memcmp(s->s + i, a->s, a->len);
        const char *src = hit ? b->s : s->s + i;
        size_t n = hit ? b->len : 1;
        if (len + n + 1 > cap) {
            cap = (len + n + 1) * 2;
            if (cap > (16u << 20)) { free(out); ARGERR("replace", "result too large"); }
            char *no = realloc(out, cap);
            if (!no) { free(out); ARGERR("replace", "out of memory"); }
            out = no;
        }
        memcpy(out + len, src, n);
        len += n;
        i += hit ? a->len : 1;
    }
    value r = mkstr(m, out, len);
    free(out);
    return r;
}

static value n_split(vm *m, int argc, value *argv) {
    ostr *s = need_str(m, "split", argv[0], 1);
    if (!s) return v_nil();
    ostr *sep = argc > 1 ? need_str(m, "split", argv[1], 2) : NULL;
    if (argc > 1 && !sep) return v_nil();
    olist *l = list_new(m);
    if (!l) ARGERR("split", "out of memory");
    const char *p = s->s, *end = s->s + s->len;
    if (!sep || !sep->len) {
        /* on runs of white space */
        while (p < end) {
            while (p < end && isspace((unsigned char)*p)) p++;
            const char *w = p;
            while (p < end && !isspace((unsigned char)*p)) p++;
            if (p > w && !list_push(m, l, mkstr(m, w, (size_t)(p - w)))) ARGERR("split", "out of memory");
        }
    } else {
        for (;;) {
            const char *q = strstr(p, sep->s);
            if (!q || q > end) q = end;
            if (!list_push(m, l, mkstr(m, p, (size_t)(q - p)))) ARGERR("split", "out of memory");
            if (q >= end) break;
            p = q + sep->len;
        }
    }
    return v_obj(l);
}

static value n_join(vm *m, int argc, value *argv) {
    if (!is_obj(argv[0], O_LIST)) ARGERR("join", "argument 1 must be a list");
    ostr *sep = argc > 1 ? need_str(m, "join", argv[1], 2) : NULL;
    if (argc > 1 && !sep) return v_nil();
    olist *l = AS_LIST(argv[0]);
    size_t cap = 256, len = 0;
    char *out = malloc(cap);
    char tmp[64];
    if (!out) ARGERR("join", "out of memory");
    for (size_t i = 0; i < l->len; i++) {
        const char *s;
        size_t n;
        if (is_obj(l->v[i], O_STR)) { s = AS_STR(l->v[i])->s; n = AS_STR(l->v[i])->len; }
        else { n = (size_t)value_to_string(l->v[i], tmp, sizeof tmp, false); s = tmp; if (n >= sizeof tmp) n = sizeof tmp - 1; }
        size_t extra = n + (sep && i + 1 < l->len ? sep->len : 0);
        if (len + extra + 1 > cap) {
            cap = (len + extra + 1) * 2;
            if (cap > (16u << 20)) { free(out); ARGERR("join", "result too large"); }
            char *no = realloc(out, cap);
            if (!no) { free(out); ARGERR("join", "out of memory"); }
            out = no;
        }
        memcpy(out + len, s, n);
        len += n;
        if (sep && i + 1 < l->len) { memcpy(out + len, sep->s, sep->len); len += sep->len; }
    }
    value r = mkstr(m, out, len);
    free(out);
    return r;
}

static value n_startswith(vm *m, int argc, value *argv) {
    ostr *s = need_str(m, "startswith", argv[0], 1), *t = need_str(m, "startswith", argv[1], 2);
    if (!s || !t) return v_nil();
    return v_bool(t->len <= s->len && !memcmp(s->s, t->s, t->len));
}

static value n_endswith(vm *m, int argc, value *argv) {
    ostr *s = need_str(m, "endswith", argv[0], 1), *t = need_str(m, "endswith", argv[1], 2);
    if (!s || !t) return v_nil();
    return v_bool(t->len <= s->len && !memcmp(s->s + s->len - t->len, t->s, t->len));
}

/* ---------------------------------------------------------------- lists */

static value n_push(vm *m, int argc, value *argv) {
    if (!is_obj(argv[0], O_LIST)) ARGERR("push", "argument 1 must be a list");
    if (!list_push(m, AS_LIST(argv[0]), argv[1])) ARGERR("push", "list too large or out of memory");
    return argv[0];
}

static value n_pop(vm *m, int argc, value *argv) {
    if (!is_obj(argv[0], O_LIST)) ARGERR("pop", "argument 1 must be a list");
    olist *l = AS_LIST(argv[0]);
    if (!l->len) ARGERR("pop", "the list is empty");
    return l->v[--l->len];
}

static value n_insert(vm *m, int argc, value *argv) {
    if (!is_obj(argv[0], O_LIST)) ARGERR("insert", "argument 1 must be a list");
    int64_t i;
    if (!need_int(m, "insert", argv[1], 2, &i)) return v_nil();
    olist *l = AS_LIST(argv[0]);
    if (i < 0 || i > (int64_t)l->len) ARGERR("insert", "position out of range");
    if (!list_push(m, l, v_nil())) ARGERR("insert", "out of memory");
    memmove(&l->v[i + 1], &l->v[i], (l->len - 1 - (size_t)i) * sizeof(value));
    l->v[i] = argv[2];
    return argv[0];
}

static value n_removeat(vm *m, int argc, value *argv) {
    if (!is_obj(argv[0], O_LIST)) ARGERR("removeat", "argument 1 must be a list");
    int64_t i;
    if (!need_int(m, "removeat", argv[1], 2, &i)) return v_nil();
    olist *l = AS_LIST(argv[0]);
    if (i < 0) i += (int64_t)l->len;
    if (i < 0 || i >= (int64_t)l->len) ARGERR("removeat", "position out of range");
    value v = l->v[i];
    memmove(&l->v[i], &l->v[i + 1], (l->len - 1 - (size_t)i) * sizeof(value));
    l->len--;
    return v;
}

static int cmp_values(const value *a, const value *b, bool *bad) {
    if (a->t == V_INT && b->t == V_INT) return a->as.i < b->as.i ? -1 : a->as.i > b->as.i;
    if (is_obj(*a, O_STR) && is_obj(*b, O_STR)) return strcmp(AS_STR(*a)->s, AS_STR(*b)->s);
    *bad = true;
    return 0;
}

static value n_sort(vm *m, int argc, value *argv) {
    if (!is_obj(argv[0], O_LIST)) ARGERR("sort", "argument 1 must be a list");
    olist *l = AS_LIST(argv[0]);
    bool bad = false;
    /* insertion sort for short lists, else a merge sort */
    value *tmp = malloc(l->len * sizeof(value) + 1);
    if (!tmp) ARGERR("sort", "out of memory");
    for (size_t width = 1; width < l->len && !bad; width *= 2) {
        for (size_t lo = 0; lo < l->len; lo += 2 * width) {
            size_t mid = lo + width < l->len ? lo + width : l->len, hi = lo + 2 * width < l->len ? lo + 2 * width : l->len;
            size_t i = lo, j = mid, k = lo;
            while (i < mid && j < hi) tmp[k++] = cmp_values(&l->v[j], &l->v[i], &bad) < 0 ? l->v[j++] : l->v[i++];
            while (i < mid) tmp[k++] = l->v[i++];
            while (j < hi) tmp[k++] = l->v[j++];
        }
        memcpy(l->v, tmp, l->len * sizeof(value));
    }
    free(tmp);
    if (bad) ARGERR("sort", "can only sort a list of ints or a list of strings");
    return argv[0];
}

static value n_reverse_list(vm *m, int argc, value *argv) {
    if (!is_obj(argv[0], O_LIST)) ARGERR("reverselist", "argument 1 must be a list");
    olist *l = AS_LIST(argv[0]);
    for (size_t i = 0, j = l->len ? l->len - 1 : 0; i < j; i++, j--) { value t = l->v[i]; l->v[i] = l->v[j]; l->v[j] = t; }
    return argv[0];
}

static value n_keys(vm *m, int argc, value *argv) {
    if (!is_obj(argv[0], O_MAP)) ARGERR("keys", "argument 1 must be a map");
    omap *mp = AS_MAP(argv[0]);
    olist *l = list_new(m);
    if (!l) ARGERR("keys", "out of memory");
    for (size_t i = 0; i < mp->cap; i++) if (mp->slots[i].used) list_push(m, l, mp->slots[i].key);
    return v_obj(l);
}

static value n_has(vm *m, int argc, value *argv) {
    if (is_obj(argv[0], O_MAP)) { value v; return v_bool(map_get(AS_MAP(argv[0]), argv[1], &v)); }
    if (is_obj(argv[0], O_LIST)) {
        olist *l = AS_LIST(argv[0]);
        for (size_t i = 0; i < l->len; i++) if (values_equal(l->v[i], argv[1])) return v_bool(true);
        return v_bool(false);
    }
    if (is_obj(argv[0], O_STR) && is_obj(argv[1], O_STR)) return v_bool(strstr(AS_STR(argv[0])->s, AS_STR(argv[1])->s) != NULL);
    ARGERR("has", "argument 1 must be a map, a list or a string");
}

static value n_range(vm *m, int argc, value *argv) {
    int64_t a = 0, b, step = 1;
    if (argc == 1) { if (!need_int(m, "range", argv[0], 1, &b)) return v_nil(); }
    else {
        if (!need_int(m, "range", argv[0], 1, &a) || !need_int(m, "range", argv[1], 2, &b)) return v_nil();
        if (argc > 2 && !need_int(m, "range", argv[2], 3, &step)) return v_nil();
    }
    if (step == 0) ARGERR("range", "the step cannot be 0");
    orange *r = vm_alloc(m, sizeof(orange), O_RANGE);
    if (!r) ARGERR("range", "out of memory");
    r->start = a; r->end = b; r->step = step;
    return v_obj(r);
}

/* ---------------------------------------------------------------- files */

static value n_readfile(vm *m, int argc, value *argv) {
    ostr *p = need_str(m, "readfile", argv[0], 1);
    if (!p) return v_nil();
    size_t len;
    char *d = read_file(p->s, &len, 16u << 20);
    if (!d) return v_nil();
    value r = mkstr(m, d, len);
    free(d);
    return r;
}

static value write_common(vm *m, value *argv, bool append, const char *fn) {
    ostr *p = need_str(m, fn, argv[0], 1);
    if (!p) return v_nil();
    char *tmp = NULL;
    const char *data;
    size_t len;
    if (is_obj(argv[1], O_STR)) { data = AS_STR(argv[1])->s; len = AS_STR(argv[1])->len; }
    else {
        tmp = malloc(1 << 16);
        if (!tmp) ARGERR(fn, "out of memory");
        len = (size_t)value_to_string(argv[1], tmp, 1 << 16, false);
        data = tmp;
    }
    int r = write_file(p->s, data, len, append);
    free(tmp);
    return v_bool(r == 0);
}
static value n_writefile(vm *m, int argc, value *argv) { return write_common(m, argv, false, "writefile"); }
static value n_appendfile(vm *m, int argc, value *argv) { return write_common(m, argv, true, "appendfile"); }

static value n_exists(vm *m, int argc, value *argv) {
    ostr *p = need_str(m, "exists", argv[0], 1);
    if (!p) return v_nil();
    struct itx_stat st;
    return v_bool(stat(p->s, &st) == 0);
}

static value n_isdir(vm *m, int argc, value *argv) {
    ostr *p = need_str(m, "isdir", argv[0], 1);
    if (!p) return v_nil();
    struct itx_stat st;
    return v_bool(stat(p->s, &st) == 0 && st.type == VT_DIR);
}

static value n_filesize(vm *m, int argc, value *argv) {
    ostr *p = need_str(m, "filesize", argv[0], 1);
    if (!p) return v_nil();
    struct itx_stat st;
    if (stat(p->s, &st)) return v_nil();
    return v_int((int64_t)st.size);
}

static value n_listdir(vm *m, int argc, value *argv) {
    ostr *p = need_str(m, "listdir", argv[0], 1);
    if (!p) return v_nil();
    int fd = open(p->s, O_RDONLY);
    if (fd < 0) return v_nil();
    olist *l = list_new(m);
    if (!l) { close(fd); ARGERR("listdir", "out of memory"); }
    struct itx_dirent de;
    while (readdir(fd, &de) == 1) list_push(m, l, mkstr(m, de.name, de.name_len));
    close(fd);
    return v_obj(l);
}

static value n_removefile(vm *m, int argc, value *argv) {
    ostr *p = need_str(m, "removefile", argv[0], 1);
    if (!p) return v_nil();
    return v_bool(remove(p->s) == 0);
}

static value n_makedir(vm *m, int argc, value *argv) {
    ostr *p = need_str(m, "makedir", argv[0], 1);
    if (!p) return v_nil();
    return v_bool(mkdir(p->s) == 0);
}

/* ------------------------------------------------------------- programs */

static int start(vm *m, int argc, value *argv, const char *fn, bool fg) {
    ostr *p = need_str(m, fn, argv[0], 1);
    if (!p) return -1;
    const char *args[34];
    int n = 0;
    const char *prog = p->s;
    if (ends_with(p->s, ".xvc")) { prog = "/system/xvc.xvr"; args[n++] = "xvc"; }
    args[n++] = p->s;
    for (int i = 1; i < argc && n < 33; i++) {
        if (!is_obj(argv[i], O_STR)) { vm_runtime_error(m, "%s(): program arguments must be strings", fn); return -1; }
        args[n++] = AS_STR(argv[i])->s;
    }
    flush();
    return spawn(prog, args, n, fg ? SPAWN_FG : 0);
}

static value n_run(vm *m, int argc, value *argv) {
    int pid = start(m, argc, argv, "run", true);
    if (m->failed) return v_nil();
    if (pid < 0) return v_int(pid);
    int status = 0;
    waitpid(pid, &status, 0);
    tty_setfg(0);
    return v_int(status);
}

static value n_spawn(vm *m, int argc, value *argv) {
    int pid = start(m, argc, argv, "spawn", false);
    return m->failed ? v_nil() : v_int(pid);
}

static value n_wait(vm *m, int argc, value *argv) {
    int64_t pid;
    if (!need_int(m, "wait", argv[0], 1, &pid)) return v_nil();
    int status = 0;
    int r = waitpid((int)pid, &status, 0);
    return r < 0 ? v_int(r) : v_int(status);
}

static value n_kill(vm *m, int argc, value *argv) {
    int64_t pid;
    if (!need_int(m, "kill", argv[0], 1, &pid)) return v_nil();
    return v_bool(kill((int)pid) == 0);
}

static value n_sleep(vm *m, int argc, value *argv) {
    int64_t ms;
    if (!need_int(m, "sleep", argv[0], 1, &ms)) return v_nil();
    flush();
    if (ms > 0) sleep_ms(ms);
    return v_nil();
}

static value n_time(vm *m, int argc, value *argv) { return v_int(unix_time()); }
static value n_uptime(vm *m, int argc, value *argv) { return v_int(uptime_ms()); }
static value n_pid(vm *m, int argc, value *argv) { return v_int(getpid()); }

static value n_random(vm *m, int argc, value *argv) {
    int64_t n;
    if (!need_int(m, "random", argv[0], 1, &n)) return v_nil();
    if (n <= 0) ARGERR("random", "the limit must be positive");
    m->rng = m->rng * 6364136223846793005ULL + 1442695040888963407ULL;
    return v_int((int64_t)((m->rng >> 33) % (uint64_t)n));
}

static value n_exit(vm *m, int argc, value *argv) {
    int64_t c = 0;
    if (argc && !need_int(m, "exit", argv[0], 1, &c)) return v_nil();
    m->exit_code = (int)c;
    m->exiting = true;
    return v_nil();
}

static value n_assert(vm *m, int argc, value *argv) {
    bool ok = !(argv[0].t == V_NIL || (argv[0].t == V_BOOL && !argv[0].as.b));
    if (!ok) vm_runtime_error(m, "assertion failed%s%s", argc > 1 && is_obj(argv[1], O_STR) ? ": " : "",
                              argc > 1 && is_obj(argv[1], O_STR) ? AS_STR(argv[1])->s : "");
    return v_nil();
}

static value n_error(vm *m, int argc, value *argv) {
    vm_runtime_error(m, "%s", is_obj(argv[0], O_STR) ? AS_STR(argv[0])->s : "error");
    return v_nil();
}

static value n_clear(vm *m, int argc, value *argv) { flush(); write(1, "\x1b[2J", 4); return v_nil(); }

static value n_color(vm *m, int argc, value *argv) {
    ostr *s = need_str(m, "color", argv[0], 1);
    if (!s) return v_nil();
    static const struct { const char *n, *code; } t[] = {
        {"white", "\x1b[0m"}, {"yellow", "\x1b[93m"}, {"red", "\x1b[91m"}, {"green", "\x1b[92m"},
        {"cyan", "\x1b[96m"}, {"dim", "\x1b[90m"}, {"reset", "\x1b[0m"},
    };
    for (size_t i = 0; i < sizeof t / sizeof t[0]; i++)
        if (!strcmp(s->s, t[i].n)) return mkstr(m, t[i].code, strlen(t[i].code));
    ARGERR("color", "unknown colour (white, yellow, red, green, cyan, dim)");
}

static value n_key(vm *m, int argc, value *argv) {
    flush();
    tty_mode(TTY_MODE_RAW);
    struct itx_key k;
    int r = tty_key(&k);
    tty_mode(TTY_MODE_COOKED);
    if (r < 0) return v_nil();
    static const char *names[] = { "", "up", "down", "left", "right", "home", "end", "delete", "pgup", "pgdn", "esc" };
    if (k.code >= 0x110000) {
        uint32_t i = k.code - 0x110000;
        if (i < sizeof names / sizeof names[0]) return mkstr(m, names[i], strlen(names[i]));
        char b[8];
        snprintf(b, sizeof b, "f%u", k.code - KEY_F1 + 1);
        return mkstr(m, b, strlen(b));
    }
    if (k.code == '\n') return mkstr(m, "enter", 5);
    if (k.code == '\b') return mkstr(m, "backspace", 9);
    if (k.code == 0x1B) return mkstr(m, "esc", 3);
    char b[4];
    return mkstr(m, b, (size_t)utf8_encode(k.code, b));
}

/* ---------------------------------------------------------------- table */

static const struct { const char *name; native_fn fn; int min, max; } natives[] = {
    {"print", n_print, 0, -1}, {"write", n_write, 0, -1}, {"input", n_input, 0, 1},
    {"len", n_len, 1, 1}, {"str", n_str, 1, 1}, {"int", n_int, 1, 1}, {"type", n_type, 1, 1},
    {"chr", n_chr, 1, 1}, {"ord", n_ord, 1, 1}, {"upper", n_upper, 1, 1}, {"lower", n_lower, 1, 1},
    {"trim", n_trim, 1, 1}, {"substr", n_substr, 2, 3}, {"find", n_find, 2, 2}, {"replace", n_replace, 3, 3},
    {"split", n_split, 1, 2}, {"join", n_join, 1, 2}, {"startswith", n_startswith, 2, 2}, {"endswith", n_endswith, 2, 2},
    {"push", n_push, 2, 2}, {"pop", n_pop, 1, 1}, {"insert", n_insert, 3, 3}, {"removeat", n_removeat, 2, 2},
    {"sort", n_sort, 1, 1}, {"reverselist", n_reverse_list, 1, 1}, {"keys", n_keys, 1, 1}, {"has", n_has, 2, 2},
    {"range", n_range, 1, 3},
    {"readfile", n_readfile, 1, 1}, {"writefile", n_writefile, 2, 2}, {"appendfile", n_appendfile, 2, 2},
    {"exists", n_exists, 1, 1}, {"isdir", n_isdir, 1, 1}, {"filesize", n_filesize, 1, 1},
    {"listdir", n_listdir, 1, 1}, {"removefile", n_removefile, 1, 1}, {"makedir", n_makedir, 1, 1},
    {"run", n_run, 1, 33}, {"spawn", n_spawn, 1, 33}, {"wait", n_wait, 1, 1}, {"kill", n_kill, 1, 1},
    {"sleep", n_sleep, 1, 1}, {"time", n_time, 0, 0}, {"uptime", n_uptime, 0, 0}, {"pid", n_pid, 0, 0},
    {"random", n_random, 1, 1}, {"exit", n_exit, 0, 1}, {"assert", n_assert, 1, 2}, {"error", n_error, 1, 1},
    {"clear", n_clear, 0, 0}, {"color", n_color, 1, 1}, {"key", n_key, 0, 0},
};

void vm_define_natives(vm *m, int argc, char **argv) {
    for (size_t i = 0; i < sizeof natives / sizeof natives[0]; i++) {
        onative *n = vm_alloc(m, sizeof(onative), O_NATIVE);
        ostr *name = str_new(m, natives[i].name, strlen(natives[i].name));
        if (!n || !name) return;
        n->name = natives[i].name;
        n->fn = natives[i].fn;
        n->min = natives[i].min;
        n->max = natives[i].max;
        map_set(m, m->globals, v_obj(name), v_obj(n));
    }
    olist *args = list_new(m);
    for (int i = 0; i < argc; i++) list_push(m, args, v_obj(str_new(m, argv[i], strlen(argv[i]))));
    map_set(m, m->globals, v_obj(str_new(m, "args", 4)), v_obj(args));
}
