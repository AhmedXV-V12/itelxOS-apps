/* XVC compiler: source text -> bytecode, in one pass (a Pratt parser).
 *
 * Statements end at the end of a line (or at ';'); inside ( ) and [ ] a
 * line may continue. An operator at the start of a line starts a new
 * statement, so an expression that goes on to the next line ends its line
 * with the operator. */
#include "xvc.h"

typedef enum {
    T_EOF, T_ERROR, T_IDENT, T_INT, T_STRING,
    T_LPAREN, T_RPAREN, T_LBRACE, T_RBRACE, T_LBRACKET, T_RBRACKET,
    T_COMMA, T_COLON, T_SEMI, T_DOT,
    T_PLUS, T_MINUS, T_STAR, T_SLASH, T_PERCENT,
    T_EQ, T_EQEQ, T_NE, T_LT, T_LE, T_GT, T_GE, T_PLUSEQ, T_MINUSEQ,
    T_LET, T_FN, T_IF, T_ELIF, T_ELSE, T_WHILE, T_FOR, T_IN, T_RETURN, T_BREAK, T_CONTINUE,
    T_TRUE, T_FALSE, T_NIL, T_AND, T_OR, T_NOT,
} ttype;

typedef struct {
    ttype t;
    const char *s;
    int len;
    int line;
    bool nl;                /* a newline came before it */
    int64_t num;
} token;

typedef struct {
    const char *name;
    int len;
    int depth;
} local;

typedef struct loop {
    struct loop *outer;
    int start;
    int depth;              /* scope depth of the loop's own locals */
    int breaks[64];
    int nbreaks;
} loop;

typedef struct compiler {
    struct compiler *enclosing;
    ofunc *f;
    local locals[256];
    int nlocals;
    int depth;
    loop *loops;
    bool toplevel;
} compiler;

typedef struct {
    vm *m;
    const char *src, *cur, *end, *file;
    int line;
    int parens;
    token prev, tok;
    bool had_error;
    compiler *c;
} parser;

/* ---------------------------------------------------------------- lexer */

static bool ident_char(unsigned char ch) {
    return isalnum(ch) || ch == '_' || ch >= 0x80;
}

static token make(parser *p, ttype t, const char *s, int len, bool nl) {
    token k;
    k.t = t; k.s = s; k.len = len; k.line = p->line; k.nl = nl; k.num = 0;
    return k;
}

static token error_tok(parser *p, const char *msg, bool nl) {
    return make(p, T_ERROR, msg, (int)strlen(msg), nl);
}

static struct { const char *w; ttype t; } keywords[] = {
    {"let", T_LET}, {"fn", T_FN}, {"if", T_IF}, {"elif", T_ELIF}, {"else", T_ELSE},
    {"while", T_WHILE}, {"for", T_FOR}, {"in", T_IN}, {"return", T_RETURN},
    {"break", T_BREAK}, {"continue", T_CONTINUE}, {"true", T_TRUE}, {"false", T_FALSE},
    {"nil", T_NIL}, {"and", T_AND}, {"or", T_OR}, {"not", T_NOT},
};

