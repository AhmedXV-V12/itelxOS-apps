/* SQLite on itelx: SQLite formats its own text; only the names it mentions */
#ifndef SHIM_STDIO_H
#define SHIM_STDIO_H
#include <stddef.h>
#include <stdarg.h>
typedef struct shim_file FILE;
int snprintf(char *buf, size_t size, const char *fmt, ...);
int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
#endif
