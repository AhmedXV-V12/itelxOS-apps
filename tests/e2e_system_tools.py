#!/usr/bin/env python3
"""End-to-end test of the system tools (system-tools/: formater,
recovery_tester) on a real itelxOS 2.0 in QEMU.

What a user does: a FAT32 USB stick with system-tools/ on it is plugged
into itelx, the tool is started from the terminal, y or n is typed, the
computer restarts, the red recovery screen comes up, "Use backup" is
chosen there, and the tool is run again on the repaired system.

Runs inside Linux/WSL as root (KVM), with qemu-system-x86_64, OVMF,
dosfstools and mtools:

    python3 tests/e2e_system_tools.py --image /path/to/itelxOS.img [--fw uefi,bios] [--only formater,...]

itelxOS.img is the system disk image of itelxOS 2.0 (out/itelxOS.img of its
build). Every restart is a new QEMU run on the same disk file (-no-reboot).
ITELX_TEST_WORK sets the work folder. Exit status: the number of failures.
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
TOOLS = os.path.join(ROOT, "system-tools")
PROMPT = r"itelx:[^\n]*> "
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


def make_stick(path):
    if os.path.exists(path):
        os.remove(path)
    with open(path, "wb") as f:
        f.truncate(64 << 20)
    sh("mkfs.vfat", "-F", "32", "-n", "ST", path)
    for n in ("formater.xvr", "recovery_tester.xvr"):
        sh("mcopy", "-i", path, os.path.join(TOOLS, n), "::/" + n)


def red_share(shot):
    reds = total = 0
    for y in range(0, shot.h, 8):
        for x in range(0, shot.w, 8):
            c = shot.at(x, y)
            r, g, b = c >> 16, (c >> 8) & 255, c & 255
            total += 1
            if r >= 0x80 and g < 0x40 and b < 0x40:
                reds += 1
    return reds / max(total, 1)


class Session:
    """one disk, started again and again: each start a new QEMU run"""

    def __init__(self, image, fw, name, stick):
        self.fw = fw
        self.name = name
        self.stick = stick
        self.starts = 0
        os.makedirs(WORK, exist_ok=True)
        self.disk = os.path.join(WORK, "st-%s-%s.img" % (name, fw))
        shutil.copyfile(image, self.disk)
        self.vm = None

    def start(self):
        if self.vm:
            self.vm.stop()
        self.starts += 1
        self.n = 0
        self.vm = VM("st-%s-%s-%d" % (self.name, self.fw, self.starts), self.disk, firmware=self.fw, machine="q35",
                     mem=768, video_id="video0", extra=["-device", "qemu-xhci,id=xhci,p2=8,p3=8"]).start()
        return self

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

    def exited(self, timeout=60):
        end = time.time() + timeout
        while time.time() < end and self.vm.proc.poll() is None:
            time.sleep(0.2)
        ok = self.vm.proc.poll() is not None
        self.vm.stop()
        return ok

    def booted(self, timeout=120):
        self.wait(PROMPT, timeout)
        self.wait(r"bootstat: start completed", 30)
        time.sleep(0.5)

    def plug(self):
        self.n += 1
        self.vm.hmp("drive_add 0 id=st%d,if=none,file=%s,format=raw" % (self.n, self.stick))
        self.vm.cmd("device_add", driver="usb-storage", id="stdev%d" % self.n, drive="st%d" % self.n,
                    bus="xhci.0", port="1")
        self.wait(r"is /media/ST", 30)
        time.sleep(1)

    def type(self, text, settle=0.25):
        time.sleep(settle)
        self.vm.type(text)

    def ask(self, command, pattern=r"\(y/n\) ", timeout=60):
        """types a command and waits for its question; what it printed"""
        m = self.mark()
        self.type(command + "\n")
        self.wait(pattern, timeout, start=m)
        return self.text()[m:]

    def answer(self, text, pattern=PROMPT, timeout=120):
        m = self.mark()
        self.type(text + "\n")
        if pattern:
            self.wait(pattern, timeout, start=m + len(text) + 1)
        return self.text()[m:]

    def run(self, command, timeout=60):
        return plain(self.vm.run(command, timeout))

    def red_screen(self, reason_rx, label):
        m = self.wait(r"recovery screen: ([^\r\n]*)", 120)
        time.sleep(1.5)
        share = red_share(self.vm.screendump(label))
        expect("%s: %s: the red recovery screen (%s, %.0f%% red)" % (self.fw, label, m.group(1)[:60], share * 100),
               m.group(1) + (" red" if share > 0.5 else " not red"), reason_rx, r" red$")

    def use_backup(self):
        """Use backup on the red screen; the next start is the backup system"""
        self.vm.key("ret")
        self.wait(r"Press Enter to restart", 300)
        out = self.text()
        self.vm.key("ret")
        self.exited(30)
        return out

    def stop(self):
        if self.vm:
            self.vm.stop()


def cannot_run(s, label):
    """a start of a system that cannot run: it marks itself failed and restarts"""
    s.start()
    s.wait(r"itelx cannot run: ([^\r\n]*)", 120)
    record("%s: %s: the system marks itself failed and restarts" % (s.fw, label), s.exited(60))


# ------------------------------------------------------------ the tests

def t_recovery_tester(image, fw, stick):
    s = Session(image, fw, "rt", stick)
    s.start()
    s.booted()
    s.plug()

    # no: nothing changes
    out = s.ask("/media/ST/recovery_tester.xvr")
    expect("%s: recovery_tester explains the test and asks first" % fw, out,
           r"a test of the red recovery screen", r"/users/recovery_tester/xvinit\.xvr\.good",
           r"report-ar\.txt and report-en\.txt", r"Start the test\? \(y/n\)")
    out = s.answer("n") + s.run("ls /users")
    expect("%s: recovery_tester, n: nothing was changed" % fw, out, r"Nothing was changed",
           absent=[r"recovery_tester"])

    # yes, then cancel before restarting
    s.ask("/media/ST/recovery_tester.xvr")
    out = s.answer("y", r"Restart now\? \(y/n\) ")
    expect("%s: recovery_tester, y: the good copy in /users, the fault in place" % fw, out,
           r"good copy kept: /users/recovery_tester/xvinit\.xvr\.good \(CRC-32 [0-9a-f]{8}\)",
           r"/system/xvinit\.xvr damaged at byte \d+: payload checksum mismatch", r"The fault is in place")
    s.answer("n")
    out = s.ask("/media/ST/recovery_tester.xvr", r"Restart now\? \(y/n\) ")
    expect("%s: recovery_tester again before a restart: it waits for the restart" % fw, out,
           r"waits for a restart", r"/system/xvinit\.xvr is damaged")
    s.answer("n")
    out = s.run("/media/ST/recovery_tester.xvr cancel")
    left = s.run("ls /users")
    expect("%s: recovery_tester cancel: the good copy is back, nothing is left" % fw, out + "\n--- ls /users\n" + left,
           r"the test is undone: the good copy is back in /system/xvinit\.xvr", r"--- ls /users\n\(empty\)")

    # the whole test: restart, red screen, Use backup, check
    s.ask("/media/ST/recovery_tester.xvr")
    s.answer("y", r"Restart now\? \(y/n\) ")
    s.type("y\n")
    record("%s: recovery_tester restarts the computer" % fw, s.exited(60))
    cannot_run(s, "damaged xvinit")
    s.start()
    s.red_screen(r"service manager", "rt-red")
    out = s.use_backup()
    expect("%s: Use backup keeps /users and puts the backup system on the disk" % fw, out,
           r"your files are in /users/recovered-", r"Done: the backup system is on the disk")
    s.start()
    s.booted()
    s.plug()
    out = s.run("/media/ST/recovery_tester.xvr", 90)
    expect("%s: recovery_tester after the repair: PASSED, the good copy is back" % fw, out,
           r"/system/xvinit\.xvr was repaired \(valid", r"by Use backup", r"files kept by Use backup: /users/recovered-",
           r"the good copy is back in /system/xvinit\.xvr", r"recovery_tester: PASSED",
           r"update\.xvr brings the full itelxOS back")
    out = s.run("ls /users")
    expect("%s: after PASSED nothing of the test is left in /users" % fw, out, r"recovered-",
           absent=[r"recovery_tester"])

    # a repair that fails: the good copy is lost during the test
    s.ask("/media/ST/recovery_tester.xvr")
    s.answer("y", r"Restart now\? \(y/n\) ")
    s.answer("n")
    s.run("del /users/recovery_tester/xvinit.xvr.good")
    s.type("restart\n")
    s.exited(60)
    cannot_run(s, "damaged xvinit, again")
    s.start()
    s.red_screen(r"service manager", "rt-red-2")
    s.use_backup()
    s.start()
    s.booted()
    s.plug()
    out = s.run("/media/ST/recovery_tester.xvr", 90)
    expect("%s: recovery_tester: FAILED, the reasons, two report files" % fw, out,
           r"recovery_tester: FAILED", r"the good copy in /users/recovery_tester is missing or damaged",
           r"/users/recovery_tester/report-en\.txt: written", r"/users/recovery_tester/report-ar\.txt: written")
    out = s.run("cat /users/recovery_tester/report-en.txt", 60)
    expect("%s: report-en.txt: why, what was found, the logs" % fw, out,
           r"result: the repair FAILED", r"the good copy \(/users/recovery_tester/xvinit\.xvr\.good\): missing or damaged",
           r"the backup system on the disk: yes", r"boot status: the last start completed", r"---- state\.txt",
           r"target=/system/xvinit\.xvr", r"---- kernel log", r"bootstat: start completed")
    out = s.run("cat /users/recovery_tester/report-ar.txt", 60)
    expect("%s: report-ar.txt: the same in Arabic, with the logs" % fw, out,
           r"تقرير recovery_tester", r"الإصلاح فشل", r"النسخة السليمة .* مفقودة أو تالفة", r"السجلات",
           r"target=/system/xvinit\.xvr", r"سجل النواة")
    s.stop()


def t_formater(image, fw, stick):
    s = Session(image, fw, "fmt", stick)
    s.start()
    s.booted()
    s.run("add -f /notes.txt my notes")
    s.run("add -r /projects")
    s.run("add -f /projects/plan.txt a plan")
    s.plug()

    out = s.ask("/media/ST/formater.xvr")
    expect("%s: formater lists what goes and what stays, and asks first" % fw, out,
           r"this wipes the system disk", r"/system\n", r"/dex\n", r"/users\n", r"/notes\.txt\n", r"/projects\n",
           r"Kept: /kernel  /boot  /media \(USB sticks\)  /ram \(memory\)",
           r"The backup system is on the boot partition", r"Wipe the system disk\? \(y/n\)",
           absent=[r"  /kernel\n", r"  /boot\n", r"  /media\n", r"  /ram\n"])
    out = s.answer("n") + s.run("ls /")
    expect("%s: formater, n: nothing was changed" % fw, out, r"Nothing was changed", r"system", r"notes\.txt")

    s.ask("/media/ST/formater.xvr")
    out = s.answer("y", r"Restart now\? \(y/n\) ", 180)
    expect("%s: formater, y: everything but /kernel, /boot, /media, /ram goes" % fw, out,
           r"/system removed", r"/dex removed", r"/users removed", r"/projects removed",
           r"formater: done, \d+ entries removed")
    out = s.answer("n") + s.run("ls /") + s.run("ls /media/ST")
    expect("%s: after formater: /kernel and /boot stay, the stick is untouched" % fw, out,
           r"kernel", r"boot", r"media", r"formater\.xvr", r"recovery_tester\.xvr",
           absent=[r"system", r"notes\.txt", r"projects"])
    s.type("restart\n")
    s.exited(60)
    cannot_run(s, "after formater")
    s.start()
    s.red_screen(r"service manager", "fmt-red")
    out = s.use_backup()
    expect("%s: after formater, Use backup puts the backup system back" % fw, out,
           r"Done: the backup system is on the disk")
    s.start()
    s.booted()
    out = s.run("ls /") + s.run("ls /system")
    expect("%s: after formater and Use backup: the backup system starts" % fw, out,
           r"kernel", r"system", r"users", r"terminal\.xvr", absent=[r"notes\.txt", r"projects"])
    s.stop()


def t_recovery_tester_manual(image, fw, stick):
    """the repair by hand: Use manual, disk.open (/media/broken-os), cancel"""
    s = Session(image, fw, "rtm", stick)
    s.start()
    s.booted()
    s.plug()
    s.ask("/media/ST/recovery_tester.xvr")
    s.answer("y", r"Restart now\? \(y/n\) ")
    s.type("y\n")
    s.exited(60)
    cannot_run(s, "manual: damaged xvinit")
    s.start()
    s.red_screen(r"service manager", "rtm-red")
    s.vm.key("right")
    time.sleep(0.3)
    s.vm.key("ret")
    s.wait(PROMPT, 120)
    s.plug()
    out = s.run("/media/ST/recovery_tester.xvr")
    expect("%s: Use manual before disk.open: recovery_tester asks for it" % fw, out, r"Open the system disk\s+first \(disk\.open\)")
    s.run("disk.open")
    out = s.run("/media/ST/recovery_tester.xvr")
    expect("%s: Use manual: recovery_tester finds the test on /media/broken-os" % fw, out,
           r"the recovery screen's temporary terminal", r"/system/xvinit\.xvr is payload checksum mismatch")
    out = s.run("/media/ST/recovery_tester.xvr cancel")
    expect("%s: Use manual: recovery_tester cancel puts the good copy back by hand" % fw, out,
           r"the test is undone: the good copy is back in /media/broken-os/system/xvinit\.xvr")
    s.run("disk.close")
    s.type("restart\n")
    s.exited(60)
    s.start()
    s.booted()
    out = s.run("ls /system") + s.run("ls /users")
    expect("%s: after the repair by hand: the full itelxOS starts again, nothing left" % fw, out,
           r"top\.xvr", r"web\.xvr", r"\(empty\)", absent=[r"recovery_tester", r"recovered-"])
    s.stop()


TESTS = {"recovery_tester": t_recovery_tester, "recovery_tester_manual": t_recovery_tester_manual, "formater": t_formater}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", required=True)
    ap.add_argument("--fw", default="uefi,bios")
    ap.add_argument("--only", default="")
    a = ap.parse_args()
    only = [x for x in a.only.split(",") if x]
    os.makedirs(WORK, exist_ok=True)
    stick = os.path.join(WORK, "st-stick.img")
    make_stick(stick)
    for fw in a.fw.split(","):
        for name, fn in TESTS.items():
            if only and name not in only:
                continue
            try:
                fn(a.image, fw, stick)
            except Exception as e:
                record("%s: %s" % (fw, name), False, "%s: %s" % (type(e).__name__, e))
    bad = sum(1 for r in results if not r[1])
    print("\n%d tests, %d failed" % (len(results), bad))
    sys.exit(bad)


if __name__ == "__main__":
    main()