static token scan(parser *p) {
    bool nl = false;
    for (;;) {
        if (p->cur >= p->end) return make(p, T_EOF, p->cur, 0, true);
        char ch = *p->cur;
        if (ch == '\n') { p->line++; p->cur++; nl = true; continue; }
        if (ch == ' ' || ch == '\t' || ch == '\r') { p->cur++; continue; }
        if (ch == '#') { while (p->cur < p->end && *p->cur != '\n') p->cur++; continue; }
        break;
    }
    if (p->parens > 0) nl = false;
    const char *s = p->cur;
    char ch = *p->cur++;
    if (isdigit((unsigned char)ch)) {
        int64_t v = 0;
        int base = 10;
        if (ch == '0' && p->cur < p->end && (*p->cur == 'x' || *p->cur == 'X')) { base = 16; p->cur++; }
        else v = ch - '0';
        bool any = base == 10, overflow = false;
        while (p->cur < p->end) {
            char d = *p->cur;
            int dv;
            if (d >= '0' && d <= '9') dv = d - '0';
            else if (base == 16 && d >= 'a' && d <= 'f') dv = d - 'a' + 10;
            else if (base == 16 && d >= 'A' && d <= 'F') dv = d - 'A' + 10;
            else if (d == '_') { p->cur++; continue; }
            else break;
            if (v > (INT64_MAX - dv) / base) overflow = true;
            v = v * base + dv;
            any = true;
            p->cur++;
        }
        if (!any) return error_tok(p, "bad number", nl);
        if (overflow) return error_tok(p, "number too large (64-bit integers)", nl);
        if (p->cur < p->end && ident_char((unsigned char)*p->cur)) return error_tok(p, "bad number", nl);
        token k = make(p, T_INT, s, (int)(p->cur - s), nl);
        k.num = v;
        return k;
    }
    if (ident_char((unsigned char)ch)) {
        while (p->cur < p->end && ident_char((unsigned char)*p->cur)) p->cur++;
        int len = (int)(p->cur - s);
        for (size_t i = 0; i < sizeof keywords / sizeof keywords[0]; i++)
            if ((int)strlen(keywords[i].w) == len && !memcmp(keywords[i].w, s, len)) return make(p, keywords[i].t, s, len, nl);
        return make(p, T_IDENT, s, len, nl);
    }
    switch (ch) {
    case '(': p->parens++; return make(p, T_LPAREN, s, 1, nl);
    case ')': if (p->parens) p->parens--; return make(p, T_RPAREN, s, 1, nl);
    case '[': p->parens++; return make(p, T_LBRACKET, s, 1, nl);
    case ']': if (p->parens) p->parens--; return make(p, T_RBRACKET, s, 1, nl);
    case '{': return make(p, T_LBRACE, s, 1, nl);
    case '}': return make(p, T_RBRACE, s, 1, nl);
    case ',': return make(p, T_COMMA, s, 1, nl);
    case ':': return make(p, T_COLON, s, 1, nl);
    case ';': return make(p, T_SEMI, s, 1, nl);
    case '.': return make(p, T_DOT, s, 1, nl);
    case '*': return make(p, T_STAR, s, 1, nl);
    case '/': return make(p, T_SLASH, s, 1, nl);
    case '%': return make(p, T_PERCENT, s, 1, nl);
    case '+':
        if (p->cur < p->end && *p->cur == '=') { p->cur++; return make(p, T_PLUSEQ, s, 2, nl); }
        return make(p, T_PLUS, s, 1, nl);
    case '-':
        if (p->cur < p->end && *p->cur == '=') { p->cur++; return make(p, T_MINUSEQ, s, 2, nl); }
        return make(p, T_MINUS, s, 1, nl);
    case '=':
        if (p->cur < p->end && *p->cur == '=') { p->cur++; return make(p, T_EQEQ, s, 2, nl); }
        return make(p, T_EQ, s, 1, nl);
    case '!':
        if (p->cur < p->end && *p->cur == '=') { p->cur++; return make(p, T_NE, s, 2, nl); }
        return error_tok(p, "unexpected '!' (use 'not')", nl);
    case '<':
        if (p->cur < p->end && *p->cur == '=') { p->cur++; return make(p, T_LE, s, 2, nl); }
        return make(p, T_LT, s, 1, nl);
    case '>':
        if (p->cur < p->end && *p->cur == '=') { p->cur++; return make(p, T_GE, s, 2, nl); }
        return make(p, T_GT, s, 1, nl);
    case '"': {
        while (p->cur < p->end && *p->cur != '"') {
            if (*p->cur == '\n') return error_tok(p, "unterminated string", nl);
            if (*p->cur == '\\' && p->cur + 1 < p->end) p->cur++;
            p->cur++;
        }
        if (p->cur >= p->end) return error_tok(p, "unterminated string", nl);
        p->cur++;
        return make(p, T_STRING, s, (int)(p->cur - s), nl);
    }
    }
    return error_tok(p, "unexpected character", nl);
}

