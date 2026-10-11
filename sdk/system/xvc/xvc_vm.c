/* XVC virtual machine: objects, the garbage collector and the bytecode loop.
 * Every operation checks its operands; a bad operation stops the program
 * with a message naming the file and line, never the interpreter. */
#include "xvc.h"

static vm *gc_vm;

/* ------------------------------------------------------------- objects */

static void gc(vm *m);

void *vm_alloc(vm *m, size_t size, otype t) {
    if (!m->gc_off && m->allocated + size > m->next_gc) gc(m);
    obj *o = malloc(size);
    if (!o) {
        gc(m);
        o = malloc(size);
        if (!o) return NULL;
    }
    memset(o, 0, size);
    o->t = t;
    o->next = m->objects;
    m->objects = o;
    m->allocated += size;
    return o;
}

static uint32_t hash_bytes(const char *s, size_t n) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) { h ^= (uint8_t)s[i]; h *= 16777619u; }
    return h;
}

ostr *str_new(vm *m, const char *s, size_t len) {
    if (len > (16u << 20)) { vm_runtime_error(m, "string longer than 16 MiB"); return NULL; }
    ostr *o = vm_alloc(m, sizeof(ostr) + len + 1, O_STR);
    if (!o) return NULL;
    memcpy(o->s, s, len);
    o->s[len] = 0;
    o->len = len;
    size_t chars = 0;
    for (size_t i = 0; i < len; i++) if (((uint8_t)s[i] & 0xC0) != 0x80) chars++;
    o->chars = chars;
    o->hash = hash_bytes(s, len);
    m->allocated += 0;
    return o;
}

olist *list_new(vm *m) { return vm_alloc(m, sizeof(olist), O_LIST); }

bool list_push(vm *m, olist *l, value v) {
    if (l->len == l->cap) {
        size_t cap = l->cap ? l->cap * 2 : 8;
        if (cap > (16u << 20)) return false;
        value *nv = realloc(l->v, cap * sizeof(value));
        if (!nv) return false;
        m->allocated += (cap - l->cap) * sizeof(value);
        l->v = nv;
        l->cap = cap;
    }
    l->v[l->len++] = v;
    return true;
}

omap *map_new(vm *m) { return vm_alloc(m, sizeof(omap), O_MAP); }

static uint32_t value_hash(value k) {
    if (is_obj(k, O_STR)) return AS_STR(k)->hash;
    if (k.t == V_INT) return (uint32_t)(k.as.i ^ (k.as.i >> 32)) * 2654435761u;
    if (k.t == V_BOOL) return k.as.b ? 1 : 2;
    return 0;
}

bool values_equal(value a, value b) {
    if (a.t != b.t) return false;
    switch (a.t) {
    case V_NIL: return true;
    case V_BOOL: return a.as.b == b.as.b;
    case V_INT: return a.as.i == b.as.i;
    case V_OBJ:
        if (is_obj(a, O_STR) && is_obj(b, O_STR)) {
            ostr *x = AS_STR(a), *y = AS_STR(b);
            return x->len == y->len && x->hash == y->hash && !memcmp(x->s, y->s, x->len);
        }
        return a.as.o == b.as.o;
    }
    return false;
}

static mslot *map_find(mslot *slots, size_t cap, value key) {
    size_t i = value_hash(key) & (cap - 1);
    for (;;) {
        mslot *s = &slots[i];
        if (!s->used || values_equal(s->key, key)) return s;
        i = (i + 1) & (cap - 1);
    }
}

bool map_get(omap *mp, value key, value *out) {
    if (!mp->count) return false;
    mslot *s = map_find(mp->slots, mp->cap, key);
    if (!s->used) return false;
    *out = s->val;
    return true;
}

bool map_set(vm *m, omap *mp, value key, value val) {
    if ((mp->count + 1) * 4 > mp->cap * 3) {
        size_t cap = mp->cap ? mp->cap * 2 : 16;
        if (cap > (16u << 20)) return false;
        mslot *ns = calloc(cap, sizeof(mslot));
        if (!ns) return false;
        for (size_t i = 0; i < mp->cap; i++) {
            if (!mp->slots[i].used) continue;
            mslot *d = map_find(ns, cap, mp->slots[i].key);
            *d = mp->slots[i];
        }
        free(mp->slots);
        m->allocated += (cap - mp->cap) * sizeof(mslot);
        mp->slots = ns;
        mp->cap = cap;
    }
    mslot *s = map_find(mp->slots, mp->cap, key);
    if (!s->used) { s->used = true; s->key = key; mp->count++; }
    s->val = val;
    return true;
}

