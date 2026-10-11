/* xvcapp: the XVC interpreter with room for one program inside it.
 *
 * An XVR executable made from an XVC program is this program with the
 * XVC source in its last segment, the payload (xvcapp.h, xvcapp.ld). It
 * compiles that source and runs it exactly as "run program.xvc" would:
 * the same language, the same built-in functions, args[0] is the path the
 * program was started with.
 *
 * With an empty payload this program is xvc2xvr, the converter:
 *
 *   xvc2xvr <program.xvc> [output.xvr]
 *
 * It compiles the program first (a program with an error is not
 * converted), then writes a copy of its own XVR file with the program in
 * the payload, and asks the kernel to check the result (SYS_XVRCHECK).
 * tools/xvc2xvr.py does the same on a PC.
 */
#include "../system/xvc/xvc.h"
#include "../xvr/xvr_format.h"
#include "xvcapp.h"

/* the payload as linked: empty. xvc2xvr fills it in the copies it makes,
 * so it is read through the linker's symbol, never through this object */
__attribute__((section(".xvcapp"), used, aligned(16)))
static const struct xvcapp_payload empty_payload = { XVCAPP_MAGIC, 0, 0, "" };
extern const char __xvcapp_start[];

static vm machine;
static char program_name[sizeof empty_payload.name + 1];

/* ------------------------------------------------------------ helpers */

static uint32_t crc_tab[256];