/* --------------------------------------------------------------- errors */

static void error_at(parser *p, token *t, const char *msg) {
    if (p->had_error) return;
    p->had_error = true;
    if (t->t == T_EOF) dprintf(2, "xvc: %s:%d: %s at the end of the file\n", p->file, t->line, msg);
    else if (t->t == T_ERROR) dprintf(2, "xvc: %s:%d: %.*s\n", p->file, t->line, t->len, t->s);
    else dprintf(2, "xvc: %s:%d: %s (at '%.*s')\n", p->file, t->line, msg, t->len > 30 ? 30 : t->len, t->s);
}

static void error(parser *p, const char *msg) { error_at(p, &p->prev, msg); }

static void advance(parser *p) {
    p->prev = p->tok;
    for (;;) {
        p->tok = scan(p);
        if (p->tok.t != T_ERROR) break;
        error_at(p, &p->tok, "");
    }
}

static bool check(parser *p, ttype t) { return p->tok.t == t; }
static bool match(parser *p, ttype t) { if (!check(p, t)) return false; advance(p); return true; }
static void consume(parser *p, ttype t, const char *msg) {
    if (p->tok.t == t) { advance(p); return; }
    error_at(p, &p->tok, msg);
}

/* ------------------------------------------------------------- emitting */

static ofunc *F(parser *p) { return p->c->f; }
static void emit(parser *p, uint8_t b) {
    if (!func_emit(p->m, F(p), b, p->prev.line)) error(p, "out of memory");
}
static void emit2(parser *p, uint8_t a, uint8_t b) { emit(p, a); emit(p, b); }
static void emit16(parser *p, uint16_t v) { emit(p, (uint8_t)(v >> 8)); emit(p, (uint8_t)v); }

static void emit_const(parser *p, value v) {
    int i = func_const(p->m, F(p), v);
    if (i < 0 || i > 65535) { error(p, "too many constants in one function"); return; }
    emit(p, OP_CONST);
    emit16(p, (uint16_t)i);
}

static int emit_jump(parser *p, uint8_t op) {
    emit(p, op);
    emit(p, 0xFF);
    emit(p, 0xFF);
    return (int)F(p)->len - 2;
}

static void patch_jump(parser *p, int at) {
    int dist = (int)F(p)->len - at - 2;
    if (dist > 65535) { error(p, "block too large to jump over"); return; }
    F(p)->code[at] = (uint8_t)(dist >> 8);
    F(p)->code[at + 1] = (uint8_t)dist;
}

static void emit_loop(parser *p, int start) {
    emit(p, OP_LOOP);
    int dist = (int)F(p)->len - start + 2;
    if (dist > 65535) { error(p, "loop body too large"); return; }
    emit16(p, (uint16_t)dist);
}

/* --------------------------------------------------------------- scopes */

static void begin_scope(parser *p) { p->c->depth++; }

static void end_scope(parser *p) {
    compiler *c = p->c;
    c->depth--;
    while (c->nlocals > 0 && c->locals[c->nlocals - 1].depth > c->depth) {
        emit(p, OP_POP);
        c->nlocals--;
    }
}

static int add_local(parser *p, const char *name, int len) {
    compiler *c = p->c;
    if (c->nlocals == 256) { error(p, "too many local variables in one function"); return -1; }
    for (int i = c->nlocals - 1; i >= 0 && c->locals[i].depth >= c->depth; i--)
        if (c->locals[i].len == len && !memcmp(c->locals[i].name, name, len)) {
            error(p, "a variable with this name already exists in this block");
            return -1;
        }
    c->locals[c->nlocals].name = name;
    c->locals[c->nlocals].len = len;
    c->locals[c->nlocals].depth = c->depth;
    if (c->nlocals + 1 > c->f->nlocals) c->f->nlocals = c->nlocals + 1;
    return c->nlocals++;
}

