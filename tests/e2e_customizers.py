#!/usr/bin/env python3
"""End-to-end test of the customizer apps on a real itelxOS 1.4 in QEMU.

What a user does: a FAT32 USB stick with customizers/release/ on it is
plugged into itelx, a customizer is started from the terminal (or from the
stick as update.xvr when it brings a kernel), the yes is typed, the
customizer's own program comes up instead of the terminal, it is used with
keys typed on the keyboard, and at the end recustomize puts itelx back as
installed: the files on the system disk are compared with the original.

Runs inside Linux/WSL as root (KVM), with qemu-system-x86_64, OVMF,
dosfstools and mtools:

    python3 tests/e2e_customizers.py --image /path/to/itelxOS.img [--fw uefi|bios] [--only noter,...]

itelxOS.img is the system disk image of itelxOS 1.4 (out/itelxOS.img of its
build). ITELX_TEST_WORK sets the work folder.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
from qemu import VM, WORK  # noqa: E402

ENV = dict(os.environ, MTOOLS_SKIP_CHECK="1", LANG="C.UTF-8", LC_ALL="C.UTF-8")
REL = os.path.join(ROOT, "customizers", "release")
FACTORY = os.path.join(ROOT, "customizers", "factory", "itelx-1.4")
results = []


def record(name, ok, detail=""):
    results.append((name, ok))
    print("%s  %s" % ("PASS" if ok else "FAIL", name), flush=True)
    if not ok and detail:
        print("      " + detail.replace("\n", "\n      ")[-4000:], flush=True)


def plain(text):
    return re.sub(r"\x1b\[[0-9;?]*[A-Za-z]", "", text)


def expect(name, text, *patterns, absent=()):
    t = plain(text)
    missing = [p for p in patterns if not re.search(p, t)]
    present = [p for p in absent if re.search(p, t)]
    record(name, not missing and not present,
           "missing %r, present %r\n--- output:\n%s" % (missing, present, t[-3000:]))
    return not missing and not present


def sh(*cmd):
    r = subprocess.run(list(cmd), capture_output=True, text=True, env=ENV)
    if r.returncode:
        raise RuntimeError("%s: %s%s" % (" ".join(cmd), r.stdout, r.stderr))
    return r.stdout


def make_stick(path, files=(), label="CZ", size_mib=96):
    if os.path.exists(path):
        os.remove(path)
    with open(path, "wb") as f:
        f.truncate(size_mib << 20)
    sh("mkfs.vfat", "-F", "32", "-n", label, path)
    for src, dst in files:
        sh("mcopy", "-i", path, src, "::/" + dst)


def stick_ls(path):
    return sh("mdir", "-b", "-i", path, "::/")


class Session:
    """an itelx 1.4 started from a copy of the image, with an xHCI port free
    for a stick"""

    def __init__(self, image, fw, name, net=None):
        self.fw = fw
        self.name = name
        os.makedirs(WORK, exist_ok=True)
        self.disk = os.path.join(WORK, "cz-%s-%s.img" % (name, fw))
        shutil.copyfile(image, self.disk)
        self.net = net
        self.n = 0
        self.boot()

    def boot(self):
        extra = ["-device", "qemu-xhci,id=xhci,p2=8,p3=8"]
        if self.net:
            extra += self.net
        self.vm = VM("cz-%s-%s" % (self.name, self.fw), self.disk, firmware=self.fw, machine="q35", mem=768,
                     video_id="video0", extra=extra).start()

    def reboot(self, wait=None, timeout=90):
        self.vm.stop()
        self.boot()
        if wait:
            self.wait(wait, timeout)

    def plug(self, stick):
        self.n += 1
        self.vm.hmp("drive_add 0 id=st%d,if=none,file=%s,format=raw" % (self.n, stick))
        m = len(self.text())
        self.vm.cmd("device_add", driver="usb-storage", id="stdev%d" % self.n, drive="st%d" % self.n,
                    bus="xhci.0", port="1")
        return m

    def unplug(self):
        self.vm.cmd("device_del", id="stdev%d" % self.n)
        time.sleep(1.5)

    def text(self):
        return plain(self.vm.log())

    def mark(self):
        return len(self.text())

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
        raise TimeoutError("no %r after %ss; the end of it:\n%s" % (pattern, timeout, self.text()[-2500:]))

    def type(self, text, settle=0.25):
        time.sleep(settle)
        self.vm.type(text)

    def keys(self, *names, gap=0.12):
        for k in names:
            self.vm.key(*k.split("+"))
            time.sleep(gap)

    def run(self, command, timeout=30):
        return self.vm.run(command, timeout)

    def shot(self, label):
        """a screenshot, kept outside the VM's folder (a restart empties that)"""
        img = self.vm.screendump(label)
        keep = os.path.join(WORK, "shots-" + self.name)
        os.makedirs(keep, exist_ok=True)
        png = os.path.join(keep, os.path.basename(img.path)[:-4] + ".png")
        img.to_png(png)
        return png

    def stop(self):
        self.vm.stop()


