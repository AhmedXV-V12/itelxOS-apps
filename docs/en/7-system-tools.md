# 7. System tools: formater and recovery_tester

[← Contents](README.md) · [Arabic](../ar/7-system-tools.md)

Two XVR programs written in C for **itelxOS 2.0**. They work with the red
recovery screen of itelx 2.0 and its backup system, so they need itelx 2.0:
the kernel of itelx 1.x refuses to start them ("needs a newer itelx").

| tool | what it does |
|---|---|
| `formater` | wipes the system disk, all but the kernel and the boot files |
| `recovery_tester` | makes a fault on purpose, checks that the red recovery screen repairs it, and puts the system back as it was |

They are in [`system-tools/`](../../system-tools) with their SHA-256 in
`SHA256SUMS`. They are not in the USB kit (`usb/`), because the kit installs
on every itelx and these two belong only on itelx 2.0. Copy them onto a
FAT32 stick and start them from it:

```
itelx:/> /media/STICK/formater.xvr
itelx:/> /media/STICK/recovery_tester.xvr
```

Both ask **y/n** before they change anything. itelx has no user accounts:
every program runs with the owner's (root's) rights, so that question is
what protects the system.

## Before you start: the red recovery screen

When itelx 2.0 cannot start, the boot manager shows the red recovery screen
instead of the system. It has two choices:

- **Use backup**: keeps your files in `/users/recovered-<date>`, then puts
  the backup system (a small itelx kept on the boot partition) on the disk:
  `/kernel`, `/system` and `/boot` become those of the backup system. The
  full itelxOS comes back with `update.xvr`.
- **Use manual**: a small recovery system in memory, on the red screen;
  `disk.open` opens the broken system at `/media/broken-os` (itelx 2.1; at
  `/old` on itelx 2.0), and `see-log` shows where the start stopped.

`/users` is the folder that "Use backup" keeps where it is. That is why
recovery_tester keeps its files there.

## formater

```
itelx:/> /media/STICK/formater.xvr
formater: this wipes the system disk.

Removed, with everything inside:
  /system
  /dex
  /users
  /notes.txt
Kept: /kernel  /boot  /media (USB sticks)  /ram (memory)

Without /system the system cannot start: the next start shows the red
recovery screen, where "Use backup" puts the backup system back.
The backup system is on the boot partition (\recovery\recovery.img).

Wipe the system disk? (y/n)
```

- Everything at the top of the system disk goes, with everything inside:
  `/system`, `/dex`, `/users` and every other file or folder.
- What stays: `/kernel` (the kernel and its modules) and `/boot` (the boot
  files). `/media` (USB sticks and other disks) is never touched, `/ram` is
  memory, not the disk, and any other disk mounted at the top is skipped.
- It says first whether the backup system is on the boot partition. If it
  is not, only a new installation (the ODE installer) brings itelxOS back.
- At the end it asks `Restart now? (y/n)`. At the next start itelx finds it
  cannot run (there is no service manager), marks itself failed and
  restarts into the red recovery screen. **Use backup** there puts the
  backup system on the disk.
- A folder that a program is working in cannot be removed (the terminal
  says which). Go to `/` in the terminal and run formater again.
- In the recovery screen's temporary terminal it does nothing: the system
  disk is not the root there.

## recovery_tester

```
recovery_tester           start a test, or check the one under way
recovery_tester cancel    undo the fault now
```

### 1. The test starts

```
itelx:/> /media/STICK/recovery_tester.xvr
recovery_tester: a test of the red recovery screen

1. The good copy of /system/xvinit.xvr (... bytes) goes to /users/recovery_tester/xvinit.xvr.good
2. /system/xvinit.xvr is damaged on purpose: the system cannot start with it
3. The next start shows the red recovery screen; repair the system there
   (Use backup keeps /users and puts the backup system on the disk)
4. Run recovery_tester again on the repaired system: it puts the good copy back,
   or writes why the repair failed to /users/recovery_tester/report-ar.txt and report-en.txt

Start the test? (y/n)
```

Before it changes anything it checks that the boot partition, the boot
status and the backup system are there, and that `/system/xvinit.xvr` is
valid. Then:

- the good copy of `/system/xvinit.xvr` (the service manager, the first
  program itelx starts) goes to `/users/recovery_tester/xvinit.xvr.good`,
  and is read back and compared (size and CRC-32);
- `/users/recovery_tester/state.txt` records the test: the file, its size
  and CRC-32, where the fault is, when it started, the kernel;
- the fault: 64 bytes in the middle of `/system/xvinit.xvr` are inverted.
  The file's checksum no longer matches, and the kernel refuses to start
  it, exactly as it would refuse a file damaged on a real disk.

`Restart now? (y/n)`: the test goes on at the next start. Until then,
`recovery_tester cancel` puts the good copy back and leaves nothing behind.

### 2. The red recovery screen

At the next start itelx says `itelx cannot run: the service manager
(/system/xvinit.xvr) could not be started` and restarts, and the red
recovery screen comes up. Repair the system there: **Use backup**, or
**Use manual** and fix it by hand. In the terminal of Use manual, open the
system disk with `disk.open`; then `/media/STICK/recovery_tester.xvr cancel`
works there too, on the files in `/media/broken-os` (or `/old`), and the
next start is the full itelxOS again.

### 3. The check

On the repaired system, start recovery_tester again:

- **The repair worked** (`/system/xvinit.xvr` is a valid program again,
  the system started, the good copy is intact): recovery_tester puts the
  good copy back over the repaired file at once, reads it back, and removes
  its folder in `/users`:

  ```
  recovery_tester: PASSED - the red recovery screen repaired the system, and the
  original file is back.
  ```

  After Use backup the computer runs the backup system: install
  `update.xvr` there to get the full itelxOS back.
- **The repair failed**: recovery_tester says why, and writes two reports,
  `/users/recovery_tester/report-en.txt` in English and
  `/users/recovery_tester/report-ar.txt` in Arabic. Each holds the reasons,
  what was found (the file, the good copy, the boot status, whether the
  backup system is on the disk, the folders Use backup kept, the kernel)
  and the logs: `state.txt` and the end of the kernel log. The test stays
  in place, so `recovery_tester cancel` can still put the good copy back.

The reasons it looks for: the damaged file is not on the disk any more, it
is still damaged, the good copy is missing or damaged, no start completed
since the test began, or the good copy could not take the place of the
repaired file.

## How they were tested

`tests/e2e_system_tools.py` runs both on a real itelxOS 2.0 in QEMU, UEFI
and BIOS: from a FAT32 stick, with y and n typed on the keyboard, through
the restart, the red recovery screen (checked on the screen), Use backup,
and the check after it: PASSED, and FAILED with both reports when the good
copy was deleted during the test. It also repairs by hand: Use manual,
`disk.open`, `recovery_tester cancel` from the stick, and the next start is
the full itelxOS again.

```
python3 tests/e2e_system_tools.py --image /path/to/itelxOS.img --fw uefi,bios
```

[← Contents](README.md)
