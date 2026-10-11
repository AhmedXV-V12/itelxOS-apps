/* XVC, the itelx programming language: shared definitions of the compiler
 * (xvc_compile.c) and the virtual machine (xvc_vm.c).
 *
 * A program is compiled in one pass into bytecode for a stack machine, then
 * run. Values are integers, booleans, nil, strings (UTF-8; length and
 * indexing count characters), lists, maps (string or integer keys),
 * ranges and functions. Memory is managed by a mark-and-sweep collector.
 * See docs/XVC.md for the language.
 */
#ifndef XVC_H
#define XVC_H

#include <itx.h>

typedef enum { V_NIL, V_BOOL, V_INT, V_OBJ } vtype;

typedef struct obj obj;
typedef struct {
    vtype t;
    union { bool b; int64_t i; obj *o; } as;
} value;

typedef enum { O_STR, O_LIST, O_MAP, O_FUNC, O_NATIVE, O_RANGE } otype;

struct obj {
    otype t;
    bool marked;
    obj *next;
};

typedef struct {
    obj h;
    size_t len;          /* bytes */
    size_t chars;        /* code points */
    uint32_t hash;
    char s[];
} ostr;

typedef struct {
    obj h;
    size_t len, cap;
    value *v;
} olist;

typedef struct {
    value key;
    value val;
    bool used;
} mslot;

typedef struct {
    obj h;
    size_t count, cap;
    mslot *slots;
} omap;

typedef struct {
    obj h;
    int arity;
    int nlocals;
    ostr *name;
    uint8_t *code;
    int *lines;
    size_t len, cap;
    value *consts;
    size_t nconsts, cconsts;
} ofunc;

struct vm;
typedef value (*native_fn)(struct vm *vm, int argc, value *argv);

typedef struct {
    obj h;
    const char *name;
    native_fn fn;
    int min, max;        /* arguments; max -1: any */
} onative;

typedef struct {
    obj h;
    int64_t start, end, step;
} orange;

/* ---- bytecode */
enum {
    OP_CONST, OP_NIL, OP_TRUE, OP_FALSE, OP_POP,
    OP_GET_LOCAL, OP_SET_LOCAL, OP_GET_GLOBAL, OP_DEF_GLOBAL, OP_SET_GLOBAL,
    OP_GET_INDEX, OP_SET_INDEX,
    OP_EQ, OP_NE, OP_LT, OP_LE, OP_GT, OP_GE,
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD, OP_NEG, OP_NOT,
    OP_JUMP, OP_JUMP_IF_FALSE, OP_LOOP,
    OP_CALL, OP_RETURN,
    OP_LIST, OP_MAP,
    OP_ITER_NEXT,          /* [iterable, index] -> pushes the next item or jumps */
    OP_DUP,
};

#define VM_STACK 8192
#define VM_FRAMES 256

typedef struct {
    ofunc *f;
    uint8_t *ip;
    value *base;
} frame;

typedef struct vm {
    value stack[VM_STACK];
    value *sp;
    frame frames[VM_FRAMES];
    int nframes;
    omap *globals;
    obj *objects;
    size_t allocated, next_gc;
    const char *file;
    bool gc_off;
    bool failed;
    char error[256];
    int exit_code;
    bool exiting;
    uint64_t rng;
} vm;

/* values */
static inline value v_nil(void) { value v; v.t = V_NIL; v.as.i = 0; return v; }
static inline value v_bool(bool b) { value v; v.t = V_BOOL; v.as.i = 0; v.as.b = b; return v; }
static inline value v_int(int64_t i) { value v; v.t = V_INT; v.as.i = i; return v; }
static inline value v_obj(void *o) { value v; v.t = V_OBJ; v.as.o = (obj *)o; return v; }
static inline bool is_obj(value v, otype t) { return v.t == V_OBJ && v.as.o->t == t; }
#define AS_STR(v)  ((ostr *)(v).as.o)
#define AS_LIST(v) ((olist *)(v).as.o)
#define AS_MAP(v)  ((omap *)(v).as.o)
#define AS_FUNC(v) ((ofunc *)(v).as.o)

/* objects (xvc_vm.c) */
void  *vm_alloc(vm *m, size_t size, otype t);
ostr  *str_new(vm *m, const char *s, size_t len);
olist *list_new(vm *m);
bool   list_push(vm *m, olist *l, value v);
omap  *map_new(vm *m);
bool   map_get(omap *mp, value key, value *out);
bool   map_set(vm *m, omap *mp, value key, value val);
ofunc *func_new(vm *m, const char *name);
int    func_const(vm *m, ofunc *f, value v);
bool   func_emit(vm *m, ofunc *f, uint8_t b, int line);
bool   values_equal(value a, value b);
const char *type_name(value v);
void   vm_runtime_error(vm *m, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void   vm_init(vm *m, const char *file);
int    vm_run(vm *m, ofunc *main_fn);
void   vm_define_natives(vm *m, int argc, char **argv);
int    value_to_string(value v, char *buf, size_t len, bool quote);

/* compiler (xvc_compile.c) */
ofunc *xvc_compile(vm *m, const char *src, size_t len, const char *file);

#endif