ofunc *func_new(vm *m, const char *name) {
    ofunc *f = vm_alloc(m, sizeof(ofunc), O_FUNC);
    if (f && name) f->name = str_new(m, name, strlen(name));
    return f;
}

int func_const(vm *m, ofunc *f, value v) {
    /* names and strings are shared within a function */
    for (size_t i = 0; i < f->nconsts; i++)
        if (f->consts[i].t == v.t && (v.t != V_OBJ || is_obj(v, O_STR)) && values_equal(f->consts[i], v)) return (int)i;
    if (f->nconsts == f->cconsts) {
        size_t cap = f->cconsts ? f->cconsts * 2 : 16;
        value *nc = realloc(f->consts, cap * sizeof(value));
        if (!nc) return -1;
        f->consts = nc;
        f->cconsts = cap;
    }
    f->consts[f->nconsts] = v;
    return (int)f->nconsts++;
}

bool func_emit(vm *m, ofunc *f, uint8_t b, int line) {
    if (f->len == f->cap) {
        size_t cap = f->cap ? f->cap * 2 : 64;
        uint8_t *nc = realloc(f->code, cap);
        if (!nc) return false;
        f->code = nc;
        int *nl = realloc(f->lines, cap * sizeof(int));
        if (!nl) return false;
        f->lines = nl;
        f->cap = cap;
    }
    f->code[f->len] = b;
    f->lines[f->len] = line;
    f->len++;
    return true;
}

const char *type_name(value v) {
    switch (v.t) {
    case V_NIL: return "nil";
    case V_BOOL: return "bool";
    case V_INT: return "int";
    case V_OBJ:
        switch (v.as.o->t) {
        case O_STR: return "string";
        case O_LIST: return "list";
        case O_MAP: return "map";
        case O_FUNC: case O_NATIVE: return "function";
        case O_RANGE: return "range";
        }
    }
    return "?";
}

/* ------------------------------------------------------------------- GC */

static void mark_value(value v);

static obj **gray;
static size_t ngray, cgray;

static void mark_obj(obj *o) {
    if (!o || o->marked) return;
    o->marked = true;
    if (ngray == cgray) {
        size_t cap = cgray ? cgray * 2 : 256;
        obj **ng = realloc(gray, cap * sizeof(obj *));
        if (!ng) return;                     /* objects stay marked; their children may be lost: fatal below */
        gray = ng;
        cgray = cap;
    }
    gray[ngray++] = o;
}

static void mark_value(value v) { if (v.t == V_OBJ) mark_obj(v.as.o); }

static void blacken(obj *o) {
    switch (o->t) {
    case O_LIST: {
        olist *l = (olist *)o;
        for (size_t i = 0; i < l->len; i++) mark_value(l->v[i]);
        break;
    }
    case O_MAP: {
        omap *mp = (omap *)o;
        for (size_t i = 0; i < mp->cap; i++) if (mp->slots[i].used) { mark_value(mp->slots[i].key); mark_value(mp->slots[i].val); }
        break;
    }
    case O_FUNC: {
        ofunc *f = (ofunc *)o;
        mark_obj(&f->name->h);
        for (size_t i = 0; i < f->nconsts; i++) mark_value(f->consts[i]);
        break;
    }
    default: break;
    }
}

static void free_obj(vm *m, obj *o) {
    switch (o->t) {
    case O_LIST: free(((olist *)o)->v); break;
    case O_MAP: free(((omap *)o)->slots); break;
    case O_FUNC: free(((ofunc *)o)->code); free(((ofunc *)o)->lines); free(((ofunc *)o)->consts); break;
    default: break;
    }
    free(o);
}

static void gc(vm *m) {
    if (m->gc_off) return;
    gc_vm = m;
    for (value *v = m->stack; v < m->sp; v++) mark_value(*v);
    for (int i = 0; i < m->nframes; i++) mark_obj(&m->frames[i].f->h);
    mark_obj(&m->globals->h);
    while (ngray) blacken(gray[--ngray]);
    size_t live = 0;
    obj **pp = &m->objects;
    while (*pp) {
        obj *o = *pp;
        if (o->marked) { o->marked = false; live++; pp = &o->next; continue; }
        *pp = o->next;
        free_obj(m, o);
    }
    /* a rough count of what is left decides the next collection */
    m->allocated = live * 64;
    m->next_gc = m->allocated * 2 + (4u << 20);
}