def factory_list():
    files = set()
    for top, _, names in os.walk(os.path.join(FACTORY, "rootfs")):
        for n in names:
            files.add("/" + os.path.relpath(os.path.join(top, n), os.path.join(FACTORY, "rootfs")).replace(os.sep, "/"))
    return files


# ------------------------------------------------------------ the tests

def t_noter_install(s, stick):
    m = s.plug(stick)
    s.wait(r"media: .* is /media/CZ", 20, start=0)
    time.sleep(1)
    m = s.mark()
    s.type("/media/CZ/noter.xvr\n")
    s.wait(r"Install it\? \(yes/no\)", 30, start=m)
    s.type("yes\n")
    s.wait(r"Start it now\? \(yes/no\)", 60, start=m)
    out = s.text()[m:]
    expect("noter: installs from the terminal (no kernel, no stick dance)", out,
           r"Noter 1\.0 - an itelx customizer app", r"/system/terminal\.xvr", r"Noter is installed")
    s.type("yes\n")
    return m


def t_noter_use(s):
    m = s.mark()
    s.wait(r"Welcome to Noter", 20, start=m - 200 if m > 200 else 0)
    s.type("Ahmed\n")
    s.wait(r"Ahmed\.", 20, start=m)
    s.wait(r"Your journal is empty", 10, start=m)
    shots = [s.shot("noter-empty")]
    m = s.mark()
    s.keys("n")
    s.wait(r"new entry", 10, start=m)
    s.type("My first day with Noter\nA long walk by the sea. #walk #sea\n")
    time.sleep(0.5)
    s.keys("ctrl+s")
    s.wait(r"how do you feel", 10, start=m)
    s.keys("4")
    s.wait(r"A long walk by the sea", 10, start=s.mark() - 3000)
    shots.append(s.shot("noter-read"))
    m = s.mark()
    s.keys("esc")
    s.wait(r"My first day with Noter", 10, start=m)
    m = s.mark()
    s.keys("n")
    s.wait(r"new entry", 10, start=m)
    s.type("يوم جميل مع العائلة\nذهبنا الى البحر #عائلة\n")
    time.sleep(0.5)
    s.keys("ctrl+s")
    s.wait(r"how do you feel", 10, start=m)
    s.keys("5")
    time.sleep(1)
    shots.append(s.shot("noter-arabic"))
    s.keys("esc")
    time.sleep(1)
    shots.append(s.shot("noter-timeline"))
    out = s.text()[m:]
    expect("noter: an English and an Arabic entry are written, with moods and #tags", out,
           r"يوم جميل مع العائلة", r"#walk #sea|#sea", r"2 entries")
    m = s.mark()
    s.keys("2")
    s.wait(r"days written in", 10, start=m)
    shots.append(s.shot("noter-calendar"))
    m = s.mark()
    s.keys("4")
    s.wait(r"How you felt", 10, start=m)
    shots.append(s.shot("noter-insights"))
    out = s.text()[m:]
    expect("noter: insights count 2 entries and the streak", out, r"2 entries", r"streak 1 day")
    s.keys("1")
    time.sleep(0.5)
    return shots


def t_noter_reset(s):
    m = s.mark()
    s.keys("esc")
    s.wait(r"Noter - menu", 10, start=m)
    s.keys("end", "up", "ret")
    s.wait(r"Noter - system", 10, start=m)
    s.keys("down", "down", "down", "down", "ret")
    s.wait(r"Type YES to put itelx back", 30, start=m)
    out = s.text()[m:]
    expect("recustomize from Noter's system menu: says what it deletes", out,
           r"itelx is now: Noter 1\.0", r"/home +\d+ file", r"deletes for good")
    s.type("YES\n")
    s.wait(r"Restart now", 60, start=m)
    s.type("no\n")
    s.wait(r"itelx:/> ", 30, start=m)
    out = s.run("ls /home") + s.run("ls /system")
    expect("after recustomize: the terminal is back, /home is gone, /system is the original", out,
           r"/home: no such file", r"terminal\.xvr", absent=[r"recustomize\.xvr", r"customizer\.txt"])


def install_from_terminal(s, stick, name, title):
    """plugs the stick in, runs /media/CZ/<name>.xvr, answers yes twice"""
    s.plug(stick)
    s.wait(r"media: .* is /media/CZ", 20, start=0)
    time.sleep(1)
    m = s.mark()
    s.type("/media/CZ/%s.xvr\n" % name)
    s.wait(r"Install it\? \(yes/no\)", 30, start=m)
    s.type("yes\n")
    s.wait(r"Start it now\? \(yes/no\)", 60, start=m)
    expect("%s: installs from the terminal" % name, s.text()[m:], r"%s 1\.0 - an itelx customizer app" % re.escape(title),
           r"%s is installed" % re.escape(title))
    s.type("yes\n")