static int find_local(compiler *c, const char *name, int len) {
    for (int i = c->nlocals - 1; i >= 0; i--)
        if (c->locals[i].len == len && !memcmp(c->locals[i].name, name, len)) return i;
    return -1;
}

static bool global_scope(parser *p) { return p->c->toplevel && p->c->depth == 0; }

/* ---------------------------------------------------------- expressions */

typedef enum {
    P_NONE, P_ASSIGN, P_OR, P_AND, P_NOT, P_EQUALITY, P_COMPARISON, P_TERM, P_FACTOR, P_UNARY, P_CALL, P_PRIMARY
} prec;

typedef void (*parse_fn)(parser *p, bool can_assign);
typedef struct { parse_fn prefix, infix; prec pr; } rule;
static rule *get_rule(ttype t);
static void expression(parser *p);
static void parse_prec(parser *p, prec pr);
static void block(parser *p);
static void statement(parser *p);

static value name_const(parser *p, const char *s, int len) {
    ostr *str = str_new(p->m, s, (size_t)len);
    if (!str) { error(p, "out of memory"); return v_nil(); }
    return v_obj(str);
}

static void number(parser *p, bool ca) { emit_const(p, v_int(p->prev.num)); }

static void string(parser *p, bool ca) {
    /* the text between the quotes, escapes resolved */
    const char *s = p->prev.s + 1;
    int len = p->prev.len - 2;
    char *buf = malloc((size_t)len + 1);
    if (!buf) { error(p, "out of memory"); return; }
    int o = 0;
    for (int i = 0; i < len; i++) {
        if (s[i] != '\\') { buf[o++] = s[i]; continue; }
        i++;
        switch (s[i]) {
        case 'n': buf[o++] = '\n'; break;
        case 't': buf[o++] = '\t'; break;
        case 'r': buf[o++] = '\r'; break;
        case '0': buf[o++] = '\0'; break;
        case 'e': buf[o++] = 0x1B; break;
        case '\\': buf[o++] = '\\'; break;
        case '"': buf[o++] = '"'; break;
        case 'u': {
            /* \u{0627} */
            if (i + 1 < len && s[i + 1] == '{') {
                uint32_t cp = 0;
                int j = i + 2;
                while (j < len && s[j] != '}') {
                    char ch = s[j];
                    int d = isdigit((unsigned char)ch) ? ch - '0' : (ch | 0x20) >= 'a' && (ch | 0x20) <= 'f' ? (ch | 0x20) - 'a' + 10 : -1;
                    if (d < 0 || cp > 0x10FFFF) { error(p, "bad \\u{...} escape"); free(buf); return; }
                    cp = cp * 16 + d;
                    j++;
                }
                if (j >= len || cp > 0x10FFFF) { error(p, "bad \\u{...} escape"); free(buf); return; }
                o += utf8_encode(cp, buf + o);
                i = j;
                break;
            }
            error(p, "bad \\u escape (write \\u{XXXX})");
            free(buf);
            return;
        }
        default:
            error(p, "unknown escape in a string");
            free(buf);
            return;
        }
    }
    ostr *str = str_new(p->m, buf, (size_t)o);
    free(buf);
    if (!str) { error(p, "out of memory"); return; }
    emit_const(p, v_obj(str));
}

static void literal(parser *p, bool ca) {
    switch (p->prev.t) {
    case T_TRUE: emit(p, OP_TRUE); break;
    case T_FALSE: emit(p, OP_FALSE); break;
    default: emit(p, OP_NIL); break;
    }
}

static void grouping(parser *p, bool ca) {
    expression(p);
    consume(p, T_RPAREN, "expected ')'");
}

static void unary(parser *p, bool ca) {
    ttype op = p->prev.t;
    parse_prec(p, op == T_NOT ? P_EQUALITY : P_UNARY);
    emit(p, op == T_NOT ? OP_NOT : OP_NEG);
}

