/* kit: SHA-256 (FIPS 180-4), HMAC-SHA-256 (RFC 2104) and PBKDF2 (RFC 8018). */
#include "kit.h"

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

static void block(struct k_sha256 *c, const uint8_t *p) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = c->h[0], b = c->h[1], cc = c->h[2], d = c->h[3], e = c->h[4], f = c->h[5], g = c->h[6], h = c->h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = h + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
        uint32_t t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & cc) ^ (b & cc));
        h = g; g = f; f = e; e = d + t1; d = cc; cc = b; b = a; a = t1 + t2;
    }
    c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d;
    c->h[4] += e; c->h[5] += f; c->h[6] += g; c->h[7] += h;
}

void k_sha256_init(struct k_sha256 *c) {
    static const uint32_t iv[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    memcpy(c->h, iv, sizeof iv);
    c->len = 0;
    c->n = 0;
}

void k_sha256_add(struct k_sha256 *c, const void *data, size_t len) {
    const uint8_t *p = data;
    c->len += len;
    if (c->n) {
        size_t take = 64 - c->n < len ? 64 - c->n : len;
        memcpy(c->buf + c->n, p, take);
        c->n += take;
        p += take;
        len -= take;
        if (c->n == 64) { block(c, c->buf); c->n = 0; }
    }
    while (len >= 64) { block(c, p); p += 64; len -= 64; }
    if (len) { memcpy(c->buf, p, len); c->n = len; }
}

void k_sha256_end(struct k_sha256 *c, uint8_t out[32]) {
    uint64_t bits = c->len * 8;
    uint8_t pad = 0x80;
    k_sha256_add(c, &pad, 1);
    uint8_t z = 0;
    while (c->n != 56) k_sha256_add(c, &z, 1);
    uint8_t lb[8];
    for (int i = 0; i < 8; i++) lb[i] = (uint8_t)(bits >> (56 - 8 * i));
    k_sha256_add(c, lb, 8);
    for (int i = 0; i < 8; i++) {
        out[4 * i] = (uint8_t)(c->h[i] >> 24);
        out[4 * i + 1] = (uint8_t)(c->h[i] >> 16);
        out[4 * i + 2] = (uint8_t)(c->h[i] >> 8);
        out[4 * i + 3] = (uint8_t)c->h[i];
    }
}

void k_sha256(const void *data, size_t len, uint8_t out[32]) {
    struct k_sha256 c;
    k_sha256_init(&c);
    k_sha256_add(&c, data, len);
    k_sha256_end(&c, out);
}

void k_hmac_sha256(const void *key, size_t klen, const void *msg, size_t mlen, uint8_t out[32]) {
    uint8_t k[64], ipad[64], opad[64], inner[32];
    memset(k, 0, sizeof k);
    if (klen > 64) k_sha256(key, klen, k);
    else memcpy(k, key, klen);
    for (int i = 0; i < 64; i++) { ipad[i] = k[i] ^ 0x36; opad[i] = k[i] ^ 0x5c; }
    struct k_sha256 c;
    k_sha256_init(&c);
    k_sha256_add(&c, ipad, 64);
    k_sha256_add(&c, msg, mlen);
    k_sha256_end(&c, inner);
    k_sha256_init(&c);
    k_sha256_add(&c, opad, 64);
    k_sha256_add(&c, inner, 32);
    k_sha256_end(&c, out);
}

/* PBKDF2-HMAC-SHA-256; the inner and outer pads of the key are hashed once */
void k_pbkdf2_sha256(const void *pass, size_t plen, const void *salt, size_t slen, uint32_t iter,
                     uint8_t *out, size_t olen) {
    uint8_t k[64], ipad[64], opad[64];
    memset(k, 0, sizeof k);
    if (plen > 64) k_sha256(pass, plen, k);
    else memcpy(k, pass, plen);
    for (int i = 0; i < 64; i++) { ipad[i] = k[i] ^ 0x36; opad[i] = k[i] ^ 0x5c; }
    struct k_sha256 ci, co;
    k_sha256_init(&ci);
    k_sha256_add(&ci, ipad, 64);
    k_sha256_init(&co);
    k_sha256_add(&co, opad, 64);
    for (uint32_t blk = 1; olen; blk++) {
        uint8_t u[32], t[32], cnt[4] = { (uint8_t)(blk >> 24), (uint8_t)(blk >> 16), (uint8_t)(blk >> 8), (uint8_t)blk };
        struct k_sha256 c = ci;
        k_sha256_add(&c, salt, slen);
        k_sha256_add(&c, cnt, 4);
        k_sha256_end(&c, u);
        c = co;
        k_sha256_add(&c, u, 32);
        k_sha256_end(&c, u);
        memcpy(t, u, 32);
        for (uint32_t i = 1; i < iter; i++) {
            c = ci;
            k_sha256_add(&c, u, 32);
            k_sha256_end(&c, u);
            c = co;
            k_sha256_add(&c, u, 32);
            k_sha256_end(&c, u);
            for (int j = 0; j < 32; j++) t[j] ^= u[j];
        }
        size_t n = olen < 32 ? olen : 32;
        memcpy(out, t, n);
        out += n;
        olen -= n;
    }
}

void k_hex(char *out, const uint8_t *b, size_t n) {
    static const char d[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) { out[2 * i] = d[b[i] >> 4]; out[2 * i + 1] = d[b[i] & 15]; }
    out[2 * n] = 0;
}

static inline uint64_t tsc(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return (uint64_t)hi << 32 | lo;
}

void k_random(void *out, size_t n) {
    static uint64_t counter;
    uint8_t *o = out;
    while (n) {
        struct { uint64_t t1, now, up, cnt; int pid; uint64_t t2; } seed;
        seed.t1 = tsc();
        seed.now = (uint64_t)unix_time();
        seed.up = (uint64_t)uptime_ms();
        seed.cnt = ++counter;
        seed.pid = getpid();
        /* the time stamp counter around a system call varies with interrupts */
        yield();
        seed.t2 = tsc();
        uint8_t h[32];
        k_sha256(&seed, sizeof seed, h);
        size_t k = n < 32 ? n : 32;
        memcpy(o, h, k);
        o += k;
        n -= k;
    }
}
