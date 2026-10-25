#!/usr/bin/env python3
"""xvc2xvr.py: makes an itelxOS XVR executable from an XVC program, on a PC.

    python3 tools/xvc2xvr.py program.xvc [output.xvr] [--stub xvc2xvr.xvr]

The result is the XVC interpreter of itelxOS with the program inside it
(the converter usb/dex/xvc2xvr.xvr is that interpreter with no program).
On itelx it runs exactly like "run program.xvc", and it can be installed
and started by its name like any other XVR program. itelx itself has the
same converter: xvc2xvr program.xvc

Works with Python 3.6 or newer on Windows, Linux and macOS; nothing to
install. This tool does not run the XVC compiler: test the program on
itelx with "run program.xvc" first (the converter on itelx refuses a
program that does not compile; an XVR made here reports the error when it
starts).
"""
import argparse
import os
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_STUB = os.path.join(HERE, "..", "usb", "dex", "xvc2xvr.xvr")

# xvr/xvr_format.h
XVR_MAGIC = b"XVR\x1a"
XVR_VERSION = 1
XVR_MACHINE_X86_64 = 0x8664
XVR_HEADER_SIZE = 128
XVR_SEGMENT_SIZE = 32
XVR_MAX_SEGMENTS = 16
XVR_MAX_FILE = 64 << 20
XVR_MAX_SEGMENT = 256 << 20
XVR_MAX_STACK = 8 << 20
XVR_MAX_HEAP = 1 << 30
XVR_LOAD_MIN = 0x400000
XVR_LOAD_TOP = 0x0000400000000000
XVR_KINDS = {0: "program", 1: "update"}
PAGE = 4096
SUPPORTED_OS = 0x0200          # itelx 2.0

# struct xvr_header: magic, version, header_size, machine, flags, seg_count,
# entry, stack_size, heap_max, file_size, header_crc, payload_crc,
# sig_offset, sig_size, kind, min_os, name[40], reserved[16]
HEADER = struct.Struct("<4sHHHHIQQQQIIIIII40s16s")
SEGMENT = struct.Struct("<QQIIII")       # vaddr, memsz, file_offset, filesz, flags, align

# sdk/xvcapp/xvcapp.h
XVCAPP_MAGIC = b"XVCAPP1\0"
XVCAPP = struct.Struct("<8sII48s")       # magic, size, flags, name
XVCAPP_MAX = 8 << 20

assert HEADER.size == XVR_HEADER_SIZE and SEGMENT.size == XVR_SEGMENT_SIZE and XVCAPP.size == 64

HEADER_FIELDS = ("magic", "version", "header_size", "machine", "flags", "seg_count", "entry", "stack_size",
                 "heap_max", "file_size", "header_crc", "payload_crc", "sig_offset", "sig_size", "kind",
                 "min_os", "name", "reserved")


class XvrError(Exception):
    pass


def crc32(data, crc=0):
    return zlib.crc32(data, crc) & 0xFFFFFFFF


def parse(data):
    """Checks an XVR file the way the itelx kernel does (kernel/proc/xvr.c)
    and returns (header dict, list of segment dicts). Raises XvrError with
    the kernel's reason when it would refuse the file."""
    size = len(data)
    if size < XVR_HEADER_SIZE:
        raise XvrError("file too small for an XVR header")
    if size > XVR_MAX_FILE:
        raise XvrError("file larger than 64 MiB")
    h = dict(zip(HEADER_FIELDS, HEADER.unpack_from(data, 0)))
    if h["magic"] != XVR_MAGIC:
        raise XvrError("not an XVR executable (bad magic)")
    if h["version"] != XVR_VERSION:
        raise XvrError("unsupported XVR version")
    if h["header_size"] != XVR_HEADER_SIZE:
        raise XvrError("bad header size")
    if h["machine"] != XVR_MACHINE_X86_64:
        raise XvrError("built for another machine")
    if h["file_size"] != size:
        raise XvrError("file size does not match the header (truncated?)")
    if h["seg_count"] == 0 or h["seg_count"] > XVR_MAX_SEGMENTS:
        raise XvrError("bad segment count")
    table_end = XVR_HEADER_SIZE + h["seg_count"] * XVR_SEGMENT_SIZE
    if table_end > size:
        raise XvrError("segment table runs past the end of the file")
    if h["kind"] not in XVR_KINDS:
        raise XvrError("unknown program kind")
    if h["stack_size"] > XVR_MAX_STACK:
        raise XvrError("stack size too large")
    if h["heap_max"] > XVR_MAX_HEAP:
        raise XvrError("heap limit too large")
    if h["min_os"] > SUPPORTED_OS:
        raise XvrError("needs a newer itelx")
    if h["sig_offset"] or h["sig_size"]:
        if h["sig_offset"] < table_end or h["sig_offset"] + h["sig_size"] > size:
            raise XvrError("signature block outside the file")
    hdr = bytearray(data[:XVR_HEADER_SIZE])
    hdr[48:52] = b"\0\0\0\0"                 # header_crc
    if crc32(data[XVR_HEADER_SIZE:table_end], crc32(bytes(hdr))) != h["header_crc"]:
        raise XvrError("header checksum mismatch (damaged file)")
    if crc32(data[table_end:]) != h["payload_crc"]:
        raise XvrError("payload checksum mismatch (damaged file)")
    segs = []
    entry_ok = False
    prev_end = 0
    for i in range(h["seg_count"]):
        vaddr, memsz, off, filesz, flags, align = SEGMENT.unpack_from(data, XVR_HEADER_SIZE + i * XVR_SEGMENT_SIZE)
        if not flags & 1 or flags & ~7:
            raise XvrError("segment flags invalid")
        if flags & 2 and flags & 4:
            raise XvrError("a segment is both writable and executable")
        if vaddr % PAGE:
            raise XvrError("segment not page aligned")
        if not memsz or memsz > XVR_MAX_SEGMENT:
            raise XvrError("segment size invalid")
        if filesz > memsz:
            raise XvrError("segment file size larger than its memory size")
        if vaddr < XVR_LOAD_MIN or vaddr > XVR_LOAD_TOP - memsz:
            raise XvrError("segment outside the user program area")
        if filesz and (off < table_end or off + filesz > size):
            raise XvrError("segment data outside the file")
        if vaddr < prev_end:
            raise XvrError("segments overlap or are out of order")
        prev_end = (vaddr + memsz + PAGE - 1) // PAGE * PAGE
        if flags & 4 and vaddr <= h["entry"] < vaddr + memsz:
            entry_ok = True
        segs.append(dict(vaddr=vaddr, memsz=memsz, file_offset=off, filesz=filesz, flags=flags, align=align))
    if not entry_ok:
        raise XvrError("entry point is not in an executable segment")
    return h, segs


