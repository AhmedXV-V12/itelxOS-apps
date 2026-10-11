#!/usr/bin/env python3
"""End-to-end test of the itelxOS apps on a real itelxOS 1.3 in QEMU.

What a user does, step by step: a FAT32 USB stick is made from usb/ (plus
two programs converted on the PC with tools/xvc2xvr.py and checksum lists),
itelx starts, the stick is plugged in, the update is accepted ("are you
need update ?" -> yes), and then every app is started from the terminal,
keys are typed on the keyboard, and what it prints is checked. xvc2xvr is
also run on itelx itself. At the end the stick is ejected and checked with
fsck.fat.

Runs inside Linux/WSL as root (KVM), with qemu-system-x86_64, OVMF,
dosfstools and mtools:

    python3 tests/e2e_itelx.py --image /path/to/itelxOS.img [--fw uefi|bios] [--only name,...]

itelxOS.img is the system disk image of itelxOS 1.3 (out/itelxOS.img of
its build). The test works on a copy. ITELX_TEST_WORK sets the work folder.
"""
import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
import time
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
from qemu import VM, WORK  # noqa: E402
import xvc2xvr  # noqa: E402

ENV = dict(os.environ, MTOOLS_SKIP_CHECK="1", LANG="C.UTF-8", LC_ALL="C.UTF-8")
USB = os.path.join(ROOT, "usb")
results = []


def record(name, ok, detail=""):
    results.append((name, ok))
    print("%s  %s" % ("PASS" if ok else "FAIL", name), flush=True)
    if not ok and detail:
        print("      " + detail.replace("\n", "\n      ")[-3000:], flush=True)


def plain(text):
    """terminal output without colour codes"""
    return re.sub(r"\x1b\[[0-9;?]*[A-Za-z]", "", text)


def expect(name, text, *patterns, absent=()):
    t = plain(text)
    missing = [p for p in patterns if not re.search(p, t)]
    present = [p for p in absent if re.search(p, t)]
    record(name, not missing and not present,
           "missing %r, present %r\n--- output:\n%s" % (missing, present, t))


def sh(*cmd):
    r = subprocess.run(list(cmd), capture_output=True, text=True, env=ENV)
    if r.returncode:
        raise RuntimeError("%s: %s%s" % (" ".join(cmd), r.stdout, r.stderr))
    return r.stdout


def make_stick(path, label="APPS"):
    """a FAT32 stick: usb/, two programs converted on the PC, a broken
    program, and SHA-256 lists of the apps (on the stick and installed)"""
    if os.path.exists(path):
        os.remove(path)
    with open(path, "wb") as f:
        f.truncate(64 << 20)
    sh("mkfs.vfat", "-F", "32", "-n", label, path)
    for name in sorted(os.listdir(USB)):
        sh("mcopy", "-s", "-i", path, os.path.join(USB, name), "::/" + name)
    stub = open(os.path.join(USB, "dex", "xvc2xvr.xvr"), "rb").read()
    sh("mmd", "-i", path, "::/pc")
    for name in ("primes", "base"):
        src = os.path.join(ROOT, "apps", "xvc", name + ".xvc")
        data = xvc2xvr.convert(stub, open(src, "rb").read(), name + ".xvc", name + "pc")
        tmp = os.path.join(WORK, name + "pc.xvr")
        open(tmp, "wb").write(data)
        sh("mcopy", "-i", path, tmp, "::/pc/" + name + "pc.xvr")
        if name == "primes":                          # and a copy damaged on the way
            damaged = bytearray(data)
            damaged[-10] ^= 0x01
            tmp = os.path.join(WORK, "damaged.xvr")
            open(tmp, "wb").write(bytes(damaged))
            sh("mcopy", "-i", path, tmp, "::/pc/damaged.xvr")
    bad = os.path.join(WORK, "broken.xvc")
    open(bad, "w").write("let a = 1\nprint(a +)\n")
    sh("mcopy", "-i", path, bad, "::/pc/broken.xvc")
    bom = os.path.join(WORK, "bom.xvc")                   # saved by Notepad as "UTF-8 with BOM", CRLF
    open(bom, "wb").write(b"\xef\xbb\xbfprint(\"made on Windows\")\r\nprint(len(args))\r\n")
    sh("mcopy", "-i", path, bom, "::/pc/bom.xvc")
    installed = []                                     # usb/SHA256SUMS covers the stick itself
    for name in sorted(os.listdir(os.path.join(USB, "dex"))):
        h = hashlib.sha256(open(os.path.join(USB, "dex", name), "rb").read()).hexdigest()
        installed.append("%s  /dex/%s\n" % (h, name))
    tmp = os.path.join(WORK, "INSTALLED.SUMS")
    open(tmp, "w").write("".join(installed))
    sh("mcopy", "-i", path, tmp, "::/INSTALLED.SUMS")


