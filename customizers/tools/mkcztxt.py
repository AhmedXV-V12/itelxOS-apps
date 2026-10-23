#!/usr/bin/env python3
"""The .txt version of a customizer app's installer: everything the .xvr is
and does, as text that can be read on any computer.

    mkcztxt.py PAYLOAD.bin INSTALLER.xvr > NAME.txt

It holds what the customizer is (about.txt), how to install it, every file
it puts on itelx with its size and SHA-256, and the complete source code of
the installer program. It is not used to install anything: the .xvr is.
"""
import hashlib
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mkczpayload  # noqa: E402

CZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WHERE = {1: "system disk", 2: "boot partition", 4: "folder", 8: "for the installer"}


def main():
    pay = mkczpayload.read(sys.argv[1])
    xvr = open(sys.argv[2], "rb").read()
    name = os.path.basename(sys.argv[2])
    reset = pay["id"] == "recustomize"
    out = []
    w = out.append
    title = "recustomize" if reset else pay["title"]
    w("%s %s - %s" % (title, pay["version"], "puts itelx back as it was installed" if reset else "an itelx customizer app"))
    w("=" * 78)
    w("")
    w("This is the text version of %s: what it is, what it installs and the" % name)
    w("source code of its installer. Installing is done with %s itself;" % name)
    w("this file is for reading. Licence: MPL-2.0 (customizers/LICENSE in the")
    w("itelxOS-apps repository); the itelx files inside are under their own licences.")
    w("")
    w("%s: %d bytes, SHA-256 %s" % (name, len(xvr), hashlib.sha256(xvr).hexdigest()))
    w("")
    for e in pay["entries"]:
        if e[1] & 8:
            w("-" * 78)
            w(e[0].lstrip("/"))
            w("-" * 78)
            w(e[2].decode("utf-8").rstrip())
            w("")
    w("-" * 78)
    w("How to install")
    w("-" * 78)
    cmd = name[:-4]
    w("On itelx 1.4, from a USB stick (FAT32) with %s on it:" % name)
    w("    install /media/<STICK>/%s      then    %s" % (name, cmd))
    w("or run it straight from the stick:   /media/<STICK>/%s" % name)
    w("or copy it to the top of the stick as update.xvr and plug the stick in:")
    w("itelx asks \"are you need update ?\"; answer yes.")
    if any(e[1] & 2 for e in pay["entries"]):
        w("It brings its own kernel: run from the terminal, it puts itself on a USB")
        w("stick as update.xvr and asks you to plug the stick in again, because")
        w("itelx 1.4 lets only an update you accept write the kernel.")
    w("")
    w("-" * 78)
    w("What it puts on itelx (%d entries)" % len(pay["entries"]))
    w("-" * 78)
    for path, mask, data in pay["entries"]:
        if mask & 8:
            continue
        where = WHERE.get(mask, "?")
        if mask & 4:
            w("  %-40s %s" % (path + "/", where))
        else:
            w("  %-40s %-15s %8d bytes  sha256 %s" % (path, where, len(data), hashlib.sha256(data).hexdigest()))
    w("")
    w("-" * 78)
    w("The installer's source code (C, built with the itelxOS SDK)")
    w("-" * 78)
    for src in ("common/cz.h", "common/cz.c", "common/recustomize.c" if reset else "common/cz_install.c"):
        w("")
        w("==> customizers/%s <==" % src)
        w(open(os.path.join(CZ, src), encoding="utf-8").read().rstrip())
    sys.stdout.write("\n".join(out) + "\n")


if __name__ == "__main__":
    main()
