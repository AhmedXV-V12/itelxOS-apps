# Customizer apps

One `.xvr` file that turns itelxOS 1.4 into a system for one job, and
`recustomize`, which puts it back as it was installed. How to use them:
[docs/en/6-customizers.md](../docs/en/6-customizers.md)
([Arabic](../docs/ar/6-customizers.md)).

| | itelx becomes | instead of the terminal | kernel |
|---|---|---|---|
| [noter](noter) | a journal | Noter | itelx 1.4 |
| [school_mode](school_mode) | a student's desk | School mode | itelx 1.4 |
| [work_mode](work_mode) | a workplace | Work mode | itelx 1.4 |
| [file_saver](file_saver) | a vault for files | its terminal, `vault:/>` | itelx 1.4 net |
| [data_place](data_place) | SQL, document, key-value and vector databases | its terminal, `data:/>` | itelx 1.4 data |
| recustomize | itelx as installed | the itelx terminal | the original |

[`release/`](release) has them built. Each comes as `NAME.xvr`, which
installs it, and `NAME.txt`, which is for reading: what it does, every file
it installs with its SHA-256, and the source of its installer. `SHA256SUMS`
lists them all.

## How one is made

A customizer is the installer (`common/cz_install.c`) with a payload
linked into it (`common/payload.S`). The payload holds every file it puts
on itelx. `NAME/payload.txt` lists them:

```
info:/about.txt=customizers/noter/about.txt      shown by the installer
dir:/home/noter                                  a folder on the system disk
itfs:/system/terminal.xvr={B}/cz/apps/noter.xvr  a file on the system disk
boot:/boot/vmlinuz=customizers/kernel/out/net/vmlinuz   a file on the boot partition
```

`{B}` is the build folder. `tools/mkczpayload.py` packs the files:
a header, then one entry per file with its path, size and CRC-32. The
installer:

1. checks every file;
2. refuses to run on anything but itelx 1.4, or while another customizer
   is installed;
3. writes each file as `NAME.new`, reads it back, compares it, and renames
   it;
4. records what it installed in `/system/customizer.txt`.

A payload with a `boot:` file needs the kernel written. itelx 1.4 allows
that only in an update the user accepted from a USB stick, so the
installer copies itself to a stick as `update.xvr` and goes on from there
(`cz_to_stick`).

`recustomize` (`common/recustomize.c`) carries the files of a newly
installed itelx 1.4 (`factory/itelx-1.4`, made by `tools/get-factory.sh`
and `tools/mkfactory.py` from the 1.4 installer). It puts back every system
file that differs and the original kernel. It then deletes everything the
original system does not have, except `/boot/cmdline`, `/system/hostname`
and `/system/owner`.

## Layout

```
noter/ school_mode/ work_mode/ file_saver/ data_place/
                 one customizer: payload.txt, about.txt, after.txt, apps/<program>/
                 (an apps folder may have "uses": the shared parts it is built with,
                 and xvrlink.opt: options such as a bigger heap)
common/          the installer, recustomize, and the parts programs share:
  term/          the itelx 1.4 terminal with hooks, for terminals that replace it
  net/           sockets, DNS, HTTP, and the net, ping, dns, fetch commands
  vault/         File saver's vault: index, versions, trash, ChaCha20 locking
  sqlite/        SQLite on itelx: its file system (VFS), memory, math
  db/ doc/ vec/  data_place's engines: kv and JSON, documents, vectors
kit/             full-screen programs on the itelx terminal: frames, lists,
                 forms, an editor, dates, tables in text files, SHA-256, ChaCha20,
                 the system menu (restart, power off, restore), passcodes
kernel/          the network for the itelx 1.4 kernel: src/ (stack and drivers),
                 itelx-1.4-net.patch, build-kernel.sh, out/ (the built kernels)
factory/         itelx 1.4 as installed, for recustomize
third_party/     SQLite 3.54.1 (public domain)
tools/           mkczpayload.py, mkcztxt.py (the .txt version), mkfactory.py,
                 get-factory.sh
release/         the built customizers
```

## Build

```bash
make customizers
```

This needs Linux or WSL with `gcc`, `ld`, `make` and `python3`, as for the
rest of this repository, and builds into `build/cz`. The kernels are
already built in `kernel/out`. To build them again from the itelx 1.4
source, run `bash customizers/kernel/build-kernel.sh /path/to/itelxOS64bit`
and then `make customizers`.

## Test

```bash
python3 tests/e2e_customizers.py --image /path/to/itelxOS.img [--only noter,school,work,file_saver,data_place]
```

The test starts itelxOS 1.4 in QEMU and plugs in a FAT32 stick made from
`release/`. For each customizer it:

1. installs it, by the stick for the kernel ones;
2. uses it with keys typed on the keyboard;
3. runs recustomize and compares the system disk with a new installation.

File saver is also started with every network card the kernel has a driver
for: e1000 82540EM, 82545EM and 82544GC, e1000e 82574L, RTL8139 and
virtio-net. With each card it gets an address from DHCP, pings, and
downloads 3 MiB from the PC, and the SHA-256 is checked.

## Licences

See [LICENSE](LICENSE). The apps, the kit and the installers are under the
MPL 2.0. The kernel parts are under the itelx Kernel License 1.0, and
SQLite is public domain.
