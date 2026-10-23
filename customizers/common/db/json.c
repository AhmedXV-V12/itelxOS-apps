/* json: a strict JSON parser and writer (json.h). */
#include "json.h"

struct pctx { const char *p, *end, *start; char *err; size_t elen; int depth; };

static void fail(struct pctx *c, const char *msg) {
    if (c->err && !c->err[0]) snprintf(c->err, c->elen, "%s at byte %ld", msg, (long)(c->p - c->start));
}

static void ws(struct pctx *c) {
    while (c->p < c->end && (*c->p == ' ' || *c->p == '\t' || *c->p == '\n' || *c->p == '\r')) c->p++;
}

static struct jv *mk(int t) {
    struct jv *v = calloc(1, sizeof *v);
    v->t = t;
    return v;
}

double json_strtod(const char *s, char **endp) {
    const char *p = s;
    bool neg = false;
    if (*p == '-') { neg = true; p++; } else if (*p == '+') p++;
    double v = 0;
    int digits = 0;
    while (isdigit((unsigned char)*p)) { v = v * 10 + (*p - '0'); p++; digits++; }
    if (*p == '.') {
        p++;
        double f = 0.1;
        while (isdigit((unsigned char)*p)) { v += (*p - '0') * f; f /= 10; p++; digits++; }
    }
    if (digits && (*p == 'e' || *p == 'E')) {
        const char *q = p + 1;
        bool en = false;
        if (*q == '-') { en = true; q++; } else if (*q == '+') q++;
        if (isdigit((unsigned char)*q)) {
            int e = 0;
            while (isdigit((unsigned char)*q)) { if (e < 400) e = e * 10 + (*q - '0'); q++; }
            double m = 1;
            for (int i = 0; i < e; i++) m *= 10;
            v = en ? v / m : v * m;
            p = q;
        }
    }
    if (!digits) p = s;
    if (endp) *endp = (char *)p;
    return neg ? -v : v;
}

static int hex4(const char *p) {
    int v = 0;
    for (int i = 0; i < 4; i++) {
        char c = p[i];
        int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (d < 0) return -1;
        v = v * 16 + d;
    }
    return v;
}

static char *parse_str(struct pctx *c, size_t *len) {
    c->p++;                                   /* the quote */
    struct kbuf b = { 0 };
    kb_put(&b, "", 0);
    while (c->p < c->end && *c->p != '"') {
        unsigned char ch = (unsigned char)*c->p;
        if (ch < 0x20) { fail(c, "a control character in a string"); kb_free(&b); return NULL; }
        if (ch != '\\') { kb_put(&b, c->p++, 1); continue; }
        c->p++;
        if (c->p >= c->end) break;
        char e = *c->p++;
        char o;
        switch (e) {
        case '"': o = '"'; break;
        case '\\': o = '\\'; break;
        case '/': o = '/'; break;
        case 'b': o = '\b'; break;
        case 'f': o = '\f'; break;
        case 'n': o = '\n'; break;
        case 'r': o = '\r'; break;
        case 't': o = '\t'; break;
        case 'u': {
            if (c->end - c->p < 4) { fail(c, "a bad \\u escape"); kb_free(&b); return NULL; }
            int u = hex4(c->p);
            if (u < 0) { fail(c, "a bad \\u escape"); kb_free(&b); return NULL; }
            c->p += 4;
            uint32_t cp = (uint32_t)u;
            if (u >= 0xD800 && u <= 0xDBFF && c->end - c->p >= 6 && c->p[0] == '\\' && c->p[1] == 'u') {
                int lo = hex4(c->p + 2);
                if (lo >= 0xDC00 && lo <= 0xDFFF) { cp = 0x10000 + (((uint32_t)u - 0xD800) << 10) + ((uint32_t)lo - 0xDC00); c->p += 6; }
            }
            char u8[4];
            int n = utf8_encode(cp, u8);
            kb_put(&b, u8, (size_t)n);
            continue;
        }
        default: fail(c, "a bad escape"); kb_free(&b); return NULL;
        }
        kb_put(&b, &o, 1);
    }
    if (c->p >= c->end) { fail(c, "a string without its end"); kb_free(&b); return NULL; }
    c->p++;
    *len = b.n;
    return b.p;
}

static struct jv *parse_value(struct pctx *c);

static bool add(struct jv *v, struct jv *item, char *key) {
    if ((v->n & (v->n - 1)) == 0) {
        int cap = v->n ? v->n * 2 : 4;
        v->items = realloc(v->items, sizeof(struct jv *) * (size_t)cap);
        if (v->t == J_OBJ) v->keys = realloc(v->keys, sizeof(char *) * (size_t)cap);
    }
    v->items[v->n] = item;
    if (v->t == J_OBJ) v->keys[v->n] = key;
    v->n++;
    return true;
}