/* -------------------------------------------------------------- errors */

void vm_runtime_error(vm *m, const char *fmt, ...) {
    if (m->failed) return;
    m->failed = true;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(m->error, sizeof m->error, fmt, ap);
    va_end(ap);
}

/* ------------------------------------------------------------- strings */

int value_to_string(value v, char *buf, size_t len, bool quote) {
    switch (v.t) {
    case V_NIL: return snprintf(buf, len, "nil");
    case V_BOOL: return snprintf(buf, len, v.as.b ? "true" : "false");
    case V_INT: return snprintf(buf, len, "%ld", (long)v.as.i);
    case V_OBJ:
        switch (v.as.o->t) {
        case O_STR: return quote ? snprintf(buf, len, "\"%s\"", AS_STR(v)->s) : snprintf(buf, len, "%s", AS_STR(v)->s);
        case O_FUNC: return snprintf(buf, len, "<fn %s>", AS_FUNC(v)->name ? AS_FUNC(v)->name->s : "?");
        case O_NATIVE: return snprintf(buf, len, "<builtin %s>", ((onative *)v.as.o)->name);
        case O_RANGE: {
            orange *r = (orange *)v.as.o;
            return snprintf(buf, len, "range(%ld, %ld)", (long)r->start, (long)r->end);
        }
        case O_LIST: {
            olist *l = AS_LIST(v);
            size_t o = (size_t)snprintf(buf, len, "[");
            for (size_t i = 0; i < l->len && o < len; i++) {
                o += (size_t)value_to_string(l->v[i], buf + o, len - o, true);
                if (i + 1 < l->len && o < len) o += (size_t)snprintf(buf + o, len - o, ", ");
            }
            if (o < len) o += (size_t)snprintf(buf + o, len - o, "]");
            return (int)o;
        }
        case O_MAP: {
            omap *mp = AS_MAP(v);
            size_t o = (size_t)snprintf(buf, len, "{");
            bool first = true;
            for (size_t i = 0; i < mp->cap && o < len; i++) {
                if (!mp->slots[i].used) continue;
                if (!first && o < len) o += (size_t)snprintf(buf + o, len - o, ", ");
                first = false;
                if (o < len) o += (size_t)value_to_string(mp->slots[i].key, buf + o, len - o, true);
                if (o < len) o += (size_t)snprintf(buf + o, len - o, ": ");
                if (o < len) o += (size_t)value_to_string(mp->slots[i].val, buf + o, len - o, true);
            }
            if (o < len) o += (size_t)snprintf(buf + o, len - o, "}");
            return (int)o;
        }
        }
    }
    return 0;
}

/* byte offset of character index ci in s (ci <= chars) */
static size_t char_offset(ostr *s, int64_t ci) {
    if (s->chars == s->len) return (size_t)ci;            /* ASCII */
    size_t o = 0;
    for (int64_t c = 0; o < s->len; o++) {
        if (((uint8_t)s->s[o] & 0xC0) != 0x80) { if (c == ci) return o; c++; }
    }
    return s->len;
}

static value concat(vm *m, value a, value b) {
    char *ta = NULL, *tb = NULL;
    const char *sa, *sb;
    size_t la, lb;
    char tmp1[64], tmp2[64];
    if (is_obj(a, O_STR)) { sa = AS_STR(a)->s; la = AS_STR(a)->len; }
    else if (a.t == V_OBJ && (a.as.o->t == O_LIST || a.as.o->t == O_MAP)) {
        ta = malloc(1 << 16); value_to_string(a, ta, 1 << 16, true); sa = ta; la = strlen(ta);
    } else { value_to_string(a, tmp1, sizeof tmp1, false); sa = tmp1; la = strlen(tmp1); }
    if (is_obj(b, O_STR)) { sb = AS_STR(b)->s; lb = AS_STR(b)->len; }
    else if (b.t == V_OBJ && (b.as.o->t == O_LIST || b.as.o->t == O_MAP)) {
        tb = malloc(1 << 16); value_to_string(b, tb, 1 << 16, true); sb = tb; lb = strlen(tb);
    } else { value_to_string(b, tmp2, sizeof tmp2, false); sb = tmp2; lb = strlen(tmp2); }
    value r = v_nil();
    char *buf = malloc(la + lb + 1);
    if (buf) {
        memcpy(buf, sa, la);
        memcpy(buf + la, sb, lb);
        ostr *s = str_new(m, buf, la + lb);
        free(buf);
        if (s) r = v_obj(s);
    } else {
        vm_runtime_error(m, "out of memory");
    }
    free(ta);
    free(tb);
    return r;
}