static void binary(parser *p, bool ca) {
    ttype op = p->prev.t;
    rule *r = get_rule(op);
    parse_prec(p, (prec)(r->pr + 1));
    switch (op) {
    case T_PLUS: emit(p, OP_ADD); break;
    case T_MINUS: emit(p, OP_SUB); break;
    case T_STAR: emit(p, OP_MUL); break;
    case T_SLASH: emit(p, OP_DIV); break;
    case T_PERCENT: emit(p, OP_MOD); break;
    case T_EQEQ: emit(p, OP_EQ); break;
    case T_NE: emit(p, OP_NE); break;
    case T_LT: emit(p, OP_LT); break;
    case T_LE: emit(p, OP_LE); break;
    case T_GT: emit(p, OP_GT); break;
    case T_GE: emit(p, OP_GE); break;
    default: break;
    }
}

static void and_(parser *p, bool ca) {
    int end = emit_jump(p, OP_JUMP_IF_FALSE);
    emit(p, OP_POP);
    parse_prec(p, P_AND);
    patch_jump(p, end);
}

static void or_(parser *p, bool ca) {
    int else_j = emit_jump(p, OP_JUMP_IF_FALSE);
    int end_j = emit_jump(p, OP_JUMP);
    patch_jump(p, else_j);
    emit(p, OP_POP);
    parse_prec(p, P_OR);
    patch_jump(p, end_j);
}

static void named(parser *p, const char *s, int len, bool can_assign) {
    int slot = find_local(p->c, s, len);
    if (slot < 0) {
        /* a local of an enclosing function cannot be reached: no closures */
        for (compiler *e = p->c->enclosing; e; e = e->enclosing) {
            if (!e->toplevel || e->depth > 0) {
                if (find_local(e, s, len) >= 0) {
                    error(p, "a function cannot use a local variable of the function around it (pass it as an argument)");
                    return;
                }
            }
        }
    }
    uint8_t get, set;
    int arg;
    if (slot >= 0) { get = OP_GET_LOCAL; set = OP_SET_LOCAL; arg = slot; }
    else {
        value nm = name_const(p, s, len);
        arg = func_const(p->m, F(p), nm);
        if (arg < 0 || arg > 65535) { error(p, "too many names"); return; }
        get = OP_GET_GLOBAL; set = OP_SET_GLOBAL;
    }
    if (can_assign && (check(p, T_EQ) || check(p, T_PLUSEQ) || check(p, T_MINUSEQ)) && !p->tok.nl) {
        ttype op = p->tok.t;
        advance(p);
        if (op != T_EQ) {
            emit(p, get);
            if (get == OP_GET_LOCAL) emit(p, (uint8_t)arg); else emit16(p, (uint16_t)arg);
        }
        expression(p);
        if (op == T_PLUSEQ) emit(p, OP_ADD);
        else if (op == T_MINUSEQ) emit(p, OP_SUB);
        emit(p, set);
    } else {
        emit(p, get);
    }
    if (get == OP_GET_LOCAL) emit(p, (uint8_t)arg); else emit16(p, (uint16_t)arg);
}

static void variable(parser *p, bool can_assign) { named(p, p->prev.s, p->prev.len, can_assign); }

static void call(parser *p, bool ca) {
    int argc = 0;
    if (!check(p, T_RPAREN)) {
        do {
            if (check(p, T_RPAREN)) break;
            expression(p);
            if (++argc > 64) { error(p, "more than 64 arguments"); return; }
        } while (match(p, T_COMMA));
    }
    consume(p, T_RPAREN, "expected ')' after the arguments");
    emit2(p, OP_CALL, (uint8_t)argc);
}

