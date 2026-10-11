/* xvrlink: makes an XVR executable (xvr/xvr_format.h) from a statically
 * linked ELF program built with libitx and system/lib/xvr.ld.
 *
 *   xvrlink [--name NAME] [--stack BYTES] [--heap BYTES] [--update] in.elf out.xvr
 *
 * Only the loadable segments are carried over, with their protection; the
 * ELF headers, sections and symbols are not part of the XVR file. The
 * result is checked the same way the kernel checks it before it is written.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../xvr/xvr_format.h"

typedef struct {
    uint8_t  e_ident[16];
    uint16_t e_type, e_machine;
    uint32_t e_version;
    uint64_t e_entry, e_phoff, e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
} Ehdr;
typedef struct {
    uint32_t p_type, p_flags;
    uint64_t p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align;
} Phdr;

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

static void die(const char *m) { fprintf(stderr, "xvrlink: %s\n", m); exit(1); }

int main(int argc, char **argv) {
    const char *name = NULL, *in = NULL, *out = NULL;
    uint64_t stack = 0, heap = 0;
    uint32_t kind = XVR_KIND_PROGRAM;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--name") && i + 1 < argc) name = argv[++i];
        else if (!strcmp(argv[i], "--stack") && i + 1 < argc) stack = strtoull(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--heap") && i + 1 < argc) heap = strtoull(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--update")) kind = XVR_KIND_UPDATE;
        else if (!in) in = argv[i];
        else if (!out) out = argv[i];
        else die("too many arguments");
    }
    if (!in || !out) die("usage: xvrlink [--name N] [--stack B] [--heap B] [--update] in.elf out.xvr");
    FILE *f = fopen(in, "rb");
    if (!f) { perror(in); return 1; }
    fseek(f, 0, SEEK_END);
    long fsz = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *elf = malloc(fsz);
    if (fread(elf, 1, fsz, f) != (size_t)fsz) die("cannot read the input");
    fclose(f);
    Ehdr *eh = (Ehdr *)elf;
    if (fsz < (long)sizeof *eh || memcmp(eh->e_ident, "\x7f" "ELF", 4) || eh->e_ident[4] != 2 ||
        eh->e_machine != 0x3E || eh->e_type != 2)
        die("the input is not an x86_64 ELF executable");
    if (eh->e_phentsize != sizeof(Phdr) || eh->e_phoff + (uint64_t)eh->e_phnum * sizeof(Phdr) > (uint64_t)fsz)
        die("bad program headers");
    Phdr *ph = (Phdr *)(elf + eh->e_phoff);
    struct xvr_segment segs[XVR_MAX_SEGMENTS];
    const uint8_t *data[XVR_MAX_SEGMENTS];
    uint32_t nseg = 0;
    for (int i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type != 1 || !ph[i].p_memsz) continue;
        if (nseg == XVR_MAX_SEGMENTS) die("too many segments");
        if (ph[i].p_vaddr % 4096) die("a segment is not page aligned (link with system/lib/xvr.ld)");
        if (ph[i].p_offset + ph[i].p_filesz > (uint64_t)fsz) die("a segment runs past the end of the file");
        struct xvr_segment *s = &segs[nseg];
        memset(s, 0, sizeof *s);
        s->vaddr = ph[i].p_vaddr;
        s->memsz = ph[i].p_memsz;
        s->filesz = (uint32_t)ph[i].p_filesz;
        s->flags = ((ph[i].p_flags & 4) ? XVR_SEG_R : 0) | ((ph[i].p_flags & 2) ? XVR_SEG_W : 0) |
                   ((ph[i].p_flags & 1) ? XVR_SEG_X : 0);
        s->flags |= XVR_SEG_R;
        if ((s->flags & XVR_SEG_W) && (s->flags & XVR_SEG_X)) die("a segment is writable and executable");
        s->align = 4096;
        data[nseg] = elf + ph[i].p_offset;
        nseg++;
    }
    if (!nseg) die("no loadable segment");
    uint64_t table_end = XVR_HEADER_SIZE + nseg * XVR_SEGMENT_SIZE;
    uint64_t off = table_end, total = table_end;
    for (uint32_t i = 0; i < nseg; i++) { segs[i].file_offset = segs[i].filesz ? (uint32_t)off : 0; off += segs[i].filesz; }
    total = off;
    if (total > XVR_MAX_FILE) die("the program is larger than 64 MiB");
    uint8_t *x = calloc(1, total);
    struct xvr_header *h = (struct xvr_header *)x;
    h->magic[0] = XVR_MAGIC0; h->magic[1] = XVR_MAGIC1; h->magic[2] = XVR_MAGIC2; h->magic[3] = XVR_MAGIC3;
    h->version = XVR_VERSION;
    h->header_size = XVR_HEADER_SIZE;
    h->machine = XVR_MACHINE_X86_64;
    h->flags = XVR_F_CONSOLE;
    h->seg_count = nseg;
    h->entry = eh->e_entry;
    h->stack_size = stack;
    h->heap_max = heap;
    h->file_size = total;
    h->kind = kind;
    h->min_os = 0x0102;
    const char *base = name ? name : strrchr(out, '/') ? strrchr(out, '/') + 1 : out;
    strncpy(h->name, base, sizeof h->name - 1);
    memcpy(x + XVR_HEADER_SIZE, segs, nseg * XVR_SEGMENT_SIZE);
    for (uint32_t i = 0; i < nseg; i++) if (segs[i].filesz) memcpy(x + segs[i].file_offset, data[i], segs[i].filesz);
    h->payload_crc = crc32_update(0, x + table_end, total - table_end);
    h->header_crc = 0;
    h->header_crc = crc32_update(0, x, table_end);
    FILE *o = fopen(out, "wb");
    if (!o || fwrite(x, 1, total, o) != total) { perror(out); return 1; }
    fclose(o);
    printf("xvrlink: %s: %u segment(s), entry 0x%llx, %llu bytes\n", out, nseg, (unsigned long long)eh->e_entry,
           (unsigned long long)total);
    return 0;
}
