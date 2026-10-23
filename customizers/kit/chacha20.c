/* kit: the ChaCha20 stream cipher (RFC 8439), for File saver's locked files. */
#include "kit.h"

#define ROTL(a, b) (((a) << (b)) | ((a) >> (32 - (b))))
#define QR(a, b, c, d) (a += b, d ^= a, d = ROTL(d, 16), c += d, b ^= c, b = ROTL(b, 12), \
                        a += b, d ^= a, d = ROTL(d, 8), c += d, b ^= c, b = ROTL(b, 7))

static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

void k_chacha20_init(struct k_chacha20 *c, const uint8_t key[32], const uint8_t nonce[12], uint32_t counter) {
    static const uint8_t sigma[16] = "expand 32-byte k";
    for (int i = 0; i < 4; i++) c->s[i] = le32(sigma + 4 * i);
    for (int i = 0; i < 8; i++) c->s[4 + i] = le32(key + 4 * i);
    c->s[12] = counter;
    for (int i = 0; i < 3; i++) c->s[13 + i] = le32(nonce + 4 * i);
    c->used = 64;
}

static void block(struct k_chacha20 *c) {
    uint32_t x[16];
    memcpy(x, c->s, sizeof x);
    for (int i = 0; i < 10; i++) {
        QR(x[0], x[4], x[8], x[12]); QR(x[1], x[5], x[9], x[13]); QR(x[2], x[6], x[10], x[14]); QR(x[3], x[7], x[11], x[15]);
        QR(x[0], x[5], x[10], x[15]); QR(x[1], x[6], x[11], x[12]); QR(x[2], x[7], x[8], x[13]); QR(x[3], x[4], x[9], x[14]);
    }
    for (int i = 0; i < 16; i++) {
        uint32_t v = x[i] + c->s[i];
        c->ks[4 * i] = (uint8_t)v;
        c->ks[4 * i + 1] = (uint8_t)(v >> 8);
        c->ks[4 * i + 2] = (uint8_t)(v >> 16);
        c->ks[4 * i + 3] = (uint8_t)(v >> 24);
    }
    c->s[12]++;
    c->used = 0;
}

void k_chacha20_xor(struct k_chacha20 *c, void *data, size_t len) {
    uint8_t *p = data;
    for (size_t i = 0; i < len; i++) {
        if (c->used == 64) block(c);
        p[i] ^= c->ks[c->used++];
    }
}
