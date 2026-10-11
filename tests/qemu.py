"""QEMU driver for the itelx tests (runs inside WSL, as root).

A VM is started with the serial port written to a file and a QMP socket.
The tests read what the kernel prints (it copies the terminal to COM1),
take screenshots (QMP screendump, PPM), and type with QMP send-key.
"""
import json
import os
import re
import shutil
import socket
import subprocess
import time

OVMF = "/usr/share/ovmf/OVMF.fd"
WORK = os.environ.get("ITELX_TEST_WORK", "/root/itelx/test")

# keys for QMP send-key (US layout positions)
_SHIFTED = {
    '!': '1', '@': '2', '#': '3', '$': '4', '%': '5', '^': '6', '&': '7', '*': '8', '(': '9',
    ')': '0', '_': 'minus', '+': 'equal', '{': 'bracket_left', '}': 'bracket_right', '|': 'backslash',
    ':': 'semicolon', '"': 'apostrophe', '<': 'comma', '>': 'dot', '?': 'slash', '~': 'grave_accent',
}
_PLAIN = {
    ' ': 'spc', '-': 'minus', '=': 'equal', '[': 'bracket_left', ']': 'bracket_right',
    '\\': 'backslash', ';': 'semicolon', "'": 'apostrophe', ',': 'comma', '.': 'dot',
    '/': 'slash', '`': 'grave_accent', '\n': 'ret', '\t': 'tab',
}


def key_for(ch):
    """QMP key list for one character typed on a US keyboard."""
    if 'a' <= ch <= 'z' or '0' <= ch <= '9':
        return [ch]
    if 'A' <= ch <= 'Z':
        return ['shift', ch.lower()]
    if ch in _PLAIN:
        return [_PLAIN[ch]]
    if ch in _SHIFTED:
        return ['shift', _SHIFTED[ch]]
    raise ValueError("cannot type %r" % ch)