class Session:
    """an itelx with the stick plugged in and the apps installed"""

    def __init__(self, image, fw):
        self.fw = fw
        os.makedirs(WORK, exist_ok=True)
        disk = os.path.join(WORK, "apps-sys-%s.img" % fw)
        shutil.copyfile(image, disk)
        self.stick = os.path.join(WORK, "apps-stick-%s.img" % fw)
        make_stick(self.stick)
        self.vm = VM("apps-" + fw, disk, firmware=fw, machine="q35", mem=512, video_id="video0",
                     extra=["-device", "qemu-xhci,id=xhci,p2=8,p3=8"]).start()
        self.vm.wait(r"itelx:/> ", 90)

    def plug(self):
        vm = self.vm
        vm.hmp("drive_add 0 id=apps,if=none,file=%s,format=raw" % self.stick)
        m = vm.mark()
        vm.cmd("device_add", driver="usb-storage", id="appsdev", drive="apps", bus="xhci.0", port="1")
        return m

    def run(self, command, timeout=30):
        return self.vm.run(command, timeout)

    def text(self):
        """the serial log without colour codes (positions in it stay valid
        as the log grows)"""
        return plain(self.vm.log())

    def wait(self, pattern, timeout=30, start=0):
        rx = re.compile(pattern)
        end = time.time() + timeout
        while time.time() < end:
            m = rx.search(self.text(), start)
            if m:
                return m
            if self.vm.proc.poll() is not None:
                break
            time.sleep(0.1)
        raise TimeoutError("no %r on the screen after %ss; the end of it:\n%s" % (pattern, timeout, self.text()[-1500:]))

    def talk(self, command, steps, timeout=30, settle=0.35):
        """types a command, then for each (pattern, keys) waits for the
        pattern (in the text without colour codes) and types the keys: a str
        is typed as text, a tuple is a list of key names (QMP qcodes, "a+b"
        for a combination) pressed one after another. Returns everything
        printed until the terminal's prompt is back."""
        vm = self.vm
        m = len(self.text())
        vm.type(command + "\n")
        pos = m + len(command) + 1
        for pattern, keys in steps:
            if pattern:
                pos = self.wait(pattern, timeout, start=pos).end()
            time.sleep(settle)
            if isinstance(keys, tuple):
                for k in keys:
                    vm.key(*k.split("+"))
                    time.sleep(0.15)
            elif keys:
                vm.type(keys)
        self.wait(VM.PROMPT, timeout, start=pos)
        time.sleep(0.1)
        text = self.text()[m + len(command) + 1:]
        found = list(re.finditer(VM.PROMPT, text))
        return text[:found[-1].start()] if found else text

    def stop(self):
        self.vm.stop()


# ------------------------------------------------------------- install

def t_install(s):
    m = s.plug()
    s.vm.wait(r"are you need update \? \(yes/no\)", 30, start=m)
    time.sleep(0.3)
    s.vm.type("yes\n")
    s.vm.wait(r"run them by name", 60, start=m)
    s.vm.wait(VM.PROMPT, 20, start=m)
    out = s.vm.log()[m:]
    n = len(os.listdir(os.path.join(USB, "dex")))
    expect("the stick offers the update and installs every app on yes", out,
           r"holds an itelx update: itelxOS apps 1\.0", r"update done: %d program\(s\) installed" % n,
           r"installed /dex/xvc2xvr\.xvr", absent=[r"skipped"])
    out = s.run("sum -c /media/APPS/SHA256SUMS", 60) + s.run("sum -c /media/APPS/INSTALLED.SUMS", 60)
    expect("sum -c: the kit on the stick (its SHA256SUMS) and the installed copies match the PC's SHA-256", out,
           r"update\.xvr: OK", r"%d OK, 0 failed, 0 missing[\s\S]*%d OK, 0 failed, 0 missing" % (n + 1, n),
           absent=[r"FAILED", r"MISSING"])


