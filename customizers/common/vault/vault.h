/* The vault of File saver: /vault, where files of any kind are kept safe.
 *
 *   /vault/<Category>/...     Documents, Pictures, Music, Videos, Apps, Archives,
 *                             Code, Data, Other, Inbox (new files), Private
 *   /vault/.vault/index.txt   SHA-256, size and path of every file: check
 *                             finds files that changed or went missing
 *   /vault/.vault/key         the vault's key, locked with the password
 *   /vault/.versions/         a file's earlier versions, when it is replaced
 *   /vault/.trash/            deleted files, until the trash is emptied
 *
 * Locked files (NAME.locked) are encrypted with ChaCha20 and authenticated
 * with HMAC-SHA-256 (encrypt then MAC). The keys are a random master key;
 * the password (PBKDF2-SHA-256, 200000 rounds) only locks that key, so the
 * password can change without touching the files. */
#ifndef VAULT_H
#define VAULT_H

#include <kit.h>

#define VAULT  "/vault"
#define VMETA  "/vault/.vault"
#define VTRASH "/vault/.trash"
#define VVERS  "/vault/.versions"
#define VINDEX VMETA "/index.txt"
#define VKEY   VMETA "/key"
#define LOCKED_EXT ".locked"

extern const char *const vault_folders[];
extern const int vault_nfolders;

void vault_init(void);
const char *vault_category(const char *name);        /* the folder for a file name */
bool vault_hidden(const char *name);                  /* .vault .trash .versions */
const char *vault_base(const char *path);

/* files */
int  vault_sha256_file(const char *path, uint8_t out[32], uint64_t *size);
/* copies src (a file, or a folder with everything in it) into the vault:
   into folder (a vault path) or, without one, into its category; a file
   with the same name keeps its earlier version in /vault/.versions */
int  vault_store(const char *src, const char *folder, char *dst, size_t dlen, int *files, uint64_t *bytes);
int  vault_organize(const char *dir, int *moved);     /* loose files of dir into their categories */
int  vault_trash(const char *path);
int  vault_trash_restore(const char *name, char *where, size_t len);
int  vault_trash_empty(int *n);
int  vault_versions(const char *path, char names[][128], int max);   /* the earlier versions, newest first */

/* the index */
struct vcheck { int ok, changed, missing, added; };
int  vault_index_update(void);                        /* hashes what is new or changed */
int  vault_check(struct vcheck *r, void (*report)(const char *path, const char *what));

/* the password and locked files */
bool vault_has_password(void);
bool vault_unlocked(void);                            /* the key is in memory (this program) */
int  vault_open(const char *password);                /* 0, or -EACCES for a wrong password */
int  vault_set_password(const char *old, const char *new_password);
int  vault_lock_file(const char *path);               /* path -> path.locked */
int  vault_unlock_file(const char *path);             /* path.locked -> path */
void vault_forget_key(void);

/* USB backup: a copy of /vault on a stick (vault-backup/), only what changed */
int  vault_backup(const char *stick, int *copied, int *same, uint64_t *bytes, void (*progress)(const char *));
int  vault_restore(const char *stick, int *copied, int *same, void (*progress)(const char *));

#endif
