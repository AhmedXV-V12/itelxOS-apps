#!/usr/bin/env python3
"""The payload of recustomize: the files of itelx as installed.

    mkfactory.py FACTORY_DIR OUT VERSION

FACTORY_DIR is customizers/factory/itelx-1.4 (tools/get-factory.sh): its
rootfs/ goes to the system disk, esp/boot/vmlinuz and initrd.img to the
boot partition, DIRS.txt lists the folders (empty ones too).
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mkczpayload  # noqa: E402


def specs(factory):
    out = []
    root = os.path.join(factory, "rootfs")
    for line in open(os.path.join(factory, "DIRS.txt")):
        d = line.strip()
        if d and d != "rootfs":
            out.append("dir:" + d[len("rootfs"):])
    files = []
    for top, _, names in os.walk(root):
        for n in names:
            full = os.path.join(top, n)
            files.append(("/" + os.path.relpath(full, root).replace(os.sep, "/"), full))
    for dest, full in sorted(files):
        out.append("itfs:%s=%s" % (dest, full))
    for n in ("vmlinuz", "initrd.img"):
        out.append("boot:/boot/%s=%s" % (n, os.path.join(factory, "esp", "boot", n)))
    return out


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    n, size = mkczpayload.build(sys.argv[2], "recustomize", "recustomize", sys.argv[3], specs(sys.argv[1]))
    print("%s: %d entries, %d bytes" % (sys.argv[2], n, size))


if __name__ == "__main__":
    main()
