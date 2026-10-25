# Changelog

## 1.2.1 (2026-10-25)

- recovery_tester: in the recovery system of "Use manual" it finds the
  system disk at `/media/broken-os` (where `disk.open` opens it from
  itelx 2.1 on), else at `/old` (itelx 2.0).
- `tests/e2e_system_tools.py`: the repair by hand (Use manual, disk.open,
  `recovery_tester cancel`, then the full itelxOS starts again).
- The guide's chapter 7 says so, in Arabic and English.

## 1.2 (2026-10-25)

System tools, for itelxOS 2.0 (`system-tools/`, sources in `apps/xvr`).
They are not in the USB kit, which is unchanged.

- formater: wipes the system disk, all but `/kernel` and `/boot`; `/media`,
  `/ram` and any other mounted disk are never touched. It lists what goes,
  says whether the backup system is on the boot partition, and asks y/n.
  The next start shows the red recovery screen.
- recovery_tester: keeps the good copy of `/system/xvinit.xvr` in
  `/users/recovery_tester`, damages the file on purpose, and after the red
  recovery screen repaired the system it puts the good copy back over the
  repaired file. When the repair failed it writes the reasons and the logs
  (the test's state, the boot status, the kernel log) to `report-ar.txt`
  and `report-en.txt`. `recovery_tester cancel` undoes the test, also in
  the recovery screen's temporary terminal.
- Both XVRs need itelx 2.0 (min_os), so the kernel of itelx 1.x refuses
  them. `tools/xvrminos.py` sets min_os; `make system-tools` builds them.
- `tools/xvc2xvr.py` and `tools/xvrinfo.py` accept XVRs made for itelx 2.0.
- `tests/e2e_system_tools.py` runs both on itelxOS 2.0 in QEMU (UEFI and
  BIOS), through the restart, the red recovery screen and Use backup.
- The guide's chapter 7, in Arabic and English.

## 1.1 (2026-10-23)

Customizer apps, for itelxOS 1.4 (`customizers/`, `customizers/release`).
Each is one `.xvr` that installs a program in place of the terminal and its
tools, plus a `.txt` version to read (its files with SHA-256, the
installer's source).

- noter: a journal (timeline, calendar, on this day, insights, moods, tags,
  journals, templates, prompts, passcode, export to a stick).
- school_mode: classes, homework, exams, a diary, grades, a study timer per
  subject with weekly goals and statistics.
- work_mode: tasks on a list or a board, projects with budgets and rates, a
  timer and timesheet, meetings, clients and deals, notes, invoices,
  expenses, reports.
- file_saver: a vault for files of any kind (sorted by kind, SHA-256
  checks, versions, trash, ChaCha20 locking, backups to a stick, sharing to
  browsers), its own terminal, and the "itelx 1.4 net" kernel.
- data_place: SQLite 3.54.1, documents like MongoDB, key-value like Redis
  (Redis protocol server), vector search, dbscan, dbstat, an HTTP JSON API,
  checked backups; its own terminal on a black screen with white text, and
  the "itelx 1.4 data" kernel.
- recustomize: itelx back as installed. It restores the system files and
  the kernel from a copy of itelx 1.4 that it carries, then deletes
  everything else.
- The kernels add Ethernet to itelx 1.4: Intel e1000 (82540EM, 82545EM,
  82544GC), e1000e (82574L), Realtek RTL8139 and virtio-net, with ARP,
  IPv4, ICMP, UDP, DHCP, DNS and TCP. There is no WiFi.
- The kit for full-screen programs (`customizers/kit`).
- `make customizers`; `tests/e2e_customizers.py` runs every customizer on
  itelxOS 1.4 in QEMU and tests the network with every supported card.
- The guide's chapter 6, in Arabic and English.
- `sdk/` is updated to itelxOS 1.4, which adds the system calls 41 to 46
  (cpustat, hwinfo, pcilist, disklist, fsinfo, tty_keywait). The USB kit in
  `usb/` is still the 1.0 kit, unchanged.

## 1.0 (2026-10-11)

The first release, for itelxOS 1.3.

- 19 XVC apps: welcome, calc, convert, base, primes, cal, timer, todo, notes,
  contacts, wc, search, tree, cipher, typing, table, quiz, hangman, ttt.
- 8 XVR apps in C: fm, edit, sysmon, sum, 2048, sokoban (11 levels checked
  with a solver), mines, life; `apps/lib/ui` for full-screen apps.
- xvc2xvr: XVC programs to XVR executables, on itelx (`xvc2xvr`) and on a PC
  (`tools/xvc2xvr.py`); `tools/xvrinfo.py` checks XVR files as the kernel does.
- The USB kit (`usb/`): `update.xvr` "itelxOS apps 1.0" installs `dex/`.
- The SDK (`sdk/`), and `make host-xvc` for XVC on a PC.
- The guide in Arabic and English (`docs/ar`, `docs/en`).
- Tests: `tests/test_tools.py`, and `tests/e2e_itelx.py` on a real itelxOS 1.3
  in QEMU (43 checks, UEFI and BIOS).