def payload_of(data, segs):
    """The XVC program inside an XVR (made by xvc2xvr): (name, source bytes),
    (None, b"") for the converter itself, None when it is no such file."""
    last = segs[-1]
    off, filesz = last["file_offset"], last["filesz"]
    if off + filesz != len(data) or filesz < XVCAPP.size:
        return None
    magic, size, _flags, name = XVCAPP.unpack_from(data, off)
    if magic != XVCAPP_MAGIC or XVCAPP.size + size != filesz:
        return None
    if size == 0:
        return (None, b"")
    return (name.rstrip(b"\0").decode("utf-8", "replace"), data[off + XVCAPP.size:off + filesz])


def utf8_cut(raw, n):
    """raw bytes cut to at most n bytes, never inside a UTF-8 character"""
    if len(raw) <= n:
        return raw
    while n and (raw[n] & 0xC0) == 0x80:
        n -= 1
    return raw[:n]


def convert(stub, source, source_name, program_name):
    """The XVR bytes for an XVC program: a copy of the converter (stub) with
    the source in its payload segment."""
    h, segs = parse(stub)
    found = payload_of(stub, segs)
    if found is None or found[0] is not None:
        raise XvrError("the stub is not xvc2xvr.xvr (no empty XVC payload at its end)")
    if source.startswith(b"\xef\xbb\xbf"):
        source = source[3:]              # "UTF-8 with BOM" (Windows): XVC would read the mark as a name
    if b"\0" in source:
        raise XvrError("this is not a text file (it has zero bytes)")
    if len(source) > XVCAPP_MAX:
        raise XvrError("the program is larger than 8 MiB")
    try:
        source.decode("utf-8")
    except UnicodeDecodeError as e:
        raise XvrError("the program is not UTF-8 text (%s)" % e)
    last_i = len(segs) - 1
    off = segs[last_i]["file_offset"]
    out = bytearray(stub[:off])
    out += XVCAPP.pack(XVCAPP_MAGIC, len(source), 0, utf8_cut(source_name.encode("utf-8"), 47))
    out += source
    total = len(out)
    seg_at = XVR_HEADER_SIZE + last_i * XVR_SEGMENT_SIZE
    s = segs[last_i]
    SEGMENT.pack_into(out, seg_at, s["vaddr"], XVCAPP.size + len(source) + 1, off, XVCAPP.size + len(source),
                      s["flags"], s["align"])
    struct.pack_into("<Q", out, 40, total)                                  # file_size
    out[72:112] = utf8_cut(program_name.encode("utf-8"), 39).ljust(40, b"\0")  # name
    table_end = XVR_HEADER_SIZE + h["seg_count"] * XVR_SEGMENT_SIZE
    struct.pack_into("<I", out, 52, crc32(bytes(out[table_end:])))          # payload_crc
    struct.pack_into("<I", out, 48, 0)
    struct.pack_into("<I", out, 48, crc32(bytes(out[:table_end])))          # header_crc
    parse(bytes(out))                    # the kernel's checks, once more
    return bytes(out)


def main(argv=None):
    ap = argparse.ArgumentParser(description="Make an itelxOS XVR executable from an XVC program.")
    ap.add_argument("program", help="the XVC program (.xvc)")
    ap.add_argument("output", nargs="?", help="the XVR file to write (default: the program's name with .xvr)")
    ap.add_argument("--stub", default=DEFAULT_STUB, help="xvc2xvr.xvr to build from (default: usb/dex/xvc2xvr.xvr)")
    a = ap.parse_args(argv)
    if not a.program.endswith(".xvc"):
        ap.error("%s: not an .xvc program" % a.program)
    out = a.output or a.program[:-4] + ".xvr"
    if not out.endswith(".xvr") or len(os.path.basename(out)) <= 4:
        ap.error("%s: the output must be a name ending in .xvr" % out)
    try:
        with open(a.stub, "rb") as f:
            stub = f.read()
        with open(a.program, "rb") as f:
            source = f.read()
        data = convert(stub, source, os.path.basename(a.program), os.path.basename(out)[:-4])
    except (OSError, XvrError) as e:
        print("xvc2xvr: %s" % e, file=sys.stderr)
        return 1
    with open(out, "wb") as f:
        f.write(data)
    print("xvc2xvr: made %s (%d bytes) from %s" % (out, len(data), a.program))
    print("copy it to a FAT32 USB stick; on itelx run it with /media/<STICK>/%s or install it" % os.path.basename(out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
