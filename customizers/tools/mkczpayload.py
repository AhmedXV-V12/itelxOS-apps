#!/usr/bin/env python3
"""Puts together the payload of a customizer app (customizers/common/czpay.h).

    mkczpayload.py OUT ID TITLE VERSION SPEC ...

SPEC is one of
    itfs:/path=SRC        a file for the system disk (ITFS)
    boot:/path=SRC        a file for the system's boot partition (the kernel)
    dir:/path             a directory to make on the system disk
    info:/name=SRC        a text file kept in the payload for the installer
                          itself (about.txt: what the customizer does)
    @FILE                 the specs listed in FILE, one per line (# comments)
Files are installed in the order given. VERSION is "1.0" style. In SRC,
{B} stands for the build folder (the environment variable CZ_BUILD).

Layout (little endian): a 64-byte header ("ITXCZ001", count, version,
id[16], title[32]), count entries of 128 bytes (path[104], targets, offset,
size, crc32, 8 reserved bytes), then the files, each at a 16-byte boundary.
"""
import os
import struct
import sys
import zlib

TARGETS = {"itfs": 1, "boot": 2, "dir": 4, "info": 8}
HEAD = "<8sII16s32s"
ENTRY = "<104sIIII8x"


def version_num(v):
    major, minor = v.split(".")
    return int(major) << 8 | int(minor)


def expand(specs):
    out = []
    for spec in specs:
        if spec.startswith("@"):
            for line in open(spec[1:], encoding="utf-8"):
                line = line.strip()
                if line and not line.startswith("#"):
                    out.append(line)
        else:
            out.append(spec)
    b = os.environ.get("CZ_BUILD", "build")
    return [s.replace("{B}", b) for s in out]


def build(out, ident, title, version, specs):
    items = []
    for spec in expand(specs):
        kind, rest = spec.split(":", 1)
        if kind not in TARGETS:
            sys.exit("unknown target in %r" % spec)
        if kind == "dir":
            dest, data = rest, b""
        else:
            dest, src = rest.split("=", 1)
            data = open(src, "rb").read()
        if not dest.startswith("/") or len(dest.encode()) >= 104:
            sys.exit("bad destination %r" % dest)
        items.append((dest, TARGETS[kind], data))
    if len(ident.encode()) >= 16 or len(title.encode()) >= 32:
        sys.exit("id or title too long")
    head = struct.pack(HEAD, b"ITXCZ001", len(items), version_num(version), ident.encode(), title.encode())
    off = len(head) + struct.calcsize(ENTRY) * len(items)
    table, blobs = b"", b""
    for dest, mask, data in items:
        blobs += b"\0" * ((-(off + len(blobs))) % 16)
        start = off + len(blobs)
        table += struct.pack(ENTRY, dest.encode(), mask, start, len(data), zlib.crc32(data) & 0xFFFFFFFF)
        blobs += data
    with open(out, "wb") as f:
        f.write(head + table + blobs)
    return len(items), len(head) + len(table) + len(blobs)


def read(path):
    """the entries of a payload file: (path, targets, data)"""
    blob = open(path, "rb").read()
    magic, count, version, ident, title = struct.unpack_from(HEAD, blob, 0)
    assert magic == b"ITXCZ001", "not a customizer payload"
    out = []
    pos = struct.calcsize(HEAD)
    for _ in range(count):
        dest, mask, off, size, crc = struct.unpack_from(ENTRY, blob, pos)
        pos += struct.calcsize(ENTRY)
        data = blob[off:off + size]
        assert zlib.crc32(data) & 0xFFFFFFFF == crc
        out.append((dest.rstrip(b"\0").decode(), mask, data))
    return dict(id=ident.rstrip(b"\0").decode(), title=title.rstrip(b"\0").decode(),
                version="%d.%d" % (version >> 8, version & 255), entries=out)


def main():
    if len(sys.argv) < 6:
        sys.exit(__doc__)
    n, size = build(sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5:])
    print("%s: %d entries, %d bytes" % (sys.argv[1], n, size))


if __name__ == "__main__":
    main()
