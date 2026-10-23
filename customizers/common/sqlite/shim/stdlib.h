/* SQLite on itelx: memory and numbers of libitx and the kit */
#ifndef SHIM_STDLIB_H
#define SHIM_STDLIB_H
#include <stddef.h>
void *malloc(size_t n);
void *calloc(size_t n, size_t m);
void *realloc(void *p, size_t n);
void  free(void *p);
void  qsort(void *base, size_t n, size_t w, int (*cmp)(const void *, const void *));
int   atoi(const char *s);
long  atol(const char *s);
long  strtol(const char *s, char **end, int base);
unsigned long strtoul(const char *s, char **end, int base);
void  exit(int code) __attribute__((noreturn));
#endif
