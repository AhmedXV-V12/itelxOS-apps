/* ui: the frame, keys and small helpers of the full-screen apps (ui.h). */
#include "ui.h"

static char *fb;
static size_t fb_len, fb_cap;
static bool raw;

void ui_size(int *cols, int *rows) {
    struct itx_ttyinfo ti;
    if (tty_info(&ti) == 0 && ti.cols >= 20 && ti.rows >= 10) {
        *cols = (int)ti.cols;
        *rows = (int)ti.rows;
    } else {
        *cols = 80;
        *rows = 25;
    }
}

void ui_begin(void) {
    flush();
    tty_mode(TTY_MODE_RAW);
    raw = true;
}

void ui_end(void) {
    flush();
    tty_mode(TTY_MODE_COOKED);
    raw = false;
}

int ui_key(void) {
    struct itx_key k;
    if (tty_key(&k) < 0) return -1;
    if (k.code == 3 && (k.mods & 2)) {
        /* raw keys bring Ctrl+C to the program instead of stopping it:
           stop here, as the terminal does with every other program */
        ui_end();
        printf("\n[stopped]\n");
        exit(130);
    }
    if (k.code == KEY_ESC) return 27;
    return (int)k.code;
}

static void grow(size_t need) {
    if (fb_len + need + 1 <= fb_cap) return;
    size_t cap = fb_cap ? fb_cap : 8192;
    while (cap < fb_len + need + 1) cap *= 2;
    char *n = realloc(fb, cap);
    if (!n) { dprintf(2, "out of memory\n"); exit(1); }
    fb = n;
    fb_cap = cap;
}

void ui_frame(void) {
    grow(16);
    memcpy(fb, "\x1b[2J", 4);
    fb_len = 4;
}

void ui_put(const char *s) {
    size_t n = strlen(s);
    grow(n);
    memcpy(fb + fb_len, s, n);
    fb_len += n;
}

void ui_printf(const char *fmt, ...) {
    char line[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    ui_put(line);
}

void ui_show(void) {
    flush();
    size_t done = 0;
    while (done < fb_len) {
        ssize_t n = write(1, fb + done, fb_len - done);
        if (n <= 0) break;
        done += (size_t)n;
    }
}

bool ui_ask(const char *prompt, char *buf, size_t len) {
    bool was = raw;
    if (was) ui_end();
    printf("%s", prompt);
    flush();
    ssize_t n = read(0, buf, len - 1);
    if (was) ui_begin();
    if (n <= 0) { buf[0] = 0; return false; }
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) n--;
    buf[n] = 0;
    return true;
}

static uint64_t rng_state = 0x9E3779B97F4A7C15ull;

void rng_seed(uint64_t seed) {
    rng_state = seed ? seed : 0x9E3779B97F4A7C15ull;
}

uint32_t rng_below(uint32_t n) {
    rng_state ^= rng_state >> 12;
    rng_state ^= rng_state << 25;
    rng_state ^= rng_state >> 27;
    uint64_t r = rng_state * 0x2545F4914F6CDD1Dull;
    return n ? (uint32_t)((r >> 32) % n) : 0;
}

const char *ui_data_dir(void) {
    struct itx_stat st;
    if (stat("/dex/data", &st)) mkdir("/dex/data");
    return "/dex/data";
}
