/* SQLite on itelx: the mathematics of libm.c (SQL math functions, FTS5 ranking) */
#ifndef SHIM_MATH_H
#define SHIM_MATH_H
#define NAN       (__builtin_nan(""))
#define INFINITY  (__builtin_inf())
#define HUGE_VAL  (__builtin_huge_val())
#define M_PI      3.14159265358979323846
#define isnan(x)  __builtin_isnan(x)
#define isinf(x)  __builtin_isinf(x)
double fabs(double x);
double floor(double x);
double ceil(double x);
double trunc(double x);
double round(double x);
double sqrt(double x);
double fmod(double x, double y);
double exp(double x);
double log(double x);
double log10(double x);
double log2(double x);
double log1p(double x);
double pow(double x, double y);
double sin(double x);
double cos(double x);
double tan(double x);
double asin(double x);
double acos(double x);
double atan(double x);
double atan2(double y, double x);
double sinh(double x);
double cosh(double x);
double tanh(double x);
double asinh(double x);
double acosh(double x);
double atanh(double x);
#endif