def t_school(s, stick):
    install_from_terminal(s, stick, "school_mode", "School mode")
    shots = []
    m = s.mark()
    s.wait(r"Welcome to School mode", 20, start=m - 300)
    s.type("Sara\n")
    s.wait(r"No subjects yet", 10, start=m)
    for name in ("Math", "Physics", "لغة عربية"):
        m = s.mark()
        s.keys("n")
        s.wait(r"new subject", 10, start=m)
        s.type(name + "\n")
        s.wait(r"Ctrl\+S or F2 save", 10, start=m)
        s.keys("ctrl+s")
        time.sleep(0.6)
    shots.append(s.shot("school-subjects"))
    m = s.mark()
    s.keys("esc")
    s.wait(r"Classes today", 10, start=m)
    # a class on the first day of the week
    m = s.mark()
    s.keys("3")
    s.wait(r"no classes on|Monday", 10, start=m)
    m = s.mark()
    s.keys("n")
    s.wait(r"new class", 10, start=m)
    s.keys("ret")                      # subject: Math
    time.sleep(0.5)
    s.keys("ret")                      # day: as chosen
    s.wait(r"Starts.*: ", 10, start=m)
    s.type("8:00\n")
    s.wait(r"Ends.*: ", 10, start=m)
    s.type("8:45\n")
    s.wait(r"Ctrl\+S or F2 save", 10, start=m)
    s.keys("ctrl+s")
    time.sleep(0.8)
    shots.append(s.shot("school-timetable"))
    expect("school: a class in the timetable", s.text()[m:], r"08:00-08:45")
    # homework due tomorrow
    m = s.mark()
    s.keys("2")
    s.wait(r"no homework yet", 10, start=m)
    s.keys("n")
    s.wait(r"new homework", 10, start=m)
    s.keys("down", "ret")              # subject: Physics
    s.wait(r"What: ", 10, start=m)
    s.type("Exercise 4 page 32\n")
    s.wait(r"Due.*: ", 10, start=m)
    s.type("tomorrow\n")
    s.wait(r"Ctrl\+S or F2 save", 10, start=m)
    s.keys("ctrl+s")
    s.wait(r"Exercise 4 page 32", 10, start=m)
    time.sleep(0.5)
    shots.append(s.shot("school-homework"))
    expect("school: homework with its subject and due day", s.text()[m:], r"tomorrow", r"Physics\s+ǀ Exercise 4 page 32")
    # the stopwatch of Math, 32 seconds, saved
    m = s.mark()
    s.keys("4")
    s.wait(r"Last sessions", 10, start=m)
    s.keys("w")
    s.wait(r"stopwatch", 10, start=m)
    time.sleep(33)
    shots.append(s.shot("school-timer"))
    s.keys("s")
    s.wait(r"saved", 10, start=m)
    time.sleep(0.5)
    expect("school: a stopwatch session of Math is logged", s.text()[m:], r"of Math saved")
    m = s.mark()
    s.keys("1")
    s.wait(r"Classes today", 10, start=m)
    time.sleep(0.5)
    shots.append(s.shot("school-today"))
    expect("school: today shows the homework due tomorrow and the time studied", s.text()[m:],
           r"tomorrow", r"Exercise 4 page 32", r"studied today", r"32s / 3h 00m +ǀ Math", r"0s / 3h 00m +ǀ Physics")
    m = s.mark()
    s.keys("7")
    s.wait(r"The last eight weeks", 10, start=m)
    shots.append(s.shot("school-stats"))
    return shots


