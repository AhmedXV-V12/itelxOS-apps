#!/usr/bin/env python3
"""xvrminos.py: sets the lowest itelx version an XVR executable runs on
(min_os in its header) and its header checksum. The itelx kernel refuses
to start a program that needs a newer itelx.

    python3 tools/xvrminos.py app.xvr 2.0

xvrlink always writes 1.2; the system tools (formater, recovery_tester)
need the recovery screen of itelx 2.0.
"""
import struct
import sys
import zlib

XVR_HEADER_SIZE = 128
XVR_SEGMENT_SIZE = 32


def main(argv):
    if len(argv) != 3:
        print("usage: xvrminos.py app.xvr MAJOR.MINOR", file=sys.stderr)
        return 2
    major, minor = (int(x) for x in argv[2].split("."))
    data = bytearray(open(argv[1], "rb").read())
    if data[:4] != b"XVR\x1a":
        print("%s: not an XVR executable" % argv[1], file=sys.stderr)
        return 1
    seg_count = struct.unpack_from("<I", data, 12)[0]
    table_end = XVR_HEADER_SIZE + seg_count * XVR_SEGMENT_SIZE
    struct.pack_into("<I", data, 68, (major << 8) | minor)      # min_os
    struct.pack_into("<I", data, 48, 0)                         # header_crc over the header with it 0
    struct.pack_into("<I", data, 48, zlib.crc32(bytes(data[:table_end])) & 0xFFFFFFFF)
    open(argv[1], "wb").write(data)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
