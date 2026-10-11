#!/usr/bin/env python3
"""Tests of the PC tools: tools/xvc2xvr.py, tools/xvrinfo.py and the XVC
interpreter for the PC (build/host/xvc). Run with "make check" (Linux/WSL),
or: python3 tests/test_tools.py [path/to/host/xvc]

The programs themselves are tested on a real itelx by tests/e2e_itelx.py.
"""
import glob
import os
import subprocess
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import xvc2xvr  # noqa: E402
from xvc2xvr import XvrError, convert, parse, payload_of  # noqa: E402

STUB = os.path.join(ROOT, "usb", "dex", "xvc2xvr.xvr")
HOST_XVC = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "host", "xvc")


def read(path):
    with open(path, "rb") as f:
        return f.read()


class Converter(unittest.TestCase):
    def setUp(self):
        self.stub = read(STUB)

    def test_the_stub_is_the_converter(self):
        h, segs = parse(self.stub)
        self.assertEqual(payload_of(self.stub, segs), (None, b""))
        self.assertEqual(h["name"].rstrip(b"\0"), b"xvc2xvr")

    def test_every_app_converts_and_its_source_comes_back(self):
        apps = sorted(glob.glob(os.path.join(ROOT, "apps", "xvc", "*.xvc")))
        self.assertGreater(len(apps), 10)
        for path in apps:
            src = read(path)
            name = os.path.basename(path)
            out = convert(self.stub, src, name, name[:-4])
            h, segs = parse(out)                      # the kernel's checks
            self.assertEqual(payload_of(out, segs), (name, src))
            self.assertEqual(h["name"].rstrip(b"\0").decode(), name[:-4])
            self.assertEqual(segs[-1]["memsz"], segs[-1]["filesz"] + 1)       # a zero after the source
            table_end = 128 + 32 * h["seg_count"]
            off = segs[-1]["file_offset"]
            self.assertEqual(out[table_end:off], self.stub[table_end:off], "the interpreter itself is not changed")

    def test_long_and_arabic_names_are_cut_on_a_character(self):
        name = "برنامج_طويل_جدا_جدا_جدا_جدا_جدا.xvc"
        out = convert(self.stub, b"print(1)\n", name, name[:-4])
        h, segs = parse(out)
        h["name"].rstrip(b"\0").decode("utf-8")       # still valid UTF-8
        self.assertLessEqual(len(h["name"].rstrip(b"\0")), 39)
        self.assertIsNotNone(payload_of(out, segs))

    def test_the_utf8_mark_of_windows_editors_is_dropped(self):
        out = convert(self.stub, b"\xef\xbb\xbfprint(1)\n", "a.xvc", "a")
        self.assertEqual(payload_of(out, parse(out)[1]), ("a.xvc", b"print(1)\n"))

    def test_what_is_refused(self):
        with self.assertRaises(XvrError):
            convert(self.stub, b"print(1)\0\n", "a.xvc", "a")
        with self.assertRaises(XvrError):
            convert(self.stub, b"\xff\xfe bad", "a.xvc", "a")
        made = convert(self.stub, b"print(1)\n", "a.xvc", "a")
        with self.assertRaisesRegex(XvrError, "not xvc2xvr"):
            convert(made, b"print(2)\n", "b.xvc", "b")
        update = read(os.path.join(ROOT, "usb", "update.xvr"))
        with self.assertRaisesRegex(XvrError, "not xvc2xvr"):
            convert(update, b"print(2)\n", "b.xvc", "b")

    def test_damage_is_found_as_the_kernel_finds_it(self):
        made = bytearray(convert(self.stub, b"print(1)\n", "a.xvc", "a"))
        with self.assertRaisesRegex(XvrError, "truncated"):
            parse(bytes(made[:-1]))
        bad = bytearray(made)
        bad[-3] ^= 0x20
        with self.assertRaisesRegex(XvrError, "payload checksum"):
            parse(bytes(bad))
        bad = bytearray(made)
        bad[100] ^= 1                                  # in the name
        with self.assertRaisesRegex(XvrError, "header checksum"):
            parse(bytes(bad))
        with self.assertRaisesRegex(XvrError, "bad magic"):
            parse(b"MZ" + bytes(200))

    def test_every_xvr_in_usb_is_valid(self):
        files = glob.glob(os.path.join(ROOT, "usb", "dex", "*.xvr")) + [os.path.join(ROOT, "usb", "update.xvr")]
        for path in files:
            parse(read(path))

    def test_command_line(self):
        tmp = os.path.join(ROOT, "build", "test-tools")
        os.makedirs(tmp, exist_ok=True)
        src = os.path.join(tmp, "hello.xvc")
        with open(src, "w", encoding="utf-8") as f:
            f.write('print("hello")\n')
        r = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "xvc2xvr.py"), src], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertTrue(os.path.exists(src[:-4] + ".xvr"))
        r = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "xvrinfo.py"), "--source", src[:-4] + ".xvr"],
                           capture_output=True, text=True)
        self.assertEqual(r.stdout, 'print("hello")\n')
        r = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "xvrinfo.py"), src], capture_output=True, text=True)
        self.assertEqual(r.returncode, 1)
        self.assertIn("itelx would refuse it", r.stderr)


@unittest.skipUnless(os.path.exists(HOST_XVC), "build/host/xvc is not built (make host-xvc)")
class HostXvc(unittest.TestCase):
    def xvc(self, *args, stdin=""):
        r = subprocess.run([HOST_XVC] + list(args), input=stdin, capture_output=True, text=True, cwd=ROOT)
        return r.returncode, r.stdout + r.stderr

    def app(self, name):
        return os.path.join(ROOT, "apps", "xvc", name + ".xvc")

    def test_programs_with_arguments(self):
        self.assertIn("= 1/2 = 0.5", self.xvc(self.app("calc"), "1/3", "+", "1/6")[1])
        self.assertIn("2 3 5 7 11 13 17 19 23 29", self.xvc(self.app("primes"), "30")[1])
        self.assertIn("0b11111111", self.xvc(self.app("base"), "255")[1])
        self.assertIn("Khoor", self.xvc(self.app("cipher"), "caesar", "3", "Hello")[1])
        self.assertIn("6.213712 mi", self.xvc(self.app("convert"), "10", "km", "mi")[1])

    def test_int_reads_decimal_like_itelx(self):
        code, out = self.xvc(self.app("calc"), "010 + 08")
        self.assertIn("= 18", out)

    def test_errors_name_the_line(self):
        tmp = os.path.join(ROOT, "build", "test-tools")
        os.makedirs(tmp, exist_ok=True)
        bad = os.path.join(tmp, "bad.xvc")
        with open(bad, "w") as f:
            f.write("let a = [1, 2]\nprint(a[5])\n")
        code, out = self.xvc(bad)
        self.assertEqual(code, 1)
        self.assertIn("bad.xvc:2: runtime error: list index 5 out of range", out)
        with open(bad, "w") as f:
            f.write("print(1 +)\n")
        code, out = self.xvc(bad)
        self.assertEqual(code, 2)
        self.assertIn("bad.xvc:1: expected an expression", out)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]], verbosity=2)
