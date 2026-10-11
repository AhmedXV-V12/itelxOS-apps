/* XVR: the itelx executable format, version 1.
 *
 *   +-----------------------------+  offset 0
 *   | header (128 bytes)          |
 *   +-----------------------------+  128
 *   | segment table               |  seg_count x 32 bytes
 *   +-----------------------------+
 *   | segment data ...            |  each segment's bytes, at file_offset
 *   +-----------------------------+
 *   | signature block (optional)  |  reserved: sig_offset/sig_size
 *   +-----------------------------+  file_size
 *
 * A segment is loaded at vaddr with memsz bytes; the first filesz come from
 * the file, the rest are zero. Segments are page aligned, do not overlap and
 * are never both writable and executable. The header and the segment table
 * are covered by header_crc, everything after the table by payload_crc
 * (CRC-32, IEEE). Numbers are little endian.
 *
 * XVR files are made by tools/xvrlink, which takes a statically linked ELF
 * program built with the itelx userspace library and writes its loadable
 * segments in this format. The kernel never loads ELF files.
 */
#ifndef XVR_FORMAT_H
#define XVR_FORMAT_H

#include <stdint.h>

#define XVR_MAGIC0 'X'
#define XVR_MAGIC1 'V'
#define XVR_MAGIC2 'R'
#define XVR_MAGIC3 0x1A
#define XVR_VERSION        1
#define XVR_MACHINE_X86_64 0x8664
#define XVR_HEADER_SIZE    128
#define XVR_SEGMENT_SIZE   32
#define XVR_MAX_SEGMENTS   16
#define XVR_MAX_FILE       (64u << 20)
#define XVR_MAX_SEGMENT    (256ull << 20)
#define XVR_DEFAULT_STACK  (256u << 10)
#define XVR_MAX_STACK      (8u << 20)
#define XVR_DEFAULT_HEAP   (64ull << 20)
#define XVR_MAX_HEAP       (1ull << 30)
#define XVR_LOAD_MIN       0x400000ull          /* lowest segment address */
#define XVR_LOAD_TOP       0x0000400000000000ull /* segments end below this */

/* header flags */
#define XVR_F_CONSOLE   0x0001                   /* uses the terminal */

/* kind */
#define XVR_KIND_PROGRAM 0
#define XVR_KIND_UPDATE  1

/* segment flags */
#define XVR_SEG_R 1
#define XVR_SEG_W 2
#define XVR_SEG_X 4

struct xvr_header {
    uint8_t  magic[4];
    uint16_t version;
    uint16_t header_size;
    uint16_t machine;
    uint16_t flags;
    uint32_t seg_count;
    uint64_t entry;
    uint64_t stack_size;        /* 0: XVR_DEFAULT_STACK */
    uint64_t heap_max;          /* 0: XVR_DEFAULT_HEAP */
    uint64_t file_size;
    uint32_t header_crc;        /* over header (this field 0) and segment table */
    uint32_t payload_crc;       /* over all bytes after the segment table */
    uint32_t sig_offset;        /* 0: unsigned. Reserved for update signatures */
    uint32_t sig_size;
    uint32_t kind;              /* XVR_KIND_* */
    uint32_t min_os;            /* lowest itelx version, 0x0102 = 1.2 */
    char     name[40];          /* UTF-8, NUL padded */
    uint8_t  reserved[16];
};

struct xvr_segment {
    uint64_t vaddr;
    uint64_t memsz;
    uint32_t file_offset;
    uint32_t filesz;
    uint32_t flags;             /* XVR_SEG_* */
    uint32_t align;             /* 4096 */
};

#ifdef __cplusplus
static_assert(sizeof(struct xvr_header) == XVR_HEADER_SIZE, "xvr header size");
#else
_Static_assert(sizeof(struct xvr_header) == XVR_HEADER_SIZE, "xvr header size");
_Static_assert(sizeof(struct xvr_segment) == XVR_SEGMENT_SIZE, "xvr segment size");
#endif

#endif