def t_work(s, stick):
    install_from_terminal(s, stick, "work_mode", "Work mode")
    shots = []
    m = s.mark()
    s.wait(r"Welcome to Work mode", 20, start=m - 300)
    s.type("Omar\n")
    s.wait(r"Your company", 10, start=m)
    s.type("Nile Soft\n")
    s.wait(r"Currency", 10, start=m)
    s.type("\n")
    s.wait(r"Meetings today", 10, start=m)
    # a client
    m = s.mark()
    s.keys("6")
    s.wait(r"no people yet", 10, start=m)
    s.keys("n")
    s.wait(r"new person", 10, start=m)
    s.type("Mona Adel\n")
    s.wait(r"Ctrl\+S or F2 save", 10, start=m)
    s.keys("ctrl+s")
    s.wait(r"Mona Adel", 10, start=m)
    # a project for her, 250 an hour
    m = s.mark()
    s.keys("3")
    s.wait(r"no projects yet", 10, start=m)
    s.keys("n")
    s.wait(r"new project", 10, start=m)
    s.type("Website\n")
    s.wait(r"Ctrl\+S or F2 save", 10, start=m)
    s.keys("down", "ret")
    s.wait(r"new project: Client", 10, start=m)
    s.keys("down", "ret")
    time.sleep(0.4)
    s.keys("down", "down", "down", "down", "down", "ret")
    s.wait(r"Hourly rate.*: ", 10, start=m)
    s.type("250\n")
    time.sleep(0.4)
    s.keys("ctrl+s")
    s.wait(r"Website", 10, start=m)
    time.sleep(0.5)
    shots.append(s.shot("work-projects"))
    expect("work: a project with its client", s.text()[m:], r"active", r"Mona Adel\s+\S Website")
    # a task in it, moved on the board
    m = s.mark()
    s.keys("2")
    s.wait(r"no tasks yet", 10, start=m)
    s.keys("n")
    s.wait(r"new task", 10, start=m)
    s.type("Design the home page\n")
    s.wait(r"Ctrl\+S or F2 save", 10, start=m)
    s.keys("down", "ret")
    s.wait(r"new task: Project", 10, start=m)
    s.keys("down", "ret")
    time.sleep(0.4)
    s.keys("ctrl+s")
    s.wait(r"Design the home page", 10, start=m)
    m = s.mark()
    s.keys("b")
    s.wait(r"board \(b: list\)", 10, start=m)
    s.keys("shift+dot")
    time.sleep(0.8)
    shots.append(s.shot("work-board"))
    expect("work: the board moves the task to doing", s.text()[m:], r"doing \(1\)")
    s.keys("b")
    # the timer on the task, 35 seconds
    m = s.mark()
    s.keys("s")
    s.wait(r"the timer runs", 10, start=m)
    time.sleep(35)
    s.keys("4")
    s.wait(r"week of", 10, start=m)
    time.sleep(0.5)
    shots.append(s.shot("work-timer"))
    s.keys("x")
    s.wait(r"saved on Design the home page", 10, start=m)
    time.sleep(0.5)
    expect("work: the timer's time is in the timesheet as billable", s.text()[m:], r"billable", r"Website")
    # an invoice from the billable time
    m = s.mark()
    s.keys("esc")
    s.wait(r"Work mode - menu", 10, start=m)
    s.keys("ret")
    s.wait(r"no invoices yet", 10, start=m)
    s.keys("n")
    s.wait(r"for which client", 10, start=m)
    s.keys("ret")
    s.wait(r"From \(a date", 10, start=m)
    s.type("\n")
    s.wait(r"To \(Enter: today\)", 10, start=m)
    s.type("\n")
    s.wait(r"Invoice .* for Mona Adel", 10, start=m)
    s.keys("y")
    s.wait(r"invoice INV-\d{4}-001 made", 10, start=m)
    s.keys("ret")
    s.wait(r"INVOICE INV-", 10, start=m)
    time.sleep(0.4)
    shots.append(s.shot("work-invoice"))
    expect("work: an invoice made from the billable time of the client's project", s.text()[m:], r"To:\s+Mona Adel",
           r"Website\s+0:00\s+250\.00", r"Total")
    s.keys("esc")
    time.sleep(0.3)
    s.keys("esc")
    m = s.mark()
    s.keys("8")
    s.wait(r"BY PROJECT", 10, start=m)
    shots.append(s.shot("work-report"))
    expect("work: the report counts the time and the task", s.text()[m:], r"TIME", r"Website", r"MEETINGS")
    m = s.mark()
    s.keys("1")
    s.wait(r"Meetings today", 10, start=m)
    time.sleep(0.5)
    shots.append(s.shot("work-dashboard"))
    return shots


FS_PROMPT = r"vault:[^\n]*> "


def term_run(s, command, prompt, timeout=60):
    """a command in a customizer's terminal: what it printed until the prompt"""
    m = s.mark()
    s.type(command + "\n")
    s.wait(prompt, timeout, start=m + len(command) + 1)
    time.sleep(0.1)
    text = s.text()[m + len(command) + 1:]
    found = list(re.finditer(prompt, text))
    return text[:found[-1].start()] if found else text


def install_with_kernel(s, stick, name, title):
    """a customizer with a kernel: from the terminal it goes onto the stick
    as update.xvr; plugged in again, the update installs it; restart"""
    s.plug(stick)
    s.wait(r"media: .* is /media/CZ", 20, start=0)
    time.sleep(1)
    m = s.mark()
    s.type("/media/CZ/%s.xvr\n" % name)
    s.wait(r"itelx asks are you need update \?: answer yes", 60, start=m)
    s.wait(VM.PROMPT, 20, start=m)
    out = s.text()[m:]
    expect("%s: from the terminal it puts itself on the stick as update.xvr (the kernel needs an accepted update)" % name, out,
           r"brings its own itelx kernel", r"putting %s on /media/CZ as update\.xvr" % re.escape(title))
    s.unplug()
    m = s.plug(stick)
    s.wait(r"are you need update \? \(yes/no\)", 30, start=m)
    expect("%s: the stick offers it as an itelx update" % name, s.text()[m:], r"holds an itelx update: %s 1\.0 \(customizer\)" % re.escape(title))
    s.type("yes\n")
    s.wait(r"Install it\? \(yes/no\)", 30, start=m)
    s.type("yes\n")
    s.wait(r"Restart now\? \(yes/no\)", 120, start=m)
    out = s.text()[m:]
    expect("%s: installed by the update, the kernel too; the stick is cleaned" % name, out, r"/media/ITELX-ESP/boot/vmlinuz",
           r"%s is installed" % re.escape(title), r"the stick is as it was")
    s.type("no\n")
    time.sleep(1)
    s.unplug()