class VM:
    def __init__(self, name, disk, firmware="bios", machine="pc", mem=512, extra=None,
                 snapshot=False, disk_if=None, cpus=2, video_id=None):
        self.name = name
        self.dir = os.path.join(WORK, name)
        shutil.rmtree(self.dir, ignore_errors=True)
        os.makedirs(self.dir)
        self.serial = os.path.join(self.dir, "serial.log")
        self.qmp_path = os.path.join(self.dir, "qmp.sock")
        self.disk = disk
        self.firmware = firmware
        self.machine = machine
        self.mem = mem
        self.extra = extra or []
        self.snapshot = snapshot
        self.disk_if = disk_if or ("ide" if machine == "pc" else "ahci")
        self.cpus = cpus
        self.video_id = video_id      # a named VGA device: USB input bound to it (display=) gets input-send-event
        self.proc = None
        self.qmp = None
        self.shots = 0

    def cmdline(self):
        c = ["qemu-system-x86_64", "-machine", self.machine + ",accel=kvm", "-cpu", "host",
             "-smp", str(self.cpus), "-m", str(self.mem),
             "-display", "none", "-no-reboot",
             "-serial", "file:" + self.serial,
             "-qmp", "unix:%s,server,nowait" % self.qmp_path]
        c += ["-vga", "none", "-device", "VGA,id=" + self.video_id] if self.video_id else ["-vga", "std"]
        if self.firmware == "uefi":
            c += ["-bios", OVMF]
        if self.disk:
            snap = ",snapshot=on" if self.snapshot else ""
            if self.disk_if == "ide":
                c += ["-drive", "file=%s,format=raw,if=ide%s" % (self.disk, snap)]
            elif self.disk_if == "ahci":
                c += ["-drive", "id=d0,file=%s,format=raw,if=none%s" % (self.disk, snap),
                      "-device", "ide-hd,drive=d0,bus=ide.0"]
            elif self.disk_if == "nvme":
                c += ["-drive", "id=d0,file=%s,format=raw,if=none%s" % (self.disk, snap),
                      "-device", "nvme,drive=d0,serial=itelx0"]
        return c + self.extra

    def start(self):
        open(self.serial, "w").close()
        self.proc = subprocess.Popen(self.cmdline(), stdout=open(os.path.join(self.dir, "qemu.out"), "w"),
                                     stderr=subprocess.STDOUT)
        for _ in range(100):
            if os.path.exists(self.qmp_path):
                break
            time.sleep(0.05)
        self.qmp = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        for _ in range(100):
            try:
                self.qmp.connect(self.qmp_path)
                break
            except OSError:
                time.sleep(0.05)
        self.qf = self.qmp.makefile("rw")
        json.loads(self.qf.readline())
        self.cmd("qmp_capabilities")
        return self

    def cmd(self, execute, **args):
        msg = {"execute": execute}
        if args:
            msg["arguments"] = args
        self.qf.write(json.dumps(msg) + "\n")
        self.qf.flush()
        while True:
            r = json.loads(self.qf.readline())
            if "return" in r or "error" in r:
                if "error" in r:
                    raise RuntimeError("QMP %s: %s" % (execute, r["error"]))
                return r["return"]

    # ------------------------------------------------------------- serial
    def log(self):
        with open(self.serial, "rb") as f:
            return f.read().decode("utf-8", "replace").replace("\r\n", "\n")

    def wait(self, pattern, timeout=30, start=0):
        """Waits until the serial log (from offset start) matches pattern."""
        rx = re.compile(pattern)
        end = time.time() + timeout
        while time.time() < end:
            text = self.log()
            m = rx.search(text, start)
            if m:
                return m
            if self.proc.poll() is not None:
                break
            time.sleep(0.1)
        raise TimeoutError("%s: no %r in the serial log after %ss; tail:\n%s" % (
            self.name, pattern, timeout, self.log()[-1500:]))

    def mark(self):
        return len(self.log())

    # --------------------------------------------------------- keyboard
    def key(self, *keys, hold=30):
        self.cmd("send-key", keys=[{"type": "qcode", "data": k} for k in keys], **{"hold-time": hold})
        time.sleep(0.01)

    # Arabic letters are typed as a user does: Alt+Shift to the Arabic
    # layout (Arabic 101), the key under the letter, Alt+Shift back
    AR_KEYS = {'ض': 'q', 'ص': 'w', 'ث': 'e', 'ق': 'r', 'ف': 't', 'غ': 'y', 'ع': 'u', 'ه': 'i', 'خ': 'o',
               'ح': 'p', 'ج': '[', 'د': ']', 'ش': 'a', 'س': 's', 'ي': 'd', 'ب': 'f', 'ل': 'g', 'ا': 'h',
               'ت': 'j', 'ن': 'k', 'م': 'l', 'ك': ';', 'ط': "'", 'ئ': 'z', 'ء': 'x', 'ؤ': 'c', 'ر': 'v',
               'ى': 'n', 'ة': 'm', 'و': ',', 'ز': '.', 'ظ': '/', 'ذ': '`'}

    def type(self, text, delay=0.0):
        arabic = False
        for ch in text:
            if ch in self.AR_KEYS:
                if not arabic:
                    self.key("alt", "shift")
                    arabic = True
                self.key(*key_for(self.AR_KEYS[ch]))
            else:
                if arabic and not (ch == ' ' or ch.isdigit()):
                    self.key("alt", "shift")
                    arabic = False
                self.key(*key_for(ch))
            if delay:
                time.sleep(delay)
        if arabic:
            self.key("alt", "shift")

    # USB input: events for the input devices bound to the VGA console
    # (-device usb-kbd,display=<video_id>); the PS/2 keyboard gets none of them
    def ukey(self, *keys, hold=0.03):
        ev = lambda k, down: {"type": "key", "data": {"down": down, "key": {"type": "qcode", "data": k}}}
        self.cmd("input-send-event", device=self.video_id, events=[ev(k, True) for k in keys])
        time.sleep(hold)
        self.cmd("input-send-event", device=self.video_id, events=[ev(k, False) for k in reversed(keys)])
        time.sleep(0.02)

    def utype(self, text):
        arabic = False
        for ch in text:
            if ch in self.AR_KEYS:
                if not arabic:
                    self.ukey("alt", "shift")
                    arabic = True
                self.ukey(*key_for(self.AR_KEYS[ch]))
            else:
                if arabic and not (ch == ' ' or ch.isdigit()):
                    self.ukey("alt", "shift")
                    arabic = False
                self.ukey(*key_for(ch))
        if arabic:
            self.ukey("alt", "shift")

    def urun(self, command, timeout=30):
        """run(), typed on the USB keyboard"""
        m = self.mark()
        self.utype(command + "\n")
        echo_end = m + len(command) + 1
        self.wait(self.PROMPT, timeout, start=echo_end)
        time.sleep(0.05)
        text = self.log()[echo_end:]
        found = list(re.finditer(self.PROMPT, text))
        return text[:found[-1].start()] if found else text

    def wheel(self, up=True, n=1):
        for _ in range(n):
            for down in (True, False):
                self.cmd("input-send-event", device=self.video_id,
                         events=[{"type": "btn", "data": {"down": down, "button": "wheel-up" if up else "wheel-down"}}])
                time.sleep(0.03)

    def hmp(self, line):
        return self.cmd("human-monitor-command", **{"command-line": line})

    def line(self, text, wait=None, timeout=20):
        m = self.mark()
        self.type(text + "\n")
        if wait:
            return self.wait(wait, timeout, start=m)
        return None

    PROMPT = r"itelx:[^\n]*> "

    def run(self, command, timeout=30):
        """Types a terminal command, waits for the next prompt and returns
        what was printed in between (the echoed command excluded)."""
        m = self.mark()
        self.type(command + "\n")
        echo_end = m + len(command) + 1
        self.wait(self.PROMPT, timeout, start=echo_end)
        time.sleep(0.05)
        text = self.log()[echo_end:]
        found = list(re.finditer(self.PROMPT, text))
        return text[:found[-1].start()] if found else text

    # -------------------------------------------------------- screen
    def screendump(self, label=None):
        self.shots += 1
        path = os.path.join(self.dir, "%02d-%s.ppm" % (self.shots, label or "screen"))
        self.cmd("screendump", filename=path)
        return Image(path)

    def stop(self):
        if self.proc and self.proc.poll() is None:
            try:
                self.cmd("quit")
            except Exception:
                pass
            try:
                self.proc.wait(10)
            except subprocess.TimeoutExpired:
                self.proc.kill()
        if self.qmp:
            self.qmp.close()