static struct jv *parse_value(struct pctx *c) {
    ws(c);
    if (c->p >= c->end) { fail(c, "the text ended too early"); return NULL; }
    if (++c->depth > 200) { fail(c, "nested too deep"); return NULL; }
    struct jv *v = NULL;
    char ch = *c->p;
    if (ch == '{' || ch == '[') {
        bool obj = ch == '{';
        v = mk(obj ? J_OBJ : J_ARR);
        c->p++;
        ws(c);
        if (c->p < c->end && *c->p == (obj ? '}' : ']')) { c->p++; c->depth--; return v; }
        for (;;) {
            char *key = NULL;
            if (obj) {
                ws(c);
                if (c->p >= c->end || *c->p != '"') { fail(c, "a key (in quotes) was expected"); json_free(v); return NULL; }
                size_t kl;
                key = parse_str(c, &kl);
                if (!key) { json_free(v); return NULL; }
                ws(c);
                if (c->p >= c->end || *c->p != ':') { fail(c, "':' was expected"); free(key); json_free(v); return NULL; }
                c->p++;
            }
            struct jv *item = parse_value(c);
            if (!item) { free(key); json_free(v); return NULL; }
            add(v, item, key);
            ws(c);
            if (c->p < c->end && *c->p == ',') { c->p++; continue; }
            if (c->p < c->end && *c->p == (obj ? '}' : ']')) { c->p++; break; }
            fail(c, obj ? "',' or '}' was expected" : "',' or ']' was expected");
            json_free(v);
            return NULL;
        }
    } else if (ch == '"') {
        v = mk(J_STR);
        v->s = parse_str(c, &v->slen);
        if (!v->s) { free(v); return NULL; }
    } else if (ch == '-' || isdigit((unsigned char)ch)) {
        const char *s = c->p;
        if (*c->p == '-') c->p++;
        if (c->p >= c->end || !isdigit((unsigned char)*c->p)) { fail(c, "a bad number"); return NULL; }
        while (c->p < c->end && (isdigit((unsigned char)*c->p) || *c->p == '.' || *c->p == 'e' || *c->p == 'E' || *c->p == '+' || *c->p == '-')) c->p++;
        v = mk(J_NUM);
        v->slen = (size_t)(c->p - s);
        v->s = malloc(v->slen + 1);
        memcpy(v->s, s, v->slen);
        v->s[v->slen] = 0;
        char *e;
        v->num = json_strtod(v->s, &e);
        if (*e) { fail(c, "a bad number"); json_free(v); return NULL; }
    } else if (c->end - c->p >= 4 && !strncmp(c->p, "true", 4)) { v = mk(J_TRUE); c->p += 4; }
    else if (c->end - c->p >= 5 && !strncmp(c->p, "false", 5)) { v = mk(J_FALSE); c->p += 5; }
    else if (c->end - c->p >= 4 && !strncmp(c->p, "null", 4)) { v = mk(J_NULL); c->p += 4; }
    else { fail(c, "not JSON"); return NULL; }
    c->depth--;
    return v;
}

struct jv *json_parse(const char *text, size_t len, size_t *used, char *err, size_t elen) {
    struct pctx c = { text, text + len, text, err, elen, 0 };
    if (err && elen) err[0] = 0;
    struct jv *v = parse_value(&c);
    if (v) ws(&c);
    if (used) *used = (size_t)(c.p - text);
    return v;
}

void json_free(struct jv *v) {
    if (!v) return;
    for (int i = 0; i < v->n; i++) {
        json_free(v->items[i]);
        if (v->keys) free(v->keys[i]);
    }
    free(v->items);
    free(v->keys);
    free(v->s);
    free(v);
}

struct jv *json_get(const struct jv *obj, const char *key) {
    if (!obj || obj->t != J_OBJ) return NULL;
    for (int i = 0; i < obj->n; i++) if (!strcmp(obj->keys[i], key)) return obj->items[i];
    return NULL;
}

void json_write_str(const char *s, size_t n, struct kbuf *out) {
    kb_put(out, "\"", 1);
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '"' || c == '\\') { char e[2] = { '\\', (char)c }; kb_put(out, e, 2); }
        else if (c == '\n') kb_put(out, "\\n", 2);
        else if (c == '\r') kb_put(out, "\\r", 2);
        else if (c == '\t') kb_put(out, "\\t", 2);
        else if (c < 0x20) kb_printf(out, "\\u%04x", c);
        else kb_put(out, &s[i], 1);
    }
    kb_put(out, "\"", 1);
}

void json_write(const struct jv *v, struct kbuf *out) {
    switch (v->t) {
    case J_NULL: kb_str(out, "null"); break;
    case J_TRUE: kb_str(out, "true"); break;
    case J_FALSE: kb_str(out, "false"); break;
    case J_NUM: kb_put(out, v->s, v->slen); break;
    case J_STR: json_write_str(v->s, v->slen, out); break;
    case J_ARR:
    case J_OBJ:
        kb_put(out, v->t == J_OBJ ? "{" : "[", 1);
        for (int i = 0; i < v->n; i++) {
            if (i) kb_put(out, ",", 1);
            if (v->t == J_OBJ) { json_write_str(v->keys[i], strlen(v->keys[i]), out); kb_put(out, ":", 1); }
            json_write(v->items[i], out);
        }
        kb_put(out, v->t == J_OBJ ? "}" : "]", 1);
        break;
    }
}