HTTP_PORT = 18081


def t_file_saver(s, stick, shots):
    install_with_kernel(s, stick, "file_saver", "File saver")
    s.reboot()
    s.wait(FS_PROMPT, 90)
    shots.append(s.shot("fs-start"))
    out = term_run(s, "sys", FS_PROMPT)
    expect("file saver: the new kernel runs (itelx 1.4 net)", out, r"itelx 1\.4 net")
    out = term_run(s, "net dhcp", FS_PROMPT, 30) + term_run(s, "net", FS_PROMPT)
    expect("net: the e1000 card, an address from DHCP", out, r"eth0\s+Intel 82540EM", r"address: 10\.0\.2\.15/24", r"router 10\.0\.2\.2")
    out = term_run(s, "ping 10.0.2.2 3", FS_PROMPT, 30)
    expect("ping: the router answers", out, r"reply from 10\.0\.2\.2", r"3 sent, [1-3] answered")
    out = term_run(s, "fetch http://10.0.2.2:%d/payload.bin /vault/Inbox/payload.bin" % HTTP_PORT, FS_PROMPT, 120)
    out += term_run(s, "sum /vault/Inbox/payload.bin", FS_PROMPT, 60)
    want = open(os.path.join(WORK, "payload.bin.sha256")).read().split()[0]
    expect("fetch: a 3 MiB file over TCP from the PC, the SHA-256 is the PC's", out, r"payload\.bin: 3145728 bytes", want)
    # the vault
    out = term_run(s, "organize", FS_PROMPT)
    expect("organize: the Inbox into folders by kind", out, r"1 file\(s\) of /vault/Inbox put into their folders")
    s.plug(stick)
    s.wait(r"media: .* is /media/CZ", 20, start=s.mark() - 400)
    time.sleep(1)
    out = term_run(s, "store /media/CZ/noter.txt", FS_PROMPT) + term_run(s, "store /media/CZ/noter.txt", FS_PROMPT)
    expect("store: a file from the stick goes to Documents; a second time it is already there", out,
           r"stored 1 file\(s\), .*: /vault/Documents/noter\.txt", r"in the vault already, unchanged")
    m = s.mark()
    s.type("password\n")
    s.wait(r"new password: ", 10, start=m)
    s.type("Secret-7\n")
    s.wait(r"the new password again: ", 10, start=m)
    s.type("Secret-7\n")
    s.wait(FS_PROMPT, 60, start=m + 10)
    expect("password: set, the keys typed are not shown", s.text()[m:], r"the vault password is set", absent=[r"Secret-7"])
    out = term_run(s, "lock /vault/Documents/noter.txt", FS_PROMPT, 60) + term_run(s, "ls /vault/Documents", FS_PROMPT)
    expect("lock: the file is encrypted to noter.txt.locked, the plain one is gone", out, r"locked /vault/Documents/noter\.txt",
           r"noter\.txt\.locked", absent=[r"B  noter\.txt\n"])
    out = term_run(s, "check", FS_PROMPT, 120)
    expect("check: every file is as indexed", out, r"0 changed, 0 missing")
    out = term_run(s, "unlock /vault/Documents/noter.txt.locked", FS_PROMPT, 60) + term_run(s, "sum /vault/Documents/noter.txt", FS_PROMPT)
    want = sh("sha256sum", os.path.join(REL, "noter.txt")).split()[0]
    expect("unlock: the file comes back byte for byte (SHA-256 as on the PC)", out, r"unlocked", want)
    # share: the PC downloads it with HTTP through QEMU's forwarded port
    m = s.mark()
    s.type("share /vault 8080\n")
    s.wait(r"sharing /vault, read only, at http://10\.0\.2\.15:8080/", 20, start=m)
    import urllib.request
    try:
        listing = urllib.request.urlopen("http://127.0.0.1:18080/Documents/", timeout=20).read().decode()
        body = urllib.request.urlopen("http://127.0.0.1:18080/Documents/noter.txt", timeout=20).read()
        import hashlib
        got = hashlib.sha256(body).hexdigest()
    except Exception as e:  # noqa: BLE001
        listing, got = "error %s" % e, ""
    expect("share: a browser on the PC lists the folder and downloads the file (same SHA-256)", listing + "\n" + got,
           r"noter\.txt", want)
    time.sleep(0.5)
    s.keys("ctrl+c")
    s.wait(FS_PROMPT, 10, start=m)
    shots.append(s.shot("fs-share"))
    # the organizer
    m = s.mark()
    s.type("vault\n")
    s.wait(r"Your vault\. New files go to Inbox", 10, start=m)
    time.sleep(0.5)
    shots.append(s.shot("fs-vault"))
    s.keys("esc")
    s.wait(FS_PROMPT, 10, start=m)
    out = term_run(s, "backup", FS_PROMPT, 120)
    expect("backup: the vault to the stick", out, r"backup: \d+ file\(s\) copied")
    return out


