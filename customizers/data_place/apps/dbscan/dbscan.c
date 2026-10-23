/* dbscan: what database or data file a file is, by what is inside it (its
 * signature, not its name), with details where the format tells them.
 *
 *   dbscan [PATH ...]      files, or folders with everything in them (/data by default)
 *
 * Opened by data_place's tools: SQLite, kv (Redis AOF), doc, vec, CSV, JSON
 * and JSON lines. Recognized, to be opened elsewhere: other databases
 * (Redis RDB, LMDB, Berkeley DB, LevelDB/RocksDB, DuckDB, MySQL InnoDB,
 * Access, dBase, Firebird, H2, SQL Server, ...), data and AI formats
 * (Parquet, Arrow/Feather, ORC, Avro, HDF5, NumPy, PyTorch, safetensors,
 * GGUF, ONNX, TFRecord, Pickle, ...), archives and spreadsheets. */
#include <kit.h>
#include <sqlite3.h>
#include <itxsqlite.h>

static long files, known;

struct sig { const char *name, *kind; long off; const char *magic; int len; };

/* signatures (offset, bytes) */
static const struct sig sigs[] = {
    { "SQLite 3 database", "SQL", 0, "SQLite format 3\0", 16 },
    { "data_place vector collection", "vector", 0, "ITXVEC01", 8 },
    { "Redis RDB snapshot", "key-value", 0, "REDIS", 5 },
    { "DuckDB database", "SQL", 8, "DUCK", 4 },
    { "LMDB database", "key-value", 16, "\xde\xc0\xef\xbe", 4 },
    { "Berkeley DB (hash)", "key-value", 12, "\x61\x15\x06\x00", 4 },
    { "Berkeley DB (B-tree)", "key-value", 12, "\x62\x31\x05\x00", 4 },
    { "Berkeley DB (B-tree)", "key-value", 12, "\x00\x05\x31\x62", 4 },
    { "Microsoft Access database", "SQL", 4, "Standard Jet DB", 15 },
    { "Microsoft Access database (accdb)", "SQL", 4, "Standard ACE DB", 15 },
    { "Firebird / InterBase database", "SQL", 0, "\x01\x00\x39\x30", 4 },
    { "H2 database", "SQL", 0, "-- H2 0.5/B --", 14 },
    { "SQL Server data file", "SQL", 0, "\x01\x0f\x00\x00", 4 },
    { "Apache Parquet", "columnar", 0, "PAR1", 4 },
    { "Apache Arrow / Feather", "columnar", 0, "ARROW1", 6 },
    { "Apache ORC", "columnar", 0, "ORC", 3 },
    { "Apache Avro", "rows", 0, "Obj\x01", 4 },
    { "HDF5", "arrays", 0, "\x89HDF\r\n\x1a\n", 8 },
    { "NumPy array (.npy)", "arrays", 0, "\x93NUMPY", 6 },
    { "GGUF model (llama.cpp)", "AI model", 0, "GGUF", 4 },
    { "GGML model", "AI model", 0, "lmgg", 4 },
    { "TensorFlow Lite model", "AI model", 4, "TFL3", 4 },
    { "Python pickle", "objects", 0, "\x80\x04\x95", 3 },
    { "Python pickle", "objects", 0, "\x80\x05\x95", 3 },
    { "Python pickle", "objects", 0, "\x80\x02", 2 },
    { "Zip (also .xlsx, .docx, .npz, PyTorch .pt)", "archive", 0, "PK\x03\x04", 4 },
    { "gzip", "archive", 0, "\x1f\x8b", 2 },
    { "Zstandard", "archive", 0, "\x28\xb5\x2f\xfd", 4 },
    { "XZ", "archive", 0, "\xfd" "7zXZ", 5 },
    { "7-Zip", "archive", 0, "7z\xbc\xaf\x27\x1c", 6 },
    { "Excel 97-2003 / OLE document", "spreadsheet", 0, "\xd0\xcf\x11\xe0\xa1\xb1\x1a\xe1", 8 },
    { "PDF", "document", 0, "%PDF", 4 },
    { "XVR executable (itelx)", "program", 0, "XVR\x1a", 4 },
};