static void index_(parser *p, bool can_assign) {
    expression(p);
    consume(p, T_RBRACKET, "expected ']'");
    if (can_assign && (check(p, T_EQ) || check(p, T_PLUSEQ) || check(p, T_MINUSEQ)) && !p->tok.nl) {
        ttype op = p->tok.t;
        advance(p);
        if (op != T_EQ) {
            /* x[i] += v: [x, i] -> [x, i, x[i]] */
            emit(p, OP_DUP); emit(p, 1);
            emit(p, OP_DUP); emit(p, 1);
            emit(p, OP_GET_INDEX);
        }
        expression(p);
        if (op == T_PLUSEQ) emit(p, OP_ADD);
        else if (op == T_MINUSEQ) emit(p, OP_SUB);
        emit(p, OP_SET_INDEX);
    } else {
        emit(p, OP_GET_INDEX);
    }
}

static void list_lit(parser *p, bool ca) {
    int n = 0;
    if (!check(p, T_RBRACKET)) {
        do {
            if (check(p, T_RBRACKET)) break;
            expression(p);
            if (++n > 255) { error(p, "more than 255 items in a list literal"); return; }
        } while (match(p, T_COMMA));
    }
    consume(p, T_RBRACKET, "expected ']' after the list");
    emit2(p, OP_LIST, (uint8_t)n);
}

static void map_lit(parser *p, bool ca) {
    int n = 0;
    p->parens++;
    if (!check(p, T_RBRACE)) {
        do {
            if (check(p, T_RBRACE)) break;
            expression(p);
            consume(p, T_COLON, "expected ':' between a key and its value");
            expression(p);
            if (++n > 255) { error(p, "more than 255 pairs in a map literal"); break; }
        } while (match(p, T_COMMA));
    }
    p->parens--;
    consume(p, T_RBRACE, "expected '}' after the map");
    emit2(p, OP_MAP, (uint8_t)n);
}

static rule rules[] = {
    [T_LPAREN] = {grouping, call, P_CALL},
    [T_LBRACKET] = {list_lit, index_, P_CALL},
    [T_LBRACE] = {map_lit, NULL, P_NONE},
    [T_MINUS] = {unary, binary, P_TERM},
    [T_PLUS] = {NULL, binary, P_TERM},
    [T_SLASH] = {NULL, binary, P_FACTOR},
    [T_STAR] = {NULL, binary, P_FACTOR},
    [T_PERCENT] = {NULL, binary, P_FACTOR},
    [T_EQEQ] = {NULL, binary, P_EQUALITY},
    [T_NE] = {NULL, binary, P_EQUALITY},
    [T_LT] = {NULL, binary, P_COMPARISON},
    [T_LE] = {NULL, binary, P_COMPARISON},
    [T_GT] = {NULL, binary, P_COMPARISON},
    [T_GE] = {NULL, binary, P_COMPARISON},
    [T_IDENT] = {variable, NULL, P_NONE},
    [T_INT] = {number, NULL, P_NONE},
    [T_STRING] = {string, NULL, P_NONE},
    [T_TRUE] = {literal, NULL, P_NONE},
    [T_FALSE] = {literal, NULL, P_NONE},
    [T_NIL] = {literal, NULL, P_NONE},
    [T_AND] = {NULL, and_, P_AND},
    [T_OR] = {NULL, or_, P_OR},
    [T_NOT] = {unary, NULL, P_NONE},
};

static rule none_rule = {NULL, NULL, P_NONE};
static rule *get_rule(ttype t) {
    if ((size_t)t < sizeof rules / sizeof rules[0]) return &rules[t];
    return &none_rule;
}

static int depth_guard;

static void parse_prec(parser *p, prec pr) {
    if (++depth_guard > 200) { error(p, "expression nested too deeply"); depth_guard--; return; }
    advance(p);
    parse_fn prefix = get_rule(p->prev.t)->prefix;
    if (!prefix) { error(p, "expected an expression"); depth_guard--; return; }
    bool can_assign = pr <= P_ASSIGN;
    prefix(p, can_assign);
    /* an operator on a new line starts the next statement */
    while (!p->had_error && pr <= get_rule(p->tok.t)->pr && !p->tok.nl) {
        advance(p);
        get_rule(p->prev.t)->infix(p, can_assign);
    }
    if (can_assign && check(p, T_EQ) && !p->tok.nl) error_at(p, &p->tok, "cannot assign to this");
    depth_guard--;
}