# every other card the "net" kernel has a driver for: (QEMU device, what net shows)
NICS = [("e1000-82545em", r"Intel 82545EM"), ("e1000-82544gc", r"Intel 82544GC"), ("e1000e", r"Intel 82574L"),
        ("rtl8139", r"Realtek RTL8139"), ("virtio-net-pci", r"virtio network card")]


def t_nics(s, stick):
    """the installed File saver started again with each card: DHCP, ping and a
    3 MiB download over TCP; then with the first card and the stick again"""
    want = open(os.path.join(WORK, "payload.bin.sha256")).read().split()[0]
    first = s.net
    for dev, model in NICS:
        s.net = first[:-1] + [dev + ",netdev=n0"]
        s.reboot(FS_PROMPT)
        out = term_run(s, "net dhcp", FS_PROMPT, 30) + term_run(s, "net", FS_PROMPT)
        out += term_run(s, "ping 10.0.2.2 3", FS_PROMPT, 30)
        out += term_run(s, "fetch http://10.0.2.2:%d/payload.bin /vault/Inbox/nic.bin" % HTTP_PORT, FS_PROMPT, 120)
        out += term_run(s, "sum /vault/Inbox/nic.bin", FS_PROMPT, 60) + term_run(s, "rm /vault/Inbox/nic.bin", FS_PROMPT)
        expect("net %s: %s, DHCP, ping, 3 MiB over TCP (SHA-256 as on the PC)" % (dev, model), out,
               r"eth0\s+" + model, r"address: 10\.0\.2\.15/24", r"3 sent, [1-3] answered", r"nic\.bin: 3145728 bytes", want)
    s.net = first
    s.reboot(FS_PROMPT)
    m = s.plug(stick)
    s.wait(r"media: .* is /media/CZ", 20, start=m)
    time.sleep(1)


DP_PROMPT = r"data:[^\n]*> "


def resp_call(sock, *args):
    """one command in the Redis protocol; the raw answer"""
    import socket  # noqa: F401
    msg = b"*%d\r\n" % len(args) + b"".join(b"$%d\r\n%s\r\n" % (len(a.encode()), a.encode()) for a in args)
    sock.sendall(msg)
    time.sleep(0.4)
    return sock.recv(65536).decode("utf-8", "replace")


def http(method, url, token, body=None):
    import urllib.request
    req = urllib.request.Request(url, data=body.encode() if body is not None else None, method=method)
    if token:
        req.add_header("Authorization", "Bearer " + token)
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            return r.status, r.read().decode()
    except urllib.error.HTTPError as e:  # noqa: F821
        return e.code, e.read().decode()


