/* The mathematics SQLite needs (its math functions, FTS5's ranking), with
 * the x87 instructions of the processor: they work in 80-bit precision and
 * give double results as exact as a C library's for these uses. */

typedef long double ld;

#define X87_1(name, insn) \
    double name(double x) { ld r = x; __asm__(insn : "+t"(r)); return (double)r; }

double fabs(double x) { return __builtin_fabs(x); }
X87_1(sqrt, "fsqrt")
X87_1(sin, "fsin")
X87_1(cos, "fcos")

double tan(double x) {
    ld r, one;
    __asm__("fptan" : "=t"(one), "=u"(r) : "0"((ld)x));   /* leaves 1 on top, the tangent below */
    (void)one;
    return (double)r;
}

double atan2(double y, double x) {
    ld r;
    __asm__("fpatan" : "=t"(r) : "0"((ld)x), "u"((ld)y) : "st(1)");
    return (double)r;
}

double atan(double x) { return atan2(x, 1.0); }
double asin(double x) { return atan2(x, sqrt((1.0 - x) * (1.0 + x))); }
double acos(double x) { return atan2(sqrt((1.0 - x) * (1.0 + x)), x); }

static ld rnd(ld x, unsigned short mode) {
    unsigned short cw, nw;
    __asm__ volatile("fnstcw %0" : "=m"(cw));
    nw = (unsigned short)((cw & ~0x0C00) | mode);
    __asm__ volatile("fldcw %0" :: "m"(nw));
    __asm__("frndint" : "+t"(x));
    __asm__ volatile("fldcw %0" :: "m"(cw));
    return x;
}

double floor(double x) { return (double)rnd(x, 0x0400); }
double ceil(double x) { return (double)rnd(x, 0x0800); }
double trunc(double x) { return (double)rnd(x, 0x0C00); }
double round(double x) { return x < 0 ? -floor(-x + 0.5) : floor(x + 0.5); }

double fmod(double x, double y) {
    ld r = x, d = y;
    unsigned short sw;
    do {
        __asm__("fprem; fnstsw %%ax" : "+t"(r), "=a"(sw) : "u"(d));
    } while (sw & 0x0400);
    return (double)r;
}

/* log2(x) by fyl2x; logs of other bases from it */
static ld lg2(ld x) {
    ld r;
    __asm__("fyl2x" : "=t"(r) : "0"(x), "u"((ld)1.0) : "st(1)");
    return r;
}

double log2(double x) { return (double)lg2(x); }
double log(double x) { return (double)(lg2(x) * 0.693147180559945309417232121458L); }
double log10(double x) { return (double)(lg2(x) * 0.301029995663981195213738894724L); }
double log1p(double x) { return log(1.0 + x); }

/* 2^x = 2^int * 2^frac (f2xm1 takes -1..1) */
static ld exp2l_(ld x) {
    if (x != x) return x;
    if (x > 16384) return __builtin_infl();
    if (x < -16384) return 0;
    ld i = rnd(x, 0x0C00), f = x - i, r;
    __asm__("f2xm1" : "=t"(r) : "0"(f));
    r += 1.0L;
    __asm__("fscale" : "+t"(r) : "u"(i));
    return r;
}

double exp(double x) { return (double)exp2l_(x * 1.44269504088896340735992468100L); }

double pow(double x, double y) {
    if (y == 0) return 1.0;
    if (x == 0) return y > 0 ? 0.0 : __builtin_inf();
    if (x < 0) {
        /* only whole powers of a negative number */
        ld yi = rnd(y, 0x0C00);
        if (yi != y) return __builtin_nan("");
        double r = (double)exp2l_((ld)y * lg2(-(ld)x));
        long long n = (long long)yi;
        return n & 1 ? -r : r;
    }
    return (double)exp2l_((ld)y * lg2(x));
}

double sinh(double x) { double e = exp(x); return (e - 1.0 / e) / 2; }
double cosh(double x) { double e = exp(x); return (e + 1.0 / e) / 2; }
double tanh(double x) {
    if (x > 20) return 1.0;
    if (x < -20) return -1.0;
    double e = exp(2 * x);
    return (e - 1) / (e + 1);
}
double asinh(double x) { return x < 0 ? -log(-x + sqrt(x * x + 1)) : log(x + sqrt(x * x + 1)); }
double acosh(double x) { return log(x + sqrt(x * x - 1)); }
double atanh(double x) { return 0.5 * log((1 + x) / (1 - x)); }