def t_convert(s):
    out = s.run("run /media/APPS/pc/primespc.xvr 30") + s.run("basepc 255")
    expect("XVRs made on the PC by xvc2xvr.py run from the stick (args reach the program)", out,
           r"2 3 5 7 11 13 17 19 23 29", r"unknown command: basepc")
    out = s.run("install /media/APPS/pc/basepc.xvr") + s.run("basepc 0b101")
    expect("an XVR made on the PC installs and runs by its name", out, r"installed /dex/basepc\.xvr", r"dec\s+5")
    out = s.run("/media/APPS/pc/damaged.xvr") + s.run("install /media/APPS/pc/damaged.xvr")
    expect("a converted XVR damaged on the way is refused by the kernel and by install", out,
           r"damaged\.xvr: not a valid XVR executable: payload checksum mismatch",
           r"not installed: payload checksum mismatch")
    out = s.run("xvc2xvr /dex/base.xvc /dex/base2.xvr") + s.run("base2 255")
    expect("xvc2xvr on itelx converts an installed program; the result runs by its name", out,
           r"made /dex/base2\.xvr", r"run it by its name: base2", r"hex\s+0xff", r"bin\s+0b11111111")
    out = s.run("xvc2xvr /media/APPS/pc/broken.xvc") + s.run("ls /media/APPS/pc")
    expect("xvc2xvr refuses a program with an error, with its line", out,
           r"broken\.xvc:2: expected an expression", r"was not converted", absent=[r"broken\.xvr"])
    out = s.run("xvc2xvr")
    expect("xvc2xvr without arguments explains itself", out, r"usage: xvc2xvr <program\.xvc> \[output\.xvr\]")
    out = s.run("xvc2xvr /media/APPS/pc/bom.xvc /dex/bom.xvr") + s.run("bom a b")
    expect("xvc2xvr takes a Windows file (UTF-8 mark, CRLF lines)", out, r"made /dex/bom\.xvr", r"made on Windows\n3")


# ------------------------------------------------------- XVC programs

def t_welcome(s):
    out = s.run("welcome") + s.run("welcome calc")
    expect("welcome lists the apps, all installed, and explains one", out,
           r"itelxOS apps - type a name", r"calc\s+a calculator", r"xvc2xvr\s+make an XVR",
           r"calc\.xvc - a calculator that keeps fractions exact", absent=[r"^- ", r"\n- "])


def t_calc(s):
    out = s.run("calc 1/3 + 1/6") + s.run("calc 2^10 * (0.1 + 0.2)") + s.run("calc 1/0")
    expect("calc works out expressions with exact fractions", out,
           r"= 1/2 = 0\.5", r"= 1536/5 = 307\.2", r"error: division by zero")
    out = s.talk("calc", [(r"calc> ", "x = 12\n"), (r"x = 12\n", "x * 2 + 1/4\n"), (r"= 97/4", "vars\n"),
                          (r"x = 12\n", "quit\n")])
    expect("calc keeps variables in the interactive mode", out, r"= 97/4 = 24\.25", r"ans = 97/4")


def t_convert_units(s):
    out = s.run("convert 10 km mi") + s.run("convert 36.6 c f") + s.run("convert 1 GiB MB") + s.run("convert 1 m kg")
    expect("convert converts units", out, r"10 km = 6\.213712 mi", r"36\.6 c = 97\.88 f",
           r"1 GiB = 1073\.741824 MB", r"cannot convert length to mass")


def t_numbers(s):
    out = s.run("base 0xff 1575") + s.run("primes 30") + s.run("primes factor 360") + s.run("primes is 1000000007")
    expect("base and primes", out, r"dec\s+255", r"bin\s+0b11111111", r"char ا", r"2 3 5 7 11 13 17 19 23 29",
           r"360 = 2\^3 x 3\^2 x 5", r"1000000007 is prime")
    out = s.run("cipher caesar 3 Hello, World!") + s.run("cipher vigenere LEMON attack at dawn")
    expect("cipher", out, r"Khoor, Zruog!", r"lxfopv ef rnhr")


