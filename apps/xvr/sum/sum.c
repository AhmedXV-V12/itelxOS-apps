/* sum: checksums of files, to know that a file copied from a USB stick or
 * downloaded is exactly the original.
 *
 *   sum <files...>          SHA-256 of each file, written the way sha256sum
 *                           writes it ("<hash>  <file>")
 *   sum -crc <files...>     CRC-32 (the checksum zip files and XVR use)
 *   sum -c <list>           checks the files of a list written by sum or by
 *                           sha256sum (e.g. SHA256SUMS); names in the list
 *                           are taken from the list's own folder
 *
 * On Windows the same SHA-256 comes from: certutil -hashfile <file> SHA256
 * On Linux: sha256sum <file>
 */
#include <ui.h>

/* ------------------------------------------------------------ SHA-256 */

typedef struct {
    uint32_t h[8];
    uint8_t buf[64];
    size_t nbuf;
    uint64_t bytes;
} sha256;

static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha_block(sha256 *s, const uint8_t *p) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 | (uint32_t)p[i * 4 + 2] << 8 | p[i * 4 + 3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3], e = s->h[4], f = s->h[5], g = s->h[6], h = s->h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = h + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
        uint32_t t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d;
    s->h[4] += e; s->h[5] += f; s->h[6] += g; s->h[7] += h;
}

static void sha_init(sha256 *s) {
    static const uint32_t H0[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    memcpy(s->h, H0, sizeof H0);
    s->nbuf = 0;
    s->bytes = 0;
}

static void sha_update(sha256 *s, const uint8_t *p, size_t n) {
    s->bytes += n;
    while (n) {
        if (!s->nbuf && n >= 64) { sha_block(s, p); p += 64; n -= 64; continue; }
        size_t k = 64 - s->nbuf < n ? 64 - s->nbuf : n;
        memcpy(s->buf + s->nbuf, p, k);
        s->nbuf += k; p += k; n -= k;
        if (s->nbuf == 64) { sha_block(s, s->buf); s->nbuf = 0; }
    }
}

static void sha_final(sha256 *s, char hex[65]) {
    uint64_t bits = s->bytes * 8;
    uint8_t pad = 0x80, zero = 0, len[8];
    sha_update(s, &pad, 1);
    while (s->nbuf != 56) sha_update(s, &zero, 1);
    for (int i = 0; i < 8; i++) len[i] = (uint8_t)(bits >> (56 - 8 * i));
    sha_update(s, len, 8);
    for (int i = 0; i < 8; i++) snprintf(hex + i * 8, 9, "%08x", s->h[i]);
}

/* ------------------------------------------------------------- CRC-32 */

static uint32_t crc_tab[256];

static uint32_t crc32_update(uint32_t crc, const uint8_t *p, size_t n) {
    if (!crc_tab[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? (c >> 1) ^ 0xEDB88320u : c >> 1;
            crc_tab[i] = c;
        }
    crc = ~crc;
    while (n--) crc = crc_tab[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

/* ------------------------------------------------------------ files */

static uint8_t buf[65536];

/* reads a file through; 0 or a negative error */
static int digest(const char *path, char sha[65], uint32_t *crc, uint64_t *size) {
    struct itx_stat st;
    int r = stat(path, &st);
    if (r) return r;
    if (st.type == VT_DIR) return -EISDIR;
    int fd = open(path, O_RDONLY);
    if (fd < 0) return fd;
    sha256 s;
    sha_init(&s);
    uint32_t c = 0;
    uint64_t total = 0;
    for (;;) {
        ssize_t n = read(fd, buf, sizeof buf);
        if (n < 0) { close(fd); return (int)n; }
        if (n == 0) break;
        sha_update(&s, buf, (size_t)n);
        c = crc32_update(c, buf, (size_t)n);
        total += (uint64_t)n;
    }
    close(fd);
    sha_final(&s, sha);
    *crc = c;
    *size = total;
    return 0;
}

static bool is_hex(const char *s, int n) {
    for (int i = 0; i < n; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
    }
    return true;
}

static int check(const char *list) {
    size_t len;
    char *text = read_file(list, &len, 4u << 20);
    if (!text) { printf("sum: %s: cannot read it\n", list); return 2; }
    char dir[ITX_PATH_MAX] = "";
    const char *slash = strrchr(list, '/');
    if (slash) {
        size_t n = (size_t)(slash - list) + 1;
        if (n >= sizeof dir) n = sizeof dir - 1;
        memcpy(dir, list, n);
        dir[n] = 0;
    }
    int ok = 0, bad = 0, missing = 0, lines = 0;
    for (char *line = text; line && *line;) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;
        size_t l = strlen(line);
        if (l && line[l - 1] == '\r') line[--l] = 0;
        /* "<64 hex>  name" or "<64 hex> *name" */
        if (l > 66 && is_hex(line, 64) && line[64] == ' ' && (line[65] == ' ' || line[65] == '*')) {
            lines++;
            const char *name = line + 66;
            char path[ITX_PATH_MAX];
            if (name[0] == '/') strlcpy(path, name, sizeof path);
            else snprintf(path, sizeof path, "%s%s", dir, name);
            char sha[65];
            uint32_t crc;
            uint64_t size;
            int r = digest(path, sha, &crc, &size);
            if (r) {
                printf(C_YELLOW "%s: MISSING (%s)" C_RESET "\n", name, strerror(r));
                missing++;
            } else {
                bool same = true;
                for (int i = 0; i < 64; i++) {
                    char a = line[i];
                    if (a >= 'A' && a <= 'F') a = (char)(a - 'A' + 'a');
                    if (a != sha[i]) same = false;
                }
                if (same) { printf("%s: " C_GREEN "OK" C_RESET "\n", name); ok++; }
                else { printf("%s: " C_RED "FAILED" C_RESET " (the file is different)\n", name); bad++; }
            }
        }
        line = nl ? nl + 1 : NULL;
    }
    free(text);
    if (!lines) { printf("sum: %s has no lines \"<sha256>  <file>\"\n", list); return 2; }
    printf("%d OK, %d failed, %d missing\n", ok, bad, missing);
    return bad || missing ? 1 : 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("use: sum <files...>   |   sum -crc <files...>   |   sum -c <list>\n");
        return 2;
    }
    if (!strcmp(argv[1], "-c")) {
        if (argc != 3) { printf("use: sum -c <list>\n"); return 2; }
        return check(argv[2]);
    }
    bool crc_only = !strcmp(argv[1], "-crc");
    int first = crc_only ? 2 : 1, status = 0;
    if (first >= argc) { printf("use: sum -crc <files...>\n"); return 2; }
    for (int i = first; i < argc; i++) {
        char sha[65];
        uint32_t crc;
        uint64_t size;
        int r = digest(argv[i], sha, &crc, &size);
        if (r) {
            printf("sum: %s: %s\n", argv[i], strerror(r));
            status = 1;
            continue;
        }
        if (crc_only) printf("%08x  %10lu  %s\n", crc, (unsigned long)size, argv[i]);
        else printf("%s  %s\n", sha, argv[i]);
    }
    return status;
}