/* ------------------------------------------------------------------- run */

void vm_init(vm *m, const char *file) {
    memset(m, 0, sizeof *m);
    m->sp = m->stack;
    m->file = file;
    m->next_gc = 8u << 20;
    m->gc_off = true;
    m->globals = map_new(m);
    m->rng = (uint64_t)uptime_ms() * 6364136223846793005ULL + 1442695040888963407ULL;
}

static bool push(vm *m, value v) {
    if (m->sp >= m->stack + VM_STACK) { vm_runtime_error(m, "stack overflow (too much recursion?)"); return false; }
    *m->sp++ = v;
    return true;
}

static int current_line(vm *m) {
    if (!m->nframes) return 0;
    frame *fr = &m->frames[m->nframes - 1];
    size_t off = (size_t)(fr->ip - fr->f->code);
    if (off) off--;
    return off < fr->f->len ? fr->f->lines[off] : 0;
}

static bool call_value(vm *m, value callee, int argc) {
    if (is_obj(callee, O_FUNC)) {
        ofunc *f = AS_FUNC(callee);
        if (argc != f->arity) {
            vm_runtime_error(m, "%s() takes %d argument(s), %d given", f->name ? f->name->s : "function", f->arity, argc);
            return false;
        }
        if (m->nframes == VM_FRAMES) { vm_runtime_error(m, "too many nested calls (recursion deeper than %d)", VM_FRAMES); return false; }
        frame *fr = &m->frames[m->nframes++];
        fr->f = f;
        fr->ip = f->code;
        fr->base = m->sp - argc;
        return true;
    }
    if (is_obj(callee, O_NATIVE)) {
        onative *n = (onative *)callee.as.o;
        if (argc < n->min || (n->max >= 0 && argc > n->max)) {
            if (n->min == n->max) vm_runtime_error(m, "%s() takes %d argument(s), %d given", n->name, n->min, argc);
            else vm_runtime_error(m, "%s(): wrong number of arguments (%d)", n->name, argc);
            return false;
        }
        /* no collection while a builtin holds objects in C variables */
        bool was = m->gc_off;
        m->gc_off = true;
        value r = n->fn(m, argc, m->sp - argc);
        m->gc_off = was;
        if (m->failed || m->exiting) return false;
        m->sp -= argc + 1;
        push(m, r);
        return true;
    }
    vm_runtime_error(m, "a %s cannot be called", type_name(callee));
    return false;
}

static bool index_get(vm *m, value c, value k, value *out) {
    if (is_obj(c, O_LIST)) {
        if (k.t != V_INT) { vm_runtime_error(m, "a list index must be an int, not %s", type_name(k)); return false; }
        olist *l = AS_LIST(c);
        int64_t i = k.as.i < 0 ? (int64_t)l->len + k.as.i : k.as.i;
        if (i < 0 || i >= (int64_t)l->len) { vm_runtime_error(m, "list index %ld out of range (length %lu)", (long)k.as.i, (unsigned long)l->len); return false; }
        *out = l->v[i];
        return true;
    }
    if (is_obj(c, O_STR)) {
        if (k.t != V_INT) { vm_runtime_error(m, "a string index must be an int"); return false; }
        ostr *s = AS_STR(c);
        int64_t i = k.as.i < 0 ? (int64_t)s->chars + k.as.i : k.as.i;
        if (i < 0 || i >= (int64_t)s->chars) { vm_runtime_error(m, "string index %ld out of range (length %lu)", (long)k.as.i, (unsigned long)s->chars); return false; }
        size_t a = char_offset(s, i), b = a + 1;
        while (b < s->len && ((uint8_t)s->s[b] & 0xC0) == 0x80) b++;
        ostr *r = str_new(m, s->s + a, b - a);
        if (!r) return false;
        *out = v_obj(r);
        return true;
    }
    if (is_obj(c, O_MAP)) {
        if (!map_get(AS_MAP(c), k, out)) *out = v_nil();
        return true;
    }
    vm_runtime_error(m, "a %s cannot be indexed", type_name(c));
    return false;
}