class Image:
    """A PPM (P6) screenshot."""

    def __init__(self, path):
        for _ in range(50):
            if os.path.exists(path) and os.path.getsize(path) > 15:
                break
            time.sleep(0.05)
        data = open(path, "rb").read()
        parts = data.split(b"\n", 3)
        assert parts[0] == b"P6", "not a P6 file"
        self.w, self.h = map(int, parts[1].split())
        self.px = parts[3]
        self.path = path

    def at(self, x, y):
        i = (y * self.w + x) * 3
        return (self.px[i] << 16) | (self.px[i + 1] << 8) | self.px[i + 2]

    def count(self, rgb, x0=0, y0=0, x1=None, y1=None):
        x1 = self.w if x1 is None else x1
        y1 = self.h if y1 is None else y1
        n = 0
        want = bytes([(rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255])
        for y in range(y0, y1):
            row = self.px[(y * self.w + x0) * 3:(y * self.w + x1) * 3]
            n += sum(1 for i in range(0, len(row), 3) if row[i:i + 3] == want)
        return n

    def to_png(self, out):
        """Writes a PNG (zlib only, no imaging library needed)."""
        import struct
        import zlib
        raw = b"".join(b"\0" + self.px[y * self.w * 3:(y + 1) * self.w * 3] for y in range(self.h))

        def chunk(t, d):
            c = struct.pack(">I", len(d)) + t + d
            return c + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
        png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", self.w, self.h, 8, 2, 0, 0, 0))
        png += chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b"")
        open(out, "wb").write(png)
