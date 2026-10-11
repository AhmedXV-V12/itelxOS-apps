/* xvc: runs an XVC program.
 *   xvc <program.xvc> [arguments...]
 * The terminal starts it for "run program.xvc". Exit status: the program's
 * exit(), 0 when it ends normally, 1 on a runtime error, 2 when it does not
 * compile. */
#include "xvc.h"

static vm machine;

int main(int argc, char **argv) {
    if (argc < 2) {
        dprintf(2, "usage: run <program.xvc> [arguments]\n");
        return 2;
    }
    const char *file = argv[1];
    size_t len;
    char *src = read_file(file, &len, 8u << 20);
    if (!src) {
        struct itx_stat st;
        int r = stat(file, &st);
        dprintf(2, "xvc: %s: %s\n", file, r ? strerror(r) : st.type == VT_DIR ? "is a directory" : "cannot read (larger than 8 MiB?)");
        return 2;
    }
    for (size_t i = 0; i < len; i++) {
        if (src[i] == 0) { dprintf(2, "xvc: %s: this is not a text file\n", file); return 2; }
    }
    vm *m = &machine;
    vm_init(m, file);
    vm_define_natives(m, argc - 1, argv + 1);
    ofunc *fn = xvc_compile(m, src, len, file);
    free(src);
    if (!fn) return 2;
    int code = vm_run(m, fn);
    flush();
    return code;
}
