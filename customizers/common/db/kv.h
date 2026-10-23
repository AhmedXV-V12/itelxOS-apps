/* kv: a key-value database in the style of Redis, for data_place.
 *
 * Keys hold strings (and counters), hashes, lists or sets, with an
 * optional expiry. The data live in memory; every change is appended to
 * the database's file (an append-only file of RESP commands, the format
 * Redis uses for its AOF), which is read back at the start and rewritten
 * smaller when it has grown (SAVE does it at once).
 *
 * Commands get their arguments as byte strings and answer in RESP, the
 * protocol of Redis: the same engine answers the kv shell (which shows the
 * answers as text) and redis-cli or any Redis client over the network
 * (kv serve). */
#ifndef KV_H
#define KV_H

#include <kit.h>

struct kbuf { char *p; size_t n, cap; };
void kb_put(struct kbuf *b, const void *data, size_t len);
void kb_str(struct kbuf *b, const char *s);
void kb_printf(struct kbuf *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void kb_free(struct kbuf *b);

struct kvdb;
struct kvdb *kv_open(const char *path, char *err, size_t elen);
void kv_close(struct kvdb *db);
/* one command; its RESP answer is appended to out */
void kv_exec(struct kvdb *db, int argc, char **argv, size_t *lens, struct kbuf *out);
long kv_size(struct kvdb *db);              /* keys */
int  kv_save(struct kvdb *db);              /* rewrites the file from what is in memory */
void kv_expire_some(struct kvdb *db);       /* removes keys whose time is up */

/* RESP: a command from bytes (an array of bulk strings, or an inline line);
   the number of bytes it took, 0 when more are needed, -1 on garbage */
long resp_parse(const char *p, size_t n, int *argc, char ***argv, size_t **lens);
void resp_free(int argc, char **argv, size_t *lens);
/* an answer as redis-cli shows it */
void resp_print(const char *p, size_t n, struct kbuf *out);
/* a typed line split into arguments ("quotes", 'quotes', \x escapes) */
int  kv_split(const char *line, char ***argv, size_t **lens);

#endif