def t_time(s):
    out = s.run("cal 2 2028") + s.run("cal")
    expect("cal shows months and today's date", out, r"February 2028", r"27 28 29",
           r"(Sunday|Monday|Tuesday|Wednesday|Thursday|Friday|Saturday), \d+ \w+ 20\d\d\s+\d\d:\d\d:\d\d")
    out = s.run("timer 2", 20)
    expect("timer counts down", out, r"timer: 00:02", r"time is up")


def t_files(s):
    out = s.run("wc /dex/welcome.xvc /dex/calc.xvc") + s.run("tree /dex 1") + s.run("search xvc2xvr /dex")
    expect("wc, tree and search work on the disk", out, r"\d+\s+\d+\s+\d+\s+3495\s+/dex/welcome\.xvc", r"total",
           r"calc\.xvc", r"folder\(s\), \d+ file\(s\)", r"/dex/welcome\.xvc:\d+: .*xvc2xvr", r"line\(s\) found in")


def t_lists(s):
    out = s.run("todo add buy bread") + s.run("todo add write the report")
    out += s.talk("todo", [(r"todo> ", "done 1\n"), (r"done: buy bread", "list\n"), (r"1 done", "q\n")])
    expect("todo keeps a list", out, r"added 2\. write the report", r"\[x\] buy bread", r"1 to do, 1 done")
    out = s.run("notes add hello from itelx") + s.run("notes find ITELX")
    expect("notes keeps dated notes", out, r"note 1 saved", r"20\d\d-\d\d-\d\d \d\d:\d\d\s+hello from itelx",
           r"1 note\(s\)")
    out = s.talk("contacts add", [(r"name:", "Omar Ali\n"), (r"phone:", "0100 123 4567\n"), (r"note", "friend\n")])
    out += s.run("contacts find omar")
    expect("contacts keeps a phone book", out, r"saved Omar Ali", r"Omar Ali\s+0100 123 4567\s+friend", r"1 found")
    out = s.run("cat /dex/data/todo.txt")
    expect("the apps keep their data in /dex/data", out, r"1\|buy bread", r"0\|write the report")


def t_learn(s):
    out = s.run("table show 7")
    expect("table shows a multiplication table", out, r"7 x 12 =\s+84")
    # practice: answer the questions it asks (the third one wrong), then stop
    vm = s.vm
    m = vm.mark()
    vm.type("table 7\n")
    pos = m
    for q in range(3):
        mt = vm.wait(r"(\d+)\)  7 x (\d+) = ", 20, start=pos)
        pos = mt.end()
        time.sleep(0.3)
        vm.type("%d\n" % (7 * int(mt.group(2)) + (1 if q == 2 else 0)))
    vm.wait(r"4\)  7 x \d+ = ", 20, start=pos)
    time.sleep(0.3)
    vm.type("\n")
    vm.wait(VM.PROMPT, 20, start=pos)
    expect("table practice counts right and wrong answers", vm.log()[m:], r"2 of 3 right in \d+ seconds")
    # typing: type the sentence it shows
    m = vm.mark()
    vm.type("typing 1\n")
    mt = vm.wait(r"\n\x1b\[96m([a-z ]+)\x1b\[0m\n", 20, start=m)
    time.sleep(0.4)
    vm.type(mt.group(1) + "\n")
    vm.wait(VM.PROMPT, 30, start=mt.end())
    expect("typing measures the speed of a sentence typed without a mistake", vm.log()[m:],
           r"\d+ words per minute, 100% right")
    out = s.talk("quiz 2", [(r"your answer: ", ("a",)), (r"your answer: ", ("b",))])
    expect("quiz asks and scores", out, r"question 1 of 2", r"question 2 of 2", r"score: \d of 2")


