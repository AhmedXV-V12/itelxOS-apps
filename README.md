# itelxOS apps

Apps for **itelxOS**, the 64-bit operating system with its own kernel: simple
apps written in the **XVC** language and complex apps built as **XVR**
executables, a converter from XVC to XVR, the SDK to write your own,
**customizer apps** that turn itelxOS 1.4 into a system for one job, and a
guide in Arabic and English.

<p align="center"><img src="screenshots/welcome.png" alt="The welcome app on itelxOS 1.3" width="760"></p>

## Install them in one minute

1. Format a USB stick as **FAT32** (exFAT and NTFS are not read by itelx 1.3).
2. Copy **what is inside** [`usb/`](usb) to the top of the stick:
   `update.xvr` and the folder `dex/`.
3. Plug the stick into itelx and answer `yes` to `are you need update ?`.
4. Type `welcome`.

<p align="center"><img src="screenshots/usb-install.png" alt="Installing from a USB stick" width="760"></p>

Details, also for Windows, Linux and macOS: [docs/en/5-usb-fat32.md](docs/en/5-usb-fat32.md).

## The apps

**Simple apps, in XVC** ([`apps/xvc`](apps/xvc)): read them, change them, learn from them.

| app | what it does |
|---|---|
| `welcome` | all the apps and how to start them |
| `calc` | a calculator with exact fractions: `1/3 + 1/6` is `1/2` |
| `convert` | units: length, mass, volume, time, data, speed, temperature |
| `base` | a number in decimal, hex, binary, octal (and as a character) |
| `primes` | primes up to n, factors, prime test |
| `cal` | today, the time, a month or a whole year |
| `timer` | countdown and stopwatch with laps |
| `todo` | a to-do list |
| `notes` | dated notes, with search |
| `contacts` | a phone book (Arabic names too) |
| `wc` | lines, words, characters and bytes of files |
| `search` | find a text in the files of a folder |
| `tree` | a folder as a tree, with sizes |
| `cipher` | Caesar (English and Arabic letters), ROT13, Vigenère |
| `typing` | typing speed and accuracy |
| `table` | multiplication tables and practice |
| `quiz` | multiple-choice questions |
| `hangman` | guess the word |
| `ttt` | tic-tac-toe against a computer that never loses |

**Complex apps, XVR executables written in C** ([`apps/xvr`](apps/xvr)):

| app | what it does |
|---|---|
| `fm` | a file manager: arrows, open, copy and move between disks and USB sticks, delete, install |
| `edit` | a text editor (lines, insert, replace, find, substitute, undo) |
| `sysmon` | memory, processes and their CPU use, refreshed every second |
| `sum` | SHA-256 and CRC-32 of files; checks `SHA256SUMS` lists |
| `2048` | the sliding tiles game, with undo and a best score |
| `sokoban` | 11 levels, each checked with a solver; undo, progress kept |
| `mines` | minesweeper, 9×9 to 30×20 |
| `life` | Conway's Game of Life, with patterns |
| `xvc2xvr` | makes an XVR executable from an XVC program, on itelx itself |

<p align="center">
<img src="screenshots/fm.png" alt="fm" width="380"> <img src="screenshots/2048.png" alt="2048" width="380"><br>
<img src="screenshots/sokoban.png" alt="sokoban" width="380"> <img src="screenshots/sysmon.png" alt="sysmon" width="380"><br>
<img src="screenshots/calc.png" alt="calc" width="380"> <img src="screenshots/life.png" alt="life" width="380">
</p>

## Customizer apps

One `.xvr` file that turns **itelxOS 1.4** into a system for one job: it
installs its own program in place of the terminal, the tools that go with
it, and for two of them an itelx kernel with Ethernet drivers.
**recustomize** puts itelx back as it was installed. Each one is open
source and comes as `NAME.xvr`, which installs it, and `NAME.txt`, which
lists what it installs and holds its installer's source.

| customizer | itelx becomes |
|---|---|
| `noter` | a journal: timeline, calendar, on this day, insights, moods, tags, templates, passcode |
| `school_mode` | a student's desk: classes, homework, exams, a diary, grades, a study timer for every subject |
| `work_mode` | a workplace: tasks on a board, projects, a timer and timesheet, meetings, clients, invoices, reports |
| `file_saver` | a vault for files of any kind: sorted by kind, SHA-256 checked, versions, trash, encryption, backups; its own terminal and Ethernet |
| `data_place` | a data centre: SQL (SQLite), documents (like MongoDB), key-value (like Redis), vectors for AI, an HTTP API; its own terminal, black and white, and Ethernet |
| `recustomize` | itelx as installed, everything else deleted |

