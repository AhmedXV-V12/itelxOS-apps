/* SQLite on itelx: the start (itxvfs.c). Programs include sqlite3.h and call
 * itx_sqlite_init once, before anything else of SQLite: SQLite then takes
 * its memory from one block of heap_mib MiB (MEMSYS5). */
#ifndef ITXSQLITE_H
#define ITXSQLITE_H
int itx_sqlite_init(int heap_mib);
#endif