static void expression(parser *p) { parse_prec(p, P_ASSIGN); }

/* ------------------------------------------------------------ statements */

static void end_statement(parser *p) {
    if (match(p, T_SEMI)) return;
    if (p->tok.nl || check(p, T_EOF) || check(p, T_RBRACE)) return;
    error_at(p, &p->tok, "expected the end of the line");
}

static void let_stmt(parser *p) {
    consume(p, T_IDENT, "expected a variable name after 'let'");
    token name = p->prev;
    if (match(p, T_EQ)) expression(p);
    else emit(p, OP_NIL);
    if (global_scope(p)) {
        int arg = func_const(p->m, F(p), name_const(p, name.s, name.len));
        emit(p, OP_DEF_GLOBAL);
        emit16(p, (uint16_t)arg);
    } else {
        add_local(p, name.s, name.len);
    }
    end_statement(p);
}

static void function(parser *p, token name) {
    compiler c;
    memset(&c, 0, sizeof c);
    c.enclosing = p->c;
    c.f = func_new(p->m, NULL);
    if (!c.f) { error(p, "out of memory"); return; }
    c.f->name = str_new(p->m, name.s, (size_t)name.len);
    p->c = &c;
    begin_scope(p);
    consume(p, T_LPAREN, "expected '(' after the function name");
    if (!check(p, T_RPAREN)) {
        do {
            consume(p, T_IDENT, "expected a parameter name");
            add_local(p, p->prev.s, p->prev.len);
            if (++c.f->arity > 64) error(p, "more than 64 parameters");
        } while (match(p, T_COMMA));
    }
    consume(p, T_RPAREN, "expected ')' after the parameters");
    consume(p, T_LBRACE, "expected '{' before the function body");
    block(p);
    emit(p, OP_NIL);
    emit(p, OP_RETURN);
    p->c = c.enclosing;
    emit_const(p, v_obj(c.f));
}

/* functions are declared at the top level of a program; they can call
 * each other (and themselves) by name, whatever the order */
static void fn_stmt(parser *p) {
    consume(p, T_IDENT, "expected a function name after 'fn'");
    token name = p->prev;
    if (!global_scope(p)) { error(p, "functions are declared at the top level of the program, not inside a block"); return; }
    function(p, name);
    int arg = func_const(p->m, F(p), name_const(p, name.s, name.len));
    emit(p, OP_DEF_GLOBAL);
    emit16(p, (uint16_t)arg);
}

static void block(parser *p) {
    while (!check(p, T_RBRACE) && !check(p, T_EOF) && !p->had_error) statement(p);
    consume(p, T_RBRACE, "expected '}'");
}

static void if_stmt(parser *p) {
    expression(p);
    consume(p, T_LBRACE, "expected '{' after the condition");
    int then_j = emit_jump(p, OP_JUMP_IF_FALSE);
    emit(p, OP_POP);
    begin_scope(p);
    block(p);
    end_scope(p);
    int end_j = emit_jump(p, OP_JUMP);
    patch_jump(p, then_j);
    emit(p, OP_POP);
    if (match(p, T_ELIF)) {
        if_stmt(p);
    } else if (match(p, T_ELSE)) {
        if (match(p, T_IF)) { if_stmt(p); }
        else {
            consume(p, T_LBRACE, "expected '{' after 'else'");
            begin_scope(p);
            block(p);
            end_scope(p);
        }
    }
    patch_jump(p, end_j);
}

static void loop_begin(parser *p, loop *l, int start) {
    l->outer = p->c->loops;
    l->start = start;
    l->depth = p->c->depth;
    l->nbreaks = 0;
    p->c->loops = l;
}

static void loop_end(parser *p, loop *l) {
    for (int i = 0; i < l->nbreaks; i++) patch_jump(p, l->breaks[i]);
    p->c->loops = l->outer;
}