<p align="center">
<img src="screenshots/customizers/noter.png" alt="Noter" width="380"> <img src="screenshots/customizers/work.png" alt="Work mode" width="380"><br>
<img src="screenshots/customizers/vault.png" alt="File saver" width="380"> <img src="screenshots/customizers/data_place.png" alt="data_place" width="380">
</p>

They are in [`customizers/release`](customizers/release). How to install and
use them: [docs/en/6-customizers.md](docs/en/6-customizers.md); how they are
made: [customizers/README.md](customizers/README.md).

## Learn

| | English | Arabic |
|---|---|---|
| 1 | [The XVC language](docs/en/1-xvc-language.md) | [Arabic](docs/ar/1-xvc-language.md) |
| 2 | [Your first program](docs/en/2-first-program.md) | [Arabic](docs/ar/2-first-program.md) |
| 3 | [From XVC to XVR](docs/en/3-xvc-to-xvr.md) | [Arabic](docs/ar/3-xvc-to-xvr.md) |
| 4 | [XVR apps in C](docs/en/4-xvr-in-c.md) | [Arabic](docs/ar/4-xvr-in-c.md) |
| 5 | [USB sticks and FAT32](docs/en/5-usb-fat32.md) | [Arabic](docs/ar/5-usb-fat32.md) |
| 6 | [Customizer apps](docs/en/6-customizers.md) | [Arabic](docs/ar/6-customizers.md) |

## XVC to XVR

```
itelx:/> xvc2xvr /dex/myapp.xvc            # on itelx
python tools/xvc2xvr.py myapp.xvc          # on a PC (Windows, Linux, macOS; Python 3)
python tools/xvrinfo.py myapp.xvr          # check a file as the kernel does, see what is inside
```

The XVR made from an XVC program is the XVC interpreter with the program
inside it: one file that installs and runs by its name, checked by the
kernel before it runs ([chapter 3](docs/en/3-xvc-to-xvr.md)).

<p align="center"><img src="screenshots/xvc2xvr.png" alt="xvc2xvr and sum -c on itelx" width="760"></p>

## Building

Everything needed is in [`usb/`](usb) already built. To build it again, or
your own apps (Linux or WSL with `gcc`, `ld`, `make`, `python3`; a path
without spaces):

```bash
make              # sdk, apps, the USB kit -> usb/
make host-xvc     # build/host/xvc: XVC on the PC, to try programs
make check        # tests of the PC tools
make customizers  # the customizer apps -> customizers/release
```

`tests/e2e_itelx.py` starts a real itelxOS 1.3 in QEMU, plugs in a FAT32
stick made from `usb/`, accepts the update and then runs every app with
keys typed on the keyboard (43 checks, UEFI and BIOS).
`tests/e2e_customizers.py` does the same for the customizer apps on
itelxOS 1.4: install, use, the network with every supported card,
recustomize.

## Layout

```
apps/xvc/        the simple apps (XVC)
apps/xvr/        the complex apps (C -> XVR); apps/lib: their screen and key helpers
kit/             update.xvr of the USB kit
sdk/             libitx, the system call interface, the XVR format, xvrlink, the XVC
                 interpreter, and xvcapp (the runtime of XVRs made from XVC)
tools/           xvc2xvr.py, xvrinfo.py, host-xvc (XVC on a PC)
customizers/     the customizer apps: their programs, installer, kernel parts, release/
usb/             ready for a FAT32 stick
docs/ar, docs/en the guide (Arabic, English)
screenshots/     the apps on itelxOS 1.3, the customizer apps on 1.4
tests/           tests of the tools, and the test on a real itelx
```

## Licence

[Mozilla Public License 2.0](LICENSE). The files in `sdk/` (but `sdk/xvcapp`)
and `tests/qemu.py` come unchanged from the itelxOS userspace, which is under
the same licence; `sdk/xvcapp`, the apps, the kit and the tools are new here. Programs you write for itelxOS with this SDK may be under any licence.

The customizer apps carry itelx kernels: the kernels they install, their
network parts (`customizers/kernel`) and the original itelx 1.4 kernel that
recustomize puts back. These are under the itelx Kernel License 1.0: use
and share them unchanged, but do not distribute modified versions. SQLite
(`customizers/third_party`) is public domain. Details:
[customizers/LICENSE](customizers/LICENSE).

Copyright (c) 2026 Ahmed Walid (AXV).