static bool index_set(vm *m, value c, value k, value v) {
    if (is_obj(c, O_LIST)) {
        if (k.t != V_INT) { vm_runtime_error(m, "a list index must be an int"); return false; }
        olist *l = AS_LIST(c);
        int64_t i = k.as.i < 0 ? (int64_t)l->len + k.as.i : k.as.i;
        if (i < 0 || i >= (int64_t)l->len) { vm_runtime_error(m, "list index %ld out of range (length %lu)", (long)k.as.i, (unsigned long)l->len); return false; }
        l->v[i] = v;
        return true;
    }
    if (is_obj(c, O_MAP)) {
        if (!(is_obj(k, O_STR) || k.t == V_INT || k.t == V_BOOL)) { vm_runtime_error(m, "a map key must be a string, an int or a bool"); return false; }
        if (!map_set(m, AS_MAP(c), k, v)) { vm_runtime_error(m, "out of memory"); return false; }
        return true;
    }
    if (is_obj(c, O_STR)) { vm_runtime_error(m, "strings cannot be changed; build a new one"); return false; }
    vm_runtime_error(m, "a %s cannot be indexed", type_name(c));
    return false;
}

static int compare(vm *m, value a, value b, bool *ok) {
    *ok = true;
    if (a.t == V_INT && b.t == V_INT) return a.as.i < b.as.i ? -1 : a.as.i > b.as.i;
    if (is_obj(a, O_STR) && is_obj(b, O_STR)) {
        int c = strcmp(AS_STR(a)->s, AS_STR(b)->s);
        return c < 0 ? -1 : c > 0;
    }
    *ok = false;
    vm_runtime_error(m, "cannot compare %s with %s", type_name(a), type_name(b));
    return 0;
}

static bool truthy(value v) {
    if (v.t == V_NIL) return false;
    if (v.t == V_BOOL) return v.as.b;
    if (v.t == V_INT) return v.as.i != 0;
    if (is_obj(v, O_STR)) return AS_STR(v)->len > 0;
    if (is_obj(v, O_LIST)) return AS_LIST(v)->len > 0;
    return true;
}

#define READ8()  (*fr->ip++)
#define READ16() (fr->ip += 2, (uint16_t)((fr->ip[-2] << 8) | fr->ip[-1]))
#define POP()    (*--m->sp)
#define PEEK(n)  (m->sp[-1 - (n)])

