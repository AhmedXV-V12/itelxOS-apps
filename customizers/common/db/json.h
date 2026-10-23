/* json: JSON values for data_place (documents, filters, the HTTP API).
 * Numbers keep their text as written, so nothing is lost on the way to
 * SQLite; num holds their value too. */
#ifndef JSON_H
#define JSON_H

#include "kv.h"

enum { J_NULL, J_FALSE, J_TRUE, J_NUM, J_STR, J_ARR, J_OBJ };

struct jv {
    int t;
    double num;
    char *s;                 /* J_STR: the string (UTF-8); J_NUM: the number as written */
    size_t slen;
    int n;                   /* J_ARR, J_OBJ: the items */
    struct jv **items;
    char **keys;             /* J_OBJ */
};

/* the value at the start of text (spaces before and after allowed); NULL
   with err set on bad JSON. *used (may be NULL) gets the bytes taken. */
struct jv *json_parse(const char *text, size_t len, size_t *used, char *err, size_t elen);
void json_free(struct jv *v);
struct jv *json_get(const struct jv *obj, const char *key);
void json_write(const struct jv *v, struct kbuf *out);         /* compact */
void json_write_str(const char *s, size_t n, struct kbuf *out);  /* a JSON string */
double json_strtod(const char *s, char **end);

#endif