static bool has(const unsigned char *b, size_t n, const struct sig *s) {
    return (size_t)s->off + (size_t)s->len <= n && !memcmp(b + s->off, s->magic, (size_t)s->len);
}

static void sqlite_details(const char *path) {
    sqlite3 *db;
    if (sqlite3_open_v2(path, &db, SQLITE_OPEN_READONLY, NULL)) { sqlite3_close(db); return; }
    sqlite3_stmt *st;
    if (!sqlite3_prepare_v2(db, "SELECT (SELECT count(*) FROM sqlite_schema WHERE type='table'), "
                                "(SELECT count(*) FROM sqlite_schema WHERE type='index'), "
                                "(SELECT count(*) FROM sqlite_schema WHERE type='table' AND name LIKE 'c\\_%' ESCAPE '\\'), "
                                "(SELECT page_size FROM pragma_page_size)", -1, &st, NULL) && sqlite3_step(st) == SQLITE_ROW) {
        int t = sqlite3_column_int(st, 0), ix = sqlite3_column_int(st, 1), c = sqlite3_column_int(st, 2), ps = sqlite3_column_int(st, 3);
        printf("      %d table(s), %d index(es), pages of %d bytes%s\n", t, ix, ps, c && c == t ? "; a doc database (collections)" : "");
        printf("      open it with: %s %s\n", c && c == t ? "doc" : "sql", path);
    }
    sqlite3_finalize(st);
    sqlite3_close(db);
}