def t_games_xvc(s):
    steps = [(r"type a letter", ("z",))] + [(r"tried: ", (c,)) for c in "itel"] + [(r"tried: ", ("x",))]
    out = s.talk("hangman itelx", steps)
    expect("hangman: a word is guessed letter by letter", out, r"no z in the word", r"you won! the word is itelx")
    out = s.talk("ttt", [(r"your move", ("5",)), (r"took 1 - your move", ("9",)), (r"took 3 - your move", ("7",)),
                         (r"play again\? \(y/n\)", ("n",))])
    expect("ttt: the computer answers and wins when you do not block it", out,
           r"the computer took 1", r"the computer took 3", r"the computer wins\.", r"bye: you 0, computer 1, draws 0")
    out = s.talk("hangman", [(r"type a letter", ("ctrl+c",))])
    expect("Ctrl+C stops an XVC game waiting for a key", out, r"the word was: [a-z]+")


# ------------------------------------------------------- XVR programs

def t_2048(s):
    out = s.talk("2048 --seed 7", [(r"slide the tiles", ("left",)), (r"score", ("right",)), (r"score", ("up",)),
                                    (r"score", ("down",)), (r"score", ("u",)), (r"taken back|nothing to take back", ("q",))])
    expect("2048: arrows slide, u undoes, q ends with the score", out, r"2048\s+score \d+", r"\|\s+[24]\s+\|",
           r"2048: score \d+, best \d+")
    out = s.talk("2048", [(r"slide the tiles", ("ctrl+c",))]) + s.run("echo still-here")
    expect("Ctrl+C stops an app that reads raw keys (the terminal passes it on to them)", out,
           r"\[stopped\]", r"\[exit 130\]", r"still-here")


def t_sokoban(s):
    out = s.talk("sokoban 1", [(r"push every box", ("left", "left", "up", "right", "right")),
                               (r"solved in 5 moves, the fewest possible!", ("q",))])
    expect("sokoban: level 1 solved with the fewest moves", out, r"level 1 of 11", r"solved in 5 moves",
           r"sokoban: 1 of 11 level\(s\) solved")
    out = s.talk("sokoban", [(r"level 2 of 11", ("u", "r", "n", "p", "q"))])
    expect("sokoban starts at the first level not solved yet", out, r"level 2 of 11", r"nothing to undo",
           r"level 3 of 11", r"of 11 level\(s\) solved")


def t_mines(s):
    out = s.talk("mines --seed 3", [(r"open a square", ("f",)), (r"flags 1", ("left", "up", "spc")),
                                    (r"MINES", ("q",))])
    expect("mines: the first square opens safely, flags count", out, r"MINES\s+9 x 9\s+mines 10", r"flags 1",
           r"mines: (stopped|the field is cleared)", absent=[r"BOOM"])


def t_life(s):
    out = s.run("life glider 8 0", 30) + s.run("life pulsar 3 0", 30) + s.run("life nothing")
    expect("life: patterns run their generations (a glider keeps 5 cells, a pulsar 48)", out,
           r"generation 8\s+5 alive", r"life: 5 cells alive at the end", r"life: 48 cells alive at the end",
           r"patterns: random, glider, gun")


def t_sum(s):
    s.run("add -r /dex/data")
    s.run("del /dex/data/sum.txt")
    s.run("add -f /dex/data/sum.txt hello")
    want = hashlib.sha256(b"hello\n").hexdigest()
    crc = "%08x" % (zlib.crc32(b"hello\n") & 0xFFFFFFFF)
    out = s.run("sum /dex/data/sum.txt") + s.run("sum -crc /dex/data/sum.txt")
    expect("sum: SHA-256 and CRC-32 as the PC computes them", out, want + r"  /dex/data/sum\.txt",
           crc + r"\s+6\s+/dex/data/sum\.txt")
    s.run("del /dex/data/bad.sums")
    s.run("add -f /dex/data/bad.sums %s *sum.txt" % ("0" * 64))
    out = s.run("sum -c /dex/data/bad.sums")
    expect("sum -c reports a file that is different", out, r"sum\.txt: FAILED", r"0 OK, 1 failed")


def t_sysmon(s):
    out = s.run("sysmon 1", 20)
    expect("sysmon shows the system, the memory and the processes", out, r"SYSMON\s+itelx 1\.3", r"UEFI|BIOS",
           r"memory\s+\[", r"MiB used", r"\d+ processes", r"terminal", r"sysmon")