static uint32_t crc32_update(uint32_t crc, const void *data, size_t len) {
    if (!crc_tab[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? (c >> 1) ^ 0xEDB88320u : c >> 1;
            crc_tab[i] = c;
        }
    const uint8_t *p = data;
    crc = ~crc;
    while (len--) crc = crc_tab[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

static const char *base_name(const char *path) {
    const char *s = strrchr(path, '/');
    return s ? s + 1 : path;
}

/* copies at most n-1 bytes of s, never cutting a UTF-8 character */
static void copy_utf8(char *d, size_t n, const char *s, size_t len) {
    if (len > n - 1) {
        len = n - 1;
        while (len && ((uint8_t)s[len] & 0xC0) == 0x80) len--;
    }
    memset(d, 0, n);
    memcpy(d, s, len);
}

/* ------------------------------------------------------- run the program */

static int run_payload(const struct xvcapp_payload *p, int argc, char **argv) {
    if (p->size > XVCAPP_MAX) {
        dprintf(2, "xvcapp: the program inside %s is damaged\n", argv[0]);
        return 2;
    }
    memcpy(program_name, p->name, sizeof p->name);
    program_name[sizeof p->name] = 0;
    if (!program_name[0]) strlcpy(program_name, "program.xvc", sizeof program_name);
    vm *m = &machine;
    vm_init(m, program_name);
    vm_define_natives(m, argc, argv);
    ofunc *fn = xvc_compile(m, (const char *)(p + 1), p->size, program_name);
    if (!fn) return 2;
    int code = vm_run(m, fn);
    flush();
    return code;
}

/* ----------------------------------------------------------- convert */

static int usage(void) {
    dprintf(2, "usage: xvc2xvr <program.xvc> [output.xvr]\n"
               "makes an XVR executable from an XVC program: it runs like \"run program.xvc\",\n"
               "and can be installed and run by its name like any XVR program\n");
    return 2;
}

/* reads this program's own XVR file and checks that it is the converter:
 * a valid XVR whose last segment is an empty payload at the end of the file */
static char *read_self(const char *argv0, size_t *len, const struct xvr_segment **last) {
    const char *tries[] = { argv0, "/dex/xvc2xvr.xvr" };
    for (int t = 0; t < 2; t++) {
        if (!tries[t] || !ends_with(tries[t], ".xvr")) continue;
        char *img = read_file(tries[t], len, XVR_MAX_FILE);
        if (!img) continue;
        struct xvr_header *h = (struct xvr_header *)img;
        if (*len < XVR_HEADER_SIZE || memcmp(h->magic, "XVR\x1a", 4) || h->version != XVR_VERSION ||
            h->file_size != *len || h->seg_count == 0 || h->seg_count > XVR_MAX_SEGMENTS) { free(img); continue; }
        size_t table_end = XVR_HEADER_SIZE + (size_t)h->seg_count * XVR_SEGMENT_SIZE;
        if (table_end > *len) { free(img); continue; }
        const struct xvr_segment *s = (const struct xvr_segment *)(img + XVR_HEADER_SIZE) + h->seg_count - 1;
        const struct xvcapp_payload *p = (const struct xvcapp_payload *)(img + s->file_offset);
        if (s->file_offset < table_end || (size_t)s->file_offset + s->filesz != *len || s->filesz < XVCAPP_HEADER ||
            memcmp(p->magic, XVCAPP_MAGIC, 8) || p->size != 0) { free(img); continue; }
        *last = s;
        return img;
    }
    return NULL;
}

static int convert(int argc, char **argv) {
    if (argc < 2 || argc > 3 || !strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) return usage();
    const char *in = argv[1];
    if (!ends_with(in, ".xvc")) { dprintf(2, "xvc2xvr: %s: not an .xvc program\n", in); return 2; }
    char out[ITX_PATH_MAX];
    if (argc > 2) strlcpy(out, argv[2], sizeof out);
    else {
        strlcpy(out, in, sizeof out);
        strcpy(out + strlen(out) - 4, ".xvr");
    }
    if (!ends_with(out, ".xvr") || strlen(base_name(out)) <= 4) {
        dprintf(2, "xvc2xvr: %s: the output must be a name ending in .xvr\n", out);
        return 2;
    }

    /* the program: text, and it compiles */
    size_t len;
    char *src = read_file(in, &len, XVCAPP_MAX);
    if (!src) {
        struct itx_stat st;
        int r = stat(in, &st);
        dprintf(2, "xvc2xvr: %s: %s\n", in, r ? strerror(r) : "cannot read it (larger than 8 MiB?)");
        return 2;
    }
    for (size_t i = 0; i < len; i++)
        if (!src[i]) { dprintf(2, "xvc2xvr: %s: this is not a text file\n", in); return 2; }
    char *text = src;
    if (len >= 3 && !memcmp(text, "\xef\xbb\xbf", 3)) {     /* "UTF-8 with BOM": the mark is not XVC */
        text += 3;
        len -= 3;
    }
    vm_init(&machine, in);
    if (!xvc_compile(&machine, text, len, in)) {
        dprintf(2, "xvc2xvr: %s was not converted: correct the error above first\n", in);
        return 2;
    }

    /* a copy of this program with the source in the payload */
    size_t ilen;
    const struct xvr_segment *last;
    char *img = read_self(argv[0], &ilen, &last);
    if (!img) {
        dprintf(2, "xvc2xvr: cannot read the converter's own file (%s); install it as /dex/xvc2xvr.xvr\n", argv[0]);
        return 1;
    }
    size_t off = last->file_offset;
    size_t total = off + XVCAPP_HEADER + len;
    char *x = calloc(1, total);
    if (!x) { dprintf(2, "xvc2xvr: out of memory\n"); return 1; }
    memcpy(x, img, off);
    struct xvr_header *h = (struct xvr_header *)x;
    struct xvr_segment *s = (struct xvr_segment *)(x + XVR_HEADER_SIZE) + h->seg_count - 1;
    struct xvcapp_payload *p = (struct xvcapp_payload *)(x + off);
    memcpy(p->magic, XVCAPP_MAGIC, 8);
    p->size = (uint32_t)len;
    p->flags = 0;
    copy_utf8(p->name, sizeof p->name, base_name(in), strlen(base_name(in)));
    memcpy(x + off + XVCAPP_HEADER, text, len);
    s->filesz = (uint32_t)(XVCAPP_HEADER + len);
    s->memsz = XVCAPP_HEADER + len + 1;
    h->file_size = total;
    const char *bn = base_name(out);
    copy_utf8(h->name, sizeof h->name, bn, strlen(bn) - 4);
    size_t table_end = XVR_HEADER_SIZE + (size_t)h->seg_count * XVR_SEGMENT_SIZE;
    h->payload_crc = crc32_update(0, x + table_end, total - table_end);
    h->header_crc = 0;
    h->header_crc = crc32_update(0, x, table_end);
    free(img);
    free(src);

    int r = write_file(out, x, total, false);
    free(x);
    if (r) { dprintf(2, "xvc2xvr: %s: %s\n", out, strerror(r)); return 1; }
    char why[96] = "";
    r = xvr_check(out, why, sizeof why);
    if (r && r != -ENOSYS) {
        dprintf(2, "xvc2xvr: %s was written but the system refuses it: %s\n", out, why[0] ? why : strerror(r));
        remove(out);
        return 1;
    }
    printf("xvc2xvr: made %s (%lu bytes) from %s\n", out, (unsigned long)total, in);
    if (!strncmp(out, "/dex/", 5) && !strchr(out + 5, '/')) {
        char name[ITX_NAME_MAX + 1];
        strlcpy(name, out + 5, sizeof name);
        name[strlen(name) - 4] = 0;
        printf("run it by its name: %s\n", name);
    } else {
        printf("run it:      %s\ninstall it:  install %s\n", out, out);
    }
    return 0;
}

int main(int argc, char **argv) {
    const struct xvcapp_payload *p = (const struct xvcapp_payload *)__xvcapp_start;
    if (!memcmp(p->magic, XVCAPP_MAGIC, 8) && p->size) return run_payload(p, argc, argv);
    return convert(argc, argv);
}