static void scan_file(const char *path, uint64_t size) {
    files++;
    int fd = open(path, O_RDONLY);
    if (fd < 0) return;
    unsigned char b[4096];
    ssize_t n = read(fd, b, sizeof b);
    close(fd);
    if (n < 0) return;
    char h[32];
    if (size < 1024) snprintf(h, sizeof h, "%lu B", (unsigned long)size);
    else if (size < 1048576) snprintf(h, sizeof h, "%lu KiB", (unsigned long)(size / 1024));
    else snprintf(h, sizeof h, "%lu MiB", (unsigned long)(size >> 20));
    for (size_t i = 0; i < sizeof sigs / sizeof sigs[0]; i++) {
        if (!has(b, (size_t)n, &sigs[i])) continue;
        known++;
        printf("%-12s %9s  %s\n    %s\n", sigs[i].kind, h, path, sigs[i].name);
        if (i == 0) sqlite_details(path);
        else if (i == 1) {
            uint32_t dim, m;
            memcpy(&dim, b + 8, 4);
            memcpy(&m, b + 12, 4);
            printf("      %u dimensions, %s%s; open it with: vec info NAME\n", dim, (m & 0xFF) == 1 ? "l2" : (m & 0xFF) == 2 ? "dot" : "cosine",
                   m & 0x100 ? ", made from text" : "");
        } else if (!strncmp(sigs[i].name, "NumPy", 5) && n > 10) {
            int hl = b[8] | b[9] << 8;
            char hdr[256];
            snprintf(hdr, sizeof hdr, "%.*s", hl < 240 && 10 + hl <= n ? hl : 0, (const char *)b + 10);
            printf("      %s\n", hdr);
        } else if (!strncmp(sigs[i].name, "GGUF", 4) && n >= 24) {
            uint32_t ver;
            uint64_t tensors, kvs;
            memcpy(&ver, b + 4, 4);
            memcpy(&tensors, b + 8, 8);
            memcpy(&kvs, b + 16, 8);
            printf("      GGUF version %u, %lu tensor(s), %lu metadata key(s)\n", ver, (unsigned long)tensors, (unsigned long)kvs);
        } else if (!strncmp(sigs[i].name, "Redis", 5)) {
            printf("      RDB version %.4s (a Redis snapshot; kv reads the AOF form)\n", (const char *)b + 5);
        }
        return;
    }
    /* signatures that are not at a fixed place */
    if (n >= 8) {
        uint64_t hl;
        memcpy(&hl, b, 8);
        if (hl > 2 && hl < 100000000 && b[8] == '{' && b[9] == '"') {
            known++;
            printf("%-12s %9s  %s\n    safetensors (AI model weights), a header of %lu bytes\n", "AI model", h, path, (unsigned long)hl);
            return;
        }
    }
    if (n >= 16 && b[0] == 0x08 && ends_with(path, ".onnx")) {
        known++;
        printf("%-12s %9s  %s\n    ONNX model\n", "AI model", h, path);
        return;
    }
    if (ends_with(path, ".tfrecord") || ends_with(path, ".tfrecords")) {
        known++;
        printf("%-12s %9s  %s\n    TFRecord (TensorFlow examples)\n", "AI data", h, path);
        return;
    }
    if (ends_with(path, ".ibd") || ends_with(path, ".frm")) {
        known++;
        printf("%-12s %9s  %s\n    MySQL / MariaDB InnoDB table file\n", "SQL", h, path);
        return;
    }
    if (ends_with(path, ".dbf") && n > 32 && (b[0] == 0x03 || b[0] == 0x83 || b[0] == 0x30 || b[0] == 0xF5)) {
        uint32_t recs;
        memcpy(&recs, b + 4, 4);
        known++;
        printf("%-12s %9s  %s\n    dBase / FoxPro table, %u record(s)\n", "SQL", h, path, recs);
        return;
    }
    if ((ends_with(path, ".ldb") || ends_with(path, ".sst")) && size >= 48) {
        known++;
        printf("%-12s %9s  %s\n    LevelDB / RocksDB table (sorted string table)\n", "key-value", h, path);
        return;
    }
    /* text: kv files, JSON lines, JSON, CSV */
    bool text = n > 0;
    for (ssize_t i = 0; i < n; i++) if (!b[i] || (b[i] < 0x09)) { text = false; break; }
    if (!text) return;
    int lines = 0, commas = -1, same = 1;
    for (ssize_t i = 0, c = 0; i < n; i++) {
        if (b[i] == ',') c++;
        if (b[i] == '\n') { lines++; if (commas < 0) commas = (int)c; else if (c != commas) same = 0; c = 0; }
    }
    const char *what = NULL, *kind = "text", *tool = NULL;
    if (b[0] == '*' && b[1] >= '1' && b[1] <= '9') { what = "kv database (Redis AOF commands)"; kind = "key-value"; tool = "kv"; }
    else if (b[0] == '{' && lines > 1) { what = "JSON lines (one document per line)"; kind = "documents"; tool = "doc import / vec import"; }
    else if (b[0] == '{' || b[0] == '[') { what = "JSON"; kind = "documents"; tool = "doc import"; }
    else if (lines > 1 && commas > 0 && same) { what = "CSV table"; kind = "table"; tool = "sql: .import FILE TABLE"; }
    if (!what) return;
    known++;
    printf("%-12s %9s  %s\n    %s%s%s\n", kind, h, path, what, tool ? "; " : "", tool ? tool : "");
    if (!strcmp(kind, "table")) printf("      %d columns, first line: %.*s\n", commas + 1, (int)strcspn((const char *)b, "\r\n") < 70 ? (int)strcspn((const char *)b, "\r\n") : 70, b);
}

static void scan(const char *path) {
    struct itx_stat st;
    if (stat(path, &st)) { printf("%s: no such file or folder\n", path); return; }
    if (st.type == VT_FILE) { scan_file(path, st.size); return; }
    if (st.type != VT_DIR) return;
    int fd = open(path, O_RDONLY);
    if (fd < 0) return;
    char (*names)[256] = malloc(512 * 256);
    int n = 0;
    struct itx_dirent de;
    while (readdir(fd, &de) == 1 && n < 512) strlcpy(names[n++], de.name, 256);
    close(fd);
    for (int i = 0; i < n; i++) {
        if (!strcmp(names[i], "ram") && !strcmp(path, "/")) continue;     /* the RAM, not files */
        char p[ITX_PATH_MAX];
        snprintf(p, sizeof p, "%s/%s", strcmp(path, "/") ? path : "", names[i]);
        scan(p);
    }
    free(names);
}

int main(int argc, char **argv) {
    itx_sqlite_init(16);
    if (argc < 2) scan("/data");
    for (int i = 1; i < argc; i++) scan(argv[i]);
    printf("%ld file(s) looked at, %ld recognized\n", files, known);
    return 0;
}