int vm_run(vm *m, ofunc *main_fn) {
    m->gc_off = false;
    push(m, v_obj(main_fn));
    call_value(m, v_obj(main_fn), 0);
    frame *fr = &m->frames[m->nframes - 1];
    for (;;) {
        if (m->failed || m->exiting) break;
        uint8_t op = READ8();
        switch (op) {
        case OP_CONST: push(m, fr->f->consts[READ16()]); break;
        case OP_NIL: push(m, v_nil()); break;
        case OP_TRUE: push(m, v_bool(true)); break;
        case OP_FALSE: push(m, v_bool(false)); break;
        case OP_POP: m->sp--; break;
        case OP_DUP: { uint8_t n = READ8(); push(m, PEEK(n)); break; }
        case OP_GET_LOCAL: push(m, fr->base[READ8()]); break;
        case OP_SET_LOCAL: fr->base[READ8()] = PEEK(0); break;
        case OP_GET_GLOBAL: {
            value name = fr->f->consts[READ16()], v;
            if (!map_get(m->globals, name, &v)) { vm_runtime_error(m, "undefined name '%s'", AS_STR(name)->s); break; }
            push(m, v);
            break;
        }
        case OP_DEF_GLOBAL: {
            value name = fr->f->consts[READ16()];
            if (!map_set(m, m->globals, name, PEEK(0))) vm_runtime_error(m, "out of memory");
            m->sp--;
            break;
        }
        case OP_SET_GLOBAL: {
            value name = fr->f->consts[READ16()], v;
            if (!map_get(m->globals, name, &v)) { vm_runtime_error(m, "undefined name '%s' (declare it with let)", AS_STR(name)->s); break; }
            map_set(m, m->globals, name, PEEK(0));
            break;
        }
        case OP_GET_INDEX: {
            value k = PEEK(0), c = PEEK(1), v;           /* stay on the stack (GC roots) */
            if (index_get(m, c, k, &v)) { m->sp -= 2; push(m, v); }
            break;
        }
        case OP_SET_INDEX: {
            value v = POP(), k = POP(), c = POP();
            if (index_set(m, c, k, v)) push(m, v);
            break;
        }
        case OP_EQ: { value b = POP(), a = POP(); push(m, v_bool(values_equal(a, b))); break; }
        case OP_NE: { value b = POP(), a = POP(); push(m, v_bool(!values_equal(a, b))); break; }
        case OP_LT: case OP_LE: case OP_GT: case OP_GE: {
            value b = POP(), a = POP();
            bool ok;
            int c = compare(m, a, b, &ok);
            if (!ok) break;
            bool r = op == OP_LT ? c < 0 : op == OP_LE ? c <= 0 : op == OP_GT ? c > 0 : c >= 0;
            push(m, v_bool(r));
            break;
        }
        case OP_ADD: {
            value b = PEEK(0), a = PEEK(1);              /* popped only after allocating */
            if (a.t == V_INT && b.t == V_INT) { m->sp -= 2; push(m, v_int((int64_t)((uint64_t)a.as.i + (uint64_t)b.as.i))); break; }
            if (is_obj(a, O_STR) || is_obj(b, O_STR)) {
                value r = concat(m, a, b);
                if (!m->failed) { m->sp -= 2; push(m, r); }
                break;
            }
            if (is_obj(a, O_LIST) && is_obj(b, O_LIST)) {
                olist *r = list_new(m);
                if (!r) { vm_runtime_error(m, "out of memory"); break; }
                for (size_t i = 0; i < AS_LIST(a)->len; i++) list_push(m, r, AS_LIST(a)->v[i]);
                for (size_t i = 0; i < AS_LIST(b)->len; i++) list_push(m, r, AS_LIST(b)->v[i]);
                m->sp -= 2;
                push(m, v_obj(r));
                break;
            }
            vm_runtime_error(m, "cannot add %s and %s", type_name(a), type_name(b));
            break;
        }
        case OP_SUB: case OP_MUL: case OP_DIV: case OP_MOD: {
            value b = PEEK(0), a = PEEK(1);
            if (op == OP_MUL && is_obj(a, O_STR) && b.t == V_INT) {
                if (b.as.i < 0 || (uint64_t)b.as.i * AS_STR(a)->len > (16u << 20)) { vm_runtime_error(m, "string repetition too large"); break; }
                size_t n = (size_t)b.as.i * AS_STR(a)->len;
                char *buf = malloc(n + 1);
                if (!buf) { vm_runtime_error(m, "out of memory"); break; }
                for (int64_t i = 0; i < b.as.i; i++) memcpy(buf + i * AS_STR(a)->len, AS_STR(a)->s, AS_STR(a)->len);
                ostr *r = str_new(m, buf, n);
                free(buf);
                if (r) { m->sp -= 2; push(m, v_obj(r)); }
                break;
            }
            m->sp -= 2;
            if (a.t != V_INT || b.t != V_INT) {
                vm_runtime_error(m, "'%s' needs two ints, not %s and %s", op == OP_SUB ? "-" : op == OP_MUL ? "*" : op == OP_DIV ? "/" : "%",
                                 type_name(a), type_name(b));
                break;
            }
            int64_t x = a.as.i, y = b.as.i, r;
            if ((op == OP_DIV || op == OP_MOD) && y == 0) { vm_runtime_error(m, "division by zero"); break; }
            if ((op == OP_DIV || op == OP_MOD) && x == INT64_MIN && y == -1) { vm_runtime_error(m, "integer overflow"); break; }
            if (op == OP_SUB) r = (int64_t)((uint64_t)x - (uint64_t)y);
            else if (op == OP_MUL) r = (int64_t)((uint64_t)x * (uint64_t)y);
            else if (op == OP_DIV) r = x / y;
            else r = x % y;
            push(m, v_int(r));
            break;
        }
        case OP_NEG: {
            value a = POP();
            if (a.t != V_INT) { vm_runtime_error(m, "cannot negate a %s", type_name(a)); break; }
            push(m, v_int((int64_t)(0 - (uint64_t)a.as.i)));
            break;
        }
        case OP_NOT: { value a = POP(); push(m, v_bool(!truthy(a))); break; }
        case OP_JUMP: { uint16_t o = READ16(); fr->ip += o; break; }
        case OP_JUMP_IF_FALSE: { uint16_t o = READ16(); if (!truthy(PEEK(0))) fr->ip += o; break; }
        case OP_LOOP: { uint16_t o = READ16(); fr->ip -= o; break; }
        case OP_CALL: {
            uint8_t argc = READ8();
            if (!call_value(m, PEEK(argc), argc)) break;
            fr = &m->frames[m->nframes - 1];
            break;
        }
        case OP_RETURN: {
            value r = POP();
            value *base = fr->base;
            m->nframes--;
            if (m->nframes == 0) { m->sp = m->stack; return m->exit_code; }
            m->sp = base - 1;
            push(m, r);
            fr = &m->frames[m->nframes - 1];
            break;
        }
        case OP_LIST: {
            uint8_t n = READ8();
            olist *l = list_new(m);
            if (!l) { vm_runtime_error(m, "out of memory"); break; }
            for (int i = n - 1; i >= 0; i--) list_push(m, l, PEEK(i));
            m->sp -= n;
            push(m, v_obj(l));
            break;
        }
        case OP_MAP: {
            uint8_t n = READ8();
            omap *mp = map_new(m);
            if (!mp) { vm_runtime_error(m, "out of memory"); break; }
            push(m, v_obj(mp));                    /* kept reachable while it fills */
            for (int i = n - 1; i >= 0; i--) {
                value k = PEEK(1 + i * 2 + 1), v = PEEK(1 + i * 2);
                if (!(is_obj(k, O_STR) || k.t == V_INT || k.t == V_BOOL)) { vm_runtime_error(m, "a map key must be a string, an int or a bool"); break; }
                map_set(m, mp, k, v);
            }
            m->sp -= 2 * n + 1;
            push(m, v_obj(mp));
            break;
        }
        case OP_ITER_NEXT: {
            uint8_t slot = READ8();
            uint16_t exit = READ16();
            value it = fr->base[slot];
            int64_t idx = fr->base[slot + 1].as.i;
            bool done = false;
            value item = v_nil();
            if (is_obj(it, O_LIST)) {
                if (idx >= (int64_t)AS_LIST(it)->len) done = true;
                else item = AS_LIST(it)->v[idx++];
            } else if (is_obj(it, O_RANGE)) {
                orange *r = (orange *)it.as.o;
                int64_t v = r->start + idx * r->step;
                if ((r->step > 0 && v >= r->end) || (r->step < 0 && v <= r->end)) done = true;
                else { item = v_int(v); idx++; }
            } else if (is_obj(it, O_STR)) {
                ostr *s = AS_STR(it);
                if (idx >= (int64_t)s->len) done = true;
                else {
                    int64_t e = idx + 1;
                    while (e < (int64_t)s->len && ((uint8_t)s->s[e] & 0xC0) == 0x80) e++;
                    ostr *ch = str_new(m, s->s + idx, (size_t)(e - idx));
                    if (!ch) break;
                    item = v_obj(ch);
                    idx = e;
                }
            } else if (is_obj(it, O_MAP)) {
                omap *mp = AS_MAP(it);
                while (idx < (int64_t)mp->cap && !mp->slots[idx].used) idx++;
                if (idx >= (int64_t)mp->cap) done = true;
                else item = mp->slots[idx++].key;
            } else {
                vm_runtime_error(m, "cannot loop over a %s", type_name(it));
                break;
            }
            if (done) { fr->ip += exit; break; }
            fr->base[slot + 1] = v_int(idx);
            fr->base[slot + 2] = item;
            break;
        }
        default:
            vm_runtime_error(m, "internal error: bad instruction %d", op);
        }
    }
    if (m->exiting) return m->exit_code;
    flush();
    dprintf(2, "\x1b[91mxvc: %s:%d: runtime error: %s\x1b[0m\n", m->file, current_line(m), m->error);
    return 1;
}