static void while_stmt(parser *p) {
    int start = (int)F(p)->len;
    expression(p);
    consume(p, T_LBRACE, "expected '{' after the condition");
    int exit_j = emit_jump(p, OP_JUMP_IF_FALSE);
    emit(p, OP_POP);
    loop l;
    loop_begin(p, &l, start);
    begin_scope(p);
    block(p);
    end_scope(p);
    emit_loop(p, start);
    patch_jump(p, exit_j);
    emit(p, OP_POP);
    loop_end(p, &l);
}

static void for_stmt(parser *p) {
    begin_scope(p);
    consume(p, T_IDENT, "expected a variable name after 'for'");
    token var = p->prev;
    consume(p, T_IN, "expected 'in'");
    expression(p);
    int it = add_local(p, "(iter)", 6);
    emit_const(p, v_int(0));
    add_local(p, "(index)", 7);
    emit(p, OP_NIL);
    add_local(p, var.s, var.len);
    consume(p, T_LBRACE, "expected '{' after the for header");
    int start = (int)F(p)->len;
    emit2(p, OP_ITER_NEXT, (uint8_t)it);
    emit(p, 0xFF);
    emit(p, 0xFF);
    int exit_at = (int)F(p)->len - 2;
    loop l;
    loop_begin(p, &l, start);
    begin_scope(p);
    block(p);
    end_scope(p);
    emit_loop(p, start);
    patch_jump(p, exit_at);
    loop_end(p, &l);
    end_scope(p);
}

static void pop_locals_to(parser *p, int depth) {
    for (int i = p->c->nlocals - 1; i >= 0 && p->c->locals[i].depth > depth; i--) emit(p, OP_POP);
}

static void statement(parser *p) {
    if (match(p, T_LET)) let_stmt(p);
    else if (match(p, T_FN)) fn_stmt(p);
    else if (match(p, T_IF)) if_stmt(p);
    else if (match(p, T_WHILE)) while_stmt(p);
    else if (match(p, T_FOR)) for_stmt(p);
    else if (match(p, T_RETURN)) {
        if (p->c->toplevel) { error(p, "'return' outside a function"); return; }
        if (check(p, T_RBRACE) || check(p, T_SEMI) || p->tok.nl || check(p, T_EOF)) emit(p, OP_NIL);
        else expression(p);
        emit(p, OP_RETURN);
        end_statement(p);
    } else if (match(p, T_BREAK) || match(p, T_CONTINUE)) {
        bool brk = p->prev.t == T_BREAK;
        loop *l = p->c->loops;
        if (!l) { error(p, brk ? "'break' outside a loop" : "'continue' outside a loop"); return; }
        pop_locals_to(p, l->depth + (brk ? 0 : 0));
        if (brk) {
            if (l->nbreaks == 64) { error(p, "too many 'break's in one loop"); return; }
            l->breaks[l->nbreaks++] = emit_jump(p, OP_JUMP);
        } else {
            emit_loop(p, l->start);
        }
        end_statement(p);
    } else if (match(p, T_LBRACE)) {
        begin_scope(p);
        block(p);
        end_scope(p);
    } else if (match(p, T_SEMI)) {
    } else {
        expression(p);
        emit(p, OP_POP);
        end_statement(p);
    }
}

ofunc *xvc_compile(vm *m, const char *src, size_t len, const char *file) {
    parser p;
    memset(&p, 0, sizeof p);
    p.m = m;
    p.src = p.cur = src;
    p.end = src + len;
    p.file = file;
    p.line = 1;
    compiler c;
    memset(&c, 0, sizeof c);
    c.toplevel = true;
    c.f = func_new(m, "main");
    if (!c.f) return NULL;
    p.c = &c;
    advance(&p);
    while (!match(&p, T_EOF) && !p.had_error) statement(&p);
    emit(&p, OP_NIL);
    emit(&p, OP_RETURN);
    return p.had_error ? NULL : c.f;
}