def t_data_place(s, stick, shots):
    import json
    import socket
    install_with_kernel(s, stick, "data_place", "data_place")
    s.reboot()
    s.wait(DP_PROMPT, 90)
    time.sleep(0.5)
    img = s.vm.screendump("dp-start")
    shots.append(s.shot("dp-start"))
    black = img.count(0x000000, 0, 40, img.w, img.h - 40)
    blue = img.count(0x0B3A8C, 0, 40, img.w, img.h - 40)
    record("data_place: the screen is black with white text (the data kernel)", black > (img.w * (img.h - 80)) // 2 and blue == 0,
           "black %d, blue %d" % (black, blue))
    out = term_run(s, "sys", DP_PROMPT)
    expect("data_place: the kernel is itelx 1.4 data", out, r"itelx 1\.4 data")
    term_run(s, "net dhcp", DP_PROMPT, 30)
    # SQL
    out = term_run(s, 'sql main "create table city (name text, people int)"', DP_PROMPT)
    out += term_run(s, "sql main \"insert into city values ('Cairo', 10000000), ('Giza', 9000000), ('Aswan', 1500000)\"", DP_PROMPT)
    out += term_run(s, 'sql main "select name, people from city where people > 2000000 order by people desc"', DP_PROMPT)
    expect("sql: SQLite on itelx (create, insert, select)", out, r"3 rows changed", r"Cairo\s+\S\s+10000000", r"2 rows")
    # documents, key-value, vectors
    out = term_run(s, 'doc main insert people {"name":"Ahmed","age":30,"tags":["dev"]}', DP_PROMPT)
    out += term_run(s, 'doc main insert people {"name":"Mona","age":25}', DP_PROMPT)
    out += term_run(s, 'doc main find people {"age":{"$gt":26}}', DP_PROMPT)
    expect("doc: documents found with a filter", out, r'"name":"Ahmed"', r"1 document\(s\)", absent=[r'"name":"Mona".*\n.*1 document'])
    out = term_run(s, "kv main SET greeting hello", DP_PROMPT) + term_run(s, "kv main HSET user:1 name Ahmed", DP_PROMPT)
    out += term_run(s, "kv main GET greeting", DP_PROMPT)
    expect("kv: strings and hashes", out, r'"hello"')
    out = term_run(s, "vec create notes 128 cosine text", DP_PROMPT)
    out += term_run(s, 'vec add-text notes n1 "the kernel of an operating system"', DP_PROMPT)
    out += term_run(s, 'vec add-text notes n2 "a database for documents"', DP_PROMPT)
    out += term_run(s, 'vec search-text notes "operating system kernel" k 1', DP_PROMPT)
    expect("vec: the nearest text", out, r"1\. n1 ")
    out = term_run(s, "dbstat", DP_PROMPT, 60)
    expect("dbstat: every kind of database", out, r"/data/sql/main\.db.*1 table", r"/data/doc/main\.db.*1 collection",
           r"/data/kv/main\.aof.*2 key", r"/data/vec/notes\.vec.*2 vector")
    out = term_run(s, "dbscan /data", DP_PROMPT, 60)
    expect("dbscan: the files of /data recognized by what is in them", out, r"SQLite 3 database", r"data_place vector collection",
           r"kv database \(Redis AOF", r"a doc database")
    shots.append(s.shot("dp-tools"))
    # kv in the Redis protocol, from the PC
    m = s.mark()
    s.type("kv serve main 6379 s3cret &\n")
    s.wait(r"kv main on 10\.0\.2\.15 port 6379", 20, start=m)
    time.sleep(0.5)
    try:
        c = socket.create_connection(("127.0.0.1", 16379), timeout=10)
        a = resp_call(c, "GET", "greeting")
        a += resp_call(c, "AUTH", "s3cret") + resp_call(c, "SET", "fromPC", "42") + resp_call(c, "INCR", "fromPC")
        a += resp_call(c, "GET", "greeting")
        c.close()
    except OSError as e:
        a = "error %s" % e
    expect("kv serve: a Redis client on the PC (password first, then SET, INCR, GET)", a, r"-NOAUTH", r"\+OK", r":43", r"\$5\r\nhello")
    # the HTTP API, from the PC
    m = s.mark()
    s.type("dbserve 8086 tok123 &\n")
    s.wait(r"data_place API on http://10\.0\.2\.15:8086/", 20, start=m)
    time.sleep(0.5)
    base = "http://127.0.0.1:18086"
    st0, _ = http("GET", base + "/", None)
    st1, b1 = http("POST", base + "/sql/main", "tok123", "SELECT name FROM city ORDER BY people DESC LIMIT 1")
    st2, b2 = http("POST", base + "/doc/main/people/find", "tok123", '{"filter": {"name": "Mona"}}')
    st3, b3 = http("POST", base + "/vec/notes/search", "tok123", '{"text": "documents database", "k": 1}')
    st4, b4 = http("POST", base + "/kv/main", "tok123", '["GET", "fromPC"]')
    ok = st0 == 401 and st1 == 200 and json.loads(b1)["rows"] == [["Cairo"]] and st2 == 200 and json.loads(b2)["count"] == 1 \
        and st3 == 200 and json.loads(b3)["results"][0]["id"] == "n2" and st4 == 200 and json.loads(b4)["result"] == "43"
    record("dbserve: the HTTP API (no token refused; SQL, documents, vectors, key-value as JSON)", ok,
           "%s\n%s %s\n%s %s\n%s %s\n%s %s" % (st0, st1, b1, st2, b2, st3, b3, st4, b4))
    s.plug(stick)
    s.wait(r"media: .* is /media/CZ", 20, start=s.mark() - 400)
    time.sleep(1)
    out = term_run(s, "dbbackup", DP_PROMPT, 120)
    expect("dbbackup: /data to the stick, checked", out, r"backup: \d+ file\(s\).*checked by SHA-256")
    return out


