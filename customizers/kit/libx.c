/* kit: C library functions libitx does not have (declared in kit.h). */
#include "kit.h"

size_t strcspn(const char *s, const char *reject) {
    size_t n = 0;
    for (; s[n]; n++)
        for (const char *r = reject; *r; r++)
            if (s[n] == *r) return n;
    return n;
}

size_t strspn(const char *s, const char *accept) {
    size_t n = 0;
    for (; s[n]; n++) {
        const char *a = accept;
        while (*a && *a != s[n]) a++;
        if (!*a) return n;
    }
    return n;
}

void *memchr(const void *s, int c, size_t n) {
    const unsigned char *p = s;
    for (size_t i = 0; i < n; i++) if (p[i] == (unsigned char)c) return (void *)(p + i);
    return NULL;
}

size_t strnlen(const char *s, size_t max) {
    size_t n = 0;
    while (n < max && s[n]) n++;
    return n;
}

int strcasecmp(const char *a, const char *b) {
    while (*a && tolower((unsigned char)*a) == tolower((unsigned char)*b)) { a++; b++; }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

int strncasecmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        int x = tolower((unsigned char)a[i]), y = tolower((unsigned char)b[i]);
        if (x != y || !x) return x - y;
    }
    return 0;
}

const char *k_strcasestr(const char *h, const char *n) {
    size_t nl = strlen(n);
    if (!nl) return h;
    for (; *h; h++) if (!strncasecmp(h, n, nl)) return h;
    return NULL;
}

int isupper(int c) { return c >= 'A' && c <= 'Z'; }
int islower(int c) { return c >= 'a' && c <= 'z'; }
int isxdigit(int c) { return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
int isprint(int c) { return c >= 0x20 && c < 0x7F; }
int ispunct(int c) { return isprint(c) && !isalnum(c) && c != ' '; }

/* a merge sort (stable), with a buffer from malloc; falls back to an
   insertion sort when there is no memory for it */
static void merge(char *a, char *tmp, size_t n, size_t w, int (*cmp)(const void *, const void *)) {
    if (n < 2) return;
    size_t mid = n / 2;
    merge(a, tmp, mid, w, cmp);
    merge(a + mid * w, tmp, n - mid, w, cmp);
    size_t i = 0, j = mid, k = 0;
    while (i < mid && j < n) {
        if (cmp(a + j * w, a + i * w) < 0) { memcpy(tmp + k * w, a + j * w, w); j++; }
        else { memcpy(tmp + k * w, a + i * w, w); i++; }
        k++;
    }
    while (i < mid) { memcpy(tmp + k * w, a + i * w, w); i++; k++; }
    while (j < n) { memcpy(tmp + k * w, a + j * w, w); j++; k++; }
    memcpy(a, tmp, n * w);
}

void qsort(void *base, size_t n, size_t w, int (*cmp)(const void *, const void *)) {
    if (n < 2 || !w) return;
    char *tmp = malloc(n * w);
    if (tmp) {
        merge(base, tmp, n, w, cmp);
        free(tmp);
        return;
    }
    char *a = base, *k = malloc(w);
    if (!k) return;
    for (size_t i = 1; i < n; i++) {
        memcpy(k, a + i * w, w);
        size_t j = i;
        while (j > 0 && cmp(a + (j - 1) * w, k) > 0) { memcpy(a + j * w, a + (j - 1) * w, w); j--; }
        memcpy(a + j * w, k, w);
    }
    free(k);
}
