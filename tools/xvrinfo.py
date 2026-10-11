#!/usr/bin/env python3
"""xvrinfo.py: shows what is inside an itelxOS XVR executable and checks it
the way the itelx kernel does before it runs a program.

    python3 tools/xvrinfo.py app.xvr [more.xvr ...]
    python3 tools/xvrinfo.py --source app.xvr     prints the XVC program inside
                                                  an XVR made by xvc2xvr

Exit status 0 when every file is valid, 1 otherwise.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from xvc2xvr import XVR_KINDS, XvrError, parse, payload_of  # noqa: E402


def flags_text(f):
    return "".join(c if f & b else "-" for c, b in (("R", 1), ("W", 2), ("X", 4)))


def describe(path, data):
    h, segs = parse(data)
    name = h["name"].rstrip(b"\0").decode("utf-8", "replace")
    print("%s: valid XVR executable" % path)
    print("  name      %s" % (name or "-"))
    print("  kind      %s" % XVR_KINDS[h["kind"]])
    print("  size      %d bytes" % h["file_size"])
    print("  entry     0x%x" % h["entry"])
    print("  needs     itelx %d.%d or newer" % (h["min_os"] >> 8, h["min_os"] & 0xFF))
    print("  stack     %s" % ("%d KiB" % (h["stack_size"] >> 10) if h["stack_size"] else "default (256 KiB)"))
    print("  heap      %s" % ("%d MiB" % (h["heap_max"] >> 20) if h["heap_max"] else "default (64 MiB)"))
    print("  signature %s" % ("%d bytes (not verified by itelx 1.3)" % h["sig_size"] if h["sig_size"] else "none"))
    print("  segments")
    for s in segs:
        print("    0x%09x  %8d bytes in memory, %8d in the file  %s" % (
            s["vaddr"], s["memsz"], s["filesz"], flags_text(s["flags"])))
    p = payload_of(data, segs)
    if p is not None:
        if p[0] is None:
            print("  XVC       this is xvc2xvr, the converter (no program inside)")
        else:
            lines = p[1].count(b"\n") + (0 if p[1].endswith(b"\n") else 1)
            print("  XVC       made from %s: %d bytes, %d line(s) of XVC (see --source)" % (p[0], len(p[1]), lines))


def main(argv=None):
    ap = argparse.ArgumentParser(description="Show and check itelxOS XVR executables.")
    ap.add_argument("files", nargs="+")
    ap.add_argument("--source", action="store_true", help="print the XVC program inside (xvc2xvr files)")
    a = ap.parse_args(argv)
    bad = 0
    for path in a.files:
        try:
            with open(path, "rb") as f:
                data = f.read()
            if a.source:
                h, segs = parse(data)
                p = payload_of(data, segs)
                if not p or p[0] is None:
                    raise XvrError("no XVC program inside (not made by xvc2xvr)")
                sys.stdout.write(p[1].decode("utf-8", "replace"))
            else:
                describe(path, data)
        except OSError as e:
            print("%s: %s" % (path, e.strerror or e), file=sys.stderr)
            bad += 1
        except XvrError as e:
            print("%s: itelx would refuse it: %s" % (path, e), file=sys.stderr)
            bad += 1
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