def reset_with_kernel(s, stick, prompt):
    """recustomize from a customizer's terminal: the kernel goes back by the stick"""
    m = s.mark()
    s.type("recustomize\n")
    s.wait(r"itelx asks are you need update \?: answer yes", 60, start=m)
    s.wait(prompt, 20, start=m)
    expect("recustomize: the kernel is not the original, so it goes by the stick", s.text()[m:],
           r"The kernel is not the original one", r"putting recustomize on /media/CZ as update\.xvr")
    s.unplug()
    m = s.plug(stick)
    s.wait(r"are you need update \? \(yes/no\)", 30, start=m)
    s.type("yes\n")
    s.wait(r"Type YES to put itelx back", 60, start=m)
    s.type("YES\n")
    s.wait(r"Restart now", 120, start=m)
    expect("recustomize: the original kernel and files are back, the rest is deleted", s.text()[m:],
           r"put back /media/ITELX-ESP/boot/vmlinuz", r"itelx is as it was installed")
    s.type("no\n")
    time.sleep(1)
    s.unplug()
    s.reboot()
    s.wait(r"itelx:/> ", 90)
    out = s.run("sys") + s.run("ls /")
    expect("after recustomize: itelx 1.4 as installed (its kernel, no vault or data)", out, r"itelx 1\.4\b(?! net| data)",
           absent=[r"itelx 1\.4 (net|data)", r"vault/", r"data/", r"home/"])


def reset_from_menu(s, app):
    """Esc, System (second to last), Restore the system, YES"""
    m = s.mark()
    s.keys("esc")
    s.wait(r"%s - menu" % app, 10, start=m)
    s.keys("end", "up", "ret")
    s.wait(r"%s - system" % app, 10, start=m)
    s.keys("down", "down", "down", "down", "ret")
    s.wait(r"Type YES to put itelx back", 30, start=m)
    s.type("YES\n")
    s.wait(r"Restart now", 60, start=m)
    s.type("no\n")
    s.wait(r"itelx:/> ", 30, start=m)
    out = s.run("ls /home") + s.run("ls /dex")
    expect("%s: recustomize puts itelx back (no /home, /dex as installed)" % app, out, r"/home: no such file", r"hello\.xvr",
           absent=[r"calc\.xvc"])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", required=True)
    ap.add_argument("--fw", default="uefi")
    ap.add_argument("--only", default="")
    ap.add_argument("--shots", default="")
    a = ap.parse_args()
    os.makedirs(WORK, exist_ok=True)
    stick = os.path.join(WORK, "cz-stick.img")
    make_stick(stick, [(os.path.join(REL, n), n) for n in sorted(os.listdir(REL)) if n.endswith((".xvr", ".txt"))])
    only = set(a.only.split(",")) if a.only else None
    shots = []
    if not only or "noter" in only:
        s = Session(a.image, a.fw, "noter")
        try:
            s.wait(r"itelx:/> ", 90)
            t_noter_install(s, stick)
            shots += t_noter_use(s)
            t_noter_reset(s)
        finally:
            s.stop()
    if not only or "school" in only:
        s = Session(a.image, a.fw, "school")
        try:
            s.wait(r"itelx:/> ", 90)
            shots += t_school(s, stick)
            reset_from_menu(s, "School mode")
        finally:
            s.stop()
    if not only or "work" in only:
        s = Session(a.image, a.fw, "work")
        try:
            s.wait(r"itelx:/> ", 90)
            shots += t_work(s, stick)
            reset_from_menu(s, "Work mode")
        finally:
            s.stop()
    if not only or "file_saver" in only:
        # a file for fetch, served by the PC (QEMU's user network reaches it as 10.0.2.2)
        payload = os.path.join(WORK, "www", "payload.bin")
        os.makedirs(os.path.dirname(payload), exist_ok=True)
        with open(payload, "wb") as f:
            f.write(os.urandom(3 << 20))
        sh("sh", "-c", "cd %s && sha256sum payload.bin > ../payload.bin.sha256" % os.path.dirname(payload))
        http = subprocess.Popen([sys.executable, "-m", "http.server", str(HTTP_PORT), "--bind", "127.0.0.1"],
                                cwd=os.path.dirname(payload), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        net = ["-netdev", "user,id=n0,hostfwd=tcp:127.0.0.1:18080-:8080", "-device", "e1000,netdev=n0"]
        s = Session(a.image, a.fw, "file_saver", net=net)
        try:
            s.wait(r"itelx:/> ", 90)
            t_file_saver(s, stick, shots)
            t_nics(s, stick)
            reset_with_kernel(s, stick, FS_PROMPT)
        finally:
            s.stop()
            http.terminate()
    if not only or "data_place" in only:
        net = ["-netdev", "user,id=n0,hostfwd=tcp:127.0.0.1:16379-:6379,hostfwd=tcp:127.0.0.1:18086-:8086",
               "-device", "e1000,netdev=n0"]
        s = Session(a.image, a.fw, "data_place", net=net)
        try:
            s.wait(r"itelx:/> ", 90)
            t_data_place(s, stick, shots)
            reset_with_kernel(s, stick, DP_PROMPT)
        finally:
            s.stop()
    if a.shots:
        os.makedirs(a.shots, exist_ok=True)
        for p in shots:
            shutil.copy(p, a.shots)
    bad = [n for n, ok in results if not ok]
    print("%d passed, %d failed" % (len(results) - len(bad), len(bad)))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
