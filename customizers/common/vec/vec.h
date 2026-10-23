/* vec: a vector database for data_place (embeddings for AI: search by
 * nearness, like the vector stores of AI applications).
 *
 * A collection holds vectors of one dimension, each with an id and JSON
 * metadata; search gives the k nearest to a query vector by cosine
 * similarity, Euclidean (L2) distance or dot product, exactly (every
 * vector is compared), optionally only among vectors whose metadata match.
 *
 * Text: a "text" collection turns text into vectors itself, by hashing its
 * words and word pairs into the dimensions (feature hashing, with Arabic
 * letters normalized). That finds texts that share words, not meaning: for
 * meaning, put in vectors made by an embedding model (import).
 *
 * The file (/data/vec/NAME.vec) is a header and a log of additions and
 * deletions; compact writes it again with only what is alive. */
#ifndef VEC_H
#define VEC_H

#include <json.h>

enum { VM_COSINE, VM_L2, VM_DOT };

struct vcoll {
    char path[ITX_PATH_MAX];
    char name[64];
    uint32_t dim, metric;
    bool text;                    /* turns text into vectors itself */
    int n, cap;                   /* live and deleted slots */
    long live;
    char **ids;
    float *vecs;                  /* n * dim */
    char **meta;                  /* JSON text, "" for none */
    char **texts;                 /* text collections: the text */
    bool *dead;
    int fd;
    int *hidx, hcap;              /* ids to slots */
};

struct vhit { int slot; float score; };

struct vcoll *vec_create(const char *path, const char *name, uint32_t dim, int metric, bool text, char *err, size_t elen);
struct vcoll *vec_open(const char *path, char *err, size_t elen);
void vec_close(struct vcoll *c);
/* adds (or replaces) a vector; text: for text collections */
int  vec_add(struct vcoll *c, const char *id, const float *v, const char *meta, const char *text, char *err, size_t elen);
int  vec_del(struct vcoll *c, const char *id);
int  vec_find(struct vcoll *c, const char *id);             /* the slot, or -1 */
/* the k nearest; where: JSON object of metadata that must match, or NULL */
int  vec_search(struct vcoll *c, const float *q, int k, const struct jv *where, struct vhit *out);
void vec_embed(const char *text, uint32_t dim, float *out); /* text -> a vector (feature hashing) */
int  vec_compact(struct vcoll *c);
const char *vec_metric_name(int m);
int  vec_parse_vector(const char *s, float *out, uint32_t dim);   /* "0.1,0.2,..." or [0.1, ...] -> count */

#endif
