/* doc: a document database (JSON documents in collections, queried the way
 * MongoDB is) for data_place, kept in SQLite.
 *
 * A database is one SQLite file (/data/doc/NAME.db); a collection is a
 * table c_NAME (id, doc) whose doc column holds the JSON document. Filters
 * and updates in MongoDB's form are turned into SQL with SQLite's JSON
 * functions, every value bound as a parameter, so the documents get
 * SQLite's transactions, journal and indexes, and the file can also be
 * opened with sql.
 *
 *   filters  {"age": 30}  {"age": {"$gt": 20, "$lte": 40}}  {"tags": "x"} (in an array)
 *            $eq $ne $gt $gte $lt $lte $in $nin $exists $size $type $contains $like $not
 *            $and $or $nor; "a.b" reaches into objects, "a.0" into arrays
 *   updates  $set $unset $inc $mul $push $pull $rename, or a whole new document
 */
#ifndef DOC_H
#define DOC_H

#include <json.h>
#include <sqlite3.h>

struct docdb { sqlite3 *db; char path[ITX_PATH_MAX]; };

struct docdb *doc_open(const char *path, char *err, size_t elen);
void doc_close(struct docdb *d);
bool doc_valid_name(const char *name);
int  doc_collections(struct docdb *d, char names[][64], long counts[], int max);
int  doc_insert(struct docdb *d, const char *coll, const struct jv *doc, char *id, size_t idlen, char *err, size_t elen);
/* the documents a filter finds; each is given to fn as JSON text. limit 0:
   all. Returns how many, or < 0 with err set. */
long doc_find(struct docdb *d, const char *coll, const struct jv *filter, const struct jv *sort, const struct jv *fields,
              long limit, long skip, void (*fn)(const char *json, void *ctx), void *ctx, char *err, size_t elen);
long doc_count(struct docdb *d, const char *coll, const struct jv *filter, char *err, size_t elen);
long doc_update(struct docdb *d, const char *coll, const struct jv *filter, const struct jv *update, bool many, char *err, size_t elen);
long doc_delete(struct docdb *d, const char *coll, const struct jv *filter, bool many, char *err, size_t elen);
long doc_distinct(struct docdb *d, const char *coll, const char *field, const struct jv *filter,
                  void (*fn)(const char *json, void *ctx), void *ctx, char *err, size_t elen);
int  doc_index(struct docdb *d, const char *coll, const char *field, char *err, size_t elen);
int  doc_drop(struct docdb *d, const char *coll);

#endif
