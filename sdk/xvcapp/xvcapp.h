/* The payload of an XVR executable made from an XVC program (xvc2xvr).
 *
 * It is the last segment of the XVR file (read only) and the last bytes of
 * the file:
 *
 *   struct xvcapp_payload    64 bytes
 *   the program's source     size bytes (UTF-8 text, no NUL byte)
 *
 * In memory the segment is one byte longer than in the file (memsz =
 * 64 + size + 1), so the source is followed by a zero. size 0 means there
 * is no program: the file is xvc2xvr itself. Numbers are little endian.
 * tools/xvc2xvr.py writes the same layout.
 */
#ifndef XVCAPP_H
#define XVCAPP_H

#include <stdint.h>

#define XVCAPP_MAGIC   "XVCAPP1"          /* 7 characters and a zero */
#define XVCAPP_HEADER  64
#define XVCAPP_MAX     (8u << 20)         /* the largest program, as for "run" */

struct xvcapp_payload {
    char     magic[8];
    uint32_t size;                        /* bytes of source after this header */
    uint32_t flags;                       /* 0 */
    char     name[48];                    /* the source file's name, for error messages */
};

_Static_assert(sizeof(struct xvcapp_payload) == XVCAPP_HEADER, "xvcapp payload header size");

#endif