def t_edit(s):
    out = s.talk("edit /dex/data/e.txt", [(r"a new file", "a first line\n"), (r"edit\*> ", "a second line\n"),
                                          (r"2  second line", "i 1 zero line\n"), (r"1  zero line", "r 3 third\n"),
                                          (r"3  third", "s /line/LINE/\n"), (r"replacement\(s\)", "u\n"),
                                          (r"undone", "u\n"), (r"undone", "l all\n"), (r"\(end, 3 line", "q\n"),
                                          (r"unsaved changes", "w\n"), (r"saved", "q\n")])
    out += s.run("cat /dex/data/e.txt")
    expect("edit adds, inserts, replaces, substitutes, undoes and saves lines", out, r"2 replacement\(s\) in 2 line",
           r"saved /dex/data/e\.txt: 3 line\(s\)", r"zero LINE\nfirst LINE\nthird")


def t_fm(s):
    s.run("add -r /fmtest")
    s.run("add -f /fmtest/a.txt hi from fm")
    out = s.talk("fm /fmtest", [(r"FM\s+/fmtest", ("c",)), (r"copy a\.txt to", "b.txt\n"),
                                (r"copied a\.txt to /fmtest/b\.txt", ("d",)), (r"delete a\.txt\? \(y/n\)", ("y",)),
                                (r"deleted a\.txt", ("m",)), (r"move b\.txt to", "/media/APPS\n"),
                                (r"moved b\.txt to /media/APPS/b\.txt", ("n",)), (r"name of the new folder", "sub\n"),
                                (r"created sub/", ("ret",)), (r"FM\s+/fmtest/sub", ("backspace",)),
                                (r"FM\s+/fmtest\s", ("q",))])
    expect("fm copies, deletes, moves to the stick, makes and opens folders", out,
           r"copied a\.txt to /fmtest/b\.txt", r"deleted a\.txt", r"moved b\.txt to /media/APPS/b\.txt",
           r"created sub/", r"FM\s+/fmtest/sub")
    out = s.run("ls /fmtest") + s.run("cat /media/APPS/b.txt")
    expect("what fm did is on the disks", out, r"sub/", r"hi from fm", absent=[r"a\.txt", r"b\.txt"])
    out = s.talk("fm /", [(r"FM\s+/\s", ("d",)), (r"it stays", ("q",))])
    expect("fm does not delete what itelx needs", out, r"itelx needs /\w+: it stays")


TESTS = [("install", t_install), ("convert", t_convert), ("welcome", t_welcome), ("calc", t_calc),
         ("units", t_convert_units), ("numbers", t_numbers), ("time", t_time), ("files", t_files),
         ("lists", t_lists), ("learn", t_learn), ("games", t_games_xvc), ("2048", t_2048),
         ("sokoban", t_sokoban), ("mines", t_mines), ("life", t_life), ("sum", t_sum), ("sysmon", t_sysmon),
         ("edit", t_edit), ("fm", t_fm)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", required=True, help="itelxOS 1.3 system disk image")
    ap.add_argument("--fw", default="uefi", choices=["uefi", "bios"])
    ap.add_argument("--only", default="", help="comma separated test names (install always runs)")
    a = ap.parse_args()
    only = set(filter(None, a.only.split(",")))
    s = Session(a.image, a.fw)
    try:
        for name, fn in TESTS:
            if name == "install" or not only or name in only:
                try:
                    fn(s)
                except Exception as e:  # noqa: BLE001 - a test that breaks is a failure, the rest go on
                    record("%s: %s" % (name, type(e).__name__), False, str(e))
                    s.vm.key("ctrl", "c")
                    time.sleep(0.5)
                    s.vm.type("\n")
                    time.sleep(0.5)
        m = s.vm.mark()
        s.vm.type("eject /media/APPS\n")
        s.vm.wait(r"itelx:/> ", 20, start=m)
        s.vm.type("poweroff\n")
        s.vm.wait(r"itfs: root filesystem closed cleanly", 30, start=m)
        time.sleep(1)
    finally:
        s.stop()
    r = subprocess.run(["fsck.fat", "-n", s.stick], capture_output=True, text=True, env=ENV)
    record("the stick is consistent after eject (fsck.fat)", r.returncode == 0, r.stdout + r.stderr)
    failed = [n for n, ok in results if not ok]
    print("\n%d passed, %d failed (itelx %s)" % (len(results) - len(failed), len(failed), a.fw))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
