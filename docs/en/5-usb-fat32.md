# 5. Getting programs onto itelx with a USB stick

[← Contents](README.md) · [Arabic](../ar/5-usb-fat32.md)

itelx 1.3 reads and writes USB sticks (and card readers and external disks)
formatted as **FAT32** (FAT16 and FAT12 too). **exFAT and NTFS are not
read**: a stick formatted like that does not appear. The stick may have a
partition table (MBR or GPT) or none.

## 1. Format the stick as FAT32

Formatting erases everything on the stick.

**Windows** - sticks up to 32 GB: File Explorer, right-click the stick,
*Format...*, file system **FAT32**, a volume label such as `APPS`, *Quick
Format*, *Start*. Or in a terminal (replace `E:` with the stick's letter):

```
format E: /FS:FAT32 /V:APPS /Q
```

Windows does not offer FAT32 for sticks larger than 32 GB. Use a smaller
stick, or a tool that formats large sticks as FAT32, or make one FAT32
partition of 32 GB or less on it (Disk Management).

**Linux** (replace `/dev/sdX` with the stick, see `lsblk`):

```bash
sudo mkfs.vfat -F 32 -n APPS /dev/sdX1
```

**macOS**: Disk Utility, select the stick, *Erase*, format **MS-DOS (FAT)**.

The label becomes the stick's folder on itelx: `/media/APPS`.

## 2. Copy the apps

Copy **what is inside** the [`usb/`](../../usb) folder of this repository to
the top of the stick, so that the stick holds:

```
update.xvr          the installer (an XVR of the update kind)
dex/                the apps: .xvc programs and .xvr executables
SHA256SUMS          the checksums of all of them
README.txt
```

(On GitHub: *Code*, *Download ZIP*, then open the ZIP's `usb` folder.) Your
own programs go into `dex/` on the stick next to them.

Eject the stick from the PC before pulling it out.

## 3. Plug it into itelx

itelx sees the stick at once and, because it has an `update.xvr` at its
top, asks:

```
[USB] /media/APPS holds an itelx update: itelxOS apps 1.0 (structure valid; unsigned)
are you need update ? (yes/no) yes
itelxOS apps - installing from /media/APPS
  installed /dex/2048.xvr            16008 bytes
  ...
update done: 28 program(s) installed (266 KiB)
```

Nothing runs before you answer **yes**. Every `.xvr` is checked before it
is copied; a damaged one is skipped with the reason. Then the apps run by
their names: type `welcome`.

## Without the update

Any stick works, with or without `update.xvr`:

```
ls /media                          the stick is a folder there (USB devices are listed too)
ls /media/APPS
run /media/APPS/game.xvc           run a program from the stick
/media/APPS/tool.xvr               an .xvr runs by its path
install /media/APPS/game.xvc       copy it into /dex: then "game" runs it
copy /dex/data/notes.txt /media/APPS/notes.txt     and back to the stick
eject /media/APPS                  before you pull it out
```

`copy -r`, `move` and `del -r` work on the stick like on any disk, and the
`fm` app does all of it with the arrow keys.

## Checking the files

The kit carries `SHA256SUMS`, so one command on itelx tells whether the
stick holds exactly what was published:

```
itelx:/> sum -c /media/APPS/SHA256SUMS
update.xvr: OK
dex/2048.xvr: OK
...
29 OK, 0 failed, 0 missing
```

For any other file compare its SHA-256: on the PC
`certutil -hashfile file SHA256` (Windows) or `sha256sum file` (Linux), on
itelx `sum file`.

## Good to know

- Long file names and Arabic file names work (FAT stores them as UTF-16).
- `usb` lists the USB devices with their names in `/media`; `mounts` lists
  the disks in use.
- Pulling a stick out without `eject` may lose what was being written.
- The system disk's EFI partition shows in `/media` too, read only.
