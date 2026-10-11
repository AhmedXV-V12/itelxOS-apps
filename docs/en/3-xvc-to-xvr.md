# 3. From XVC to XVR

[← Contents](README.md) · [Arabic](../ar/3-xvc-to-xvr.md)

## What an XVR file is

XVR is the executable format of itelxOS (the kernel loads no other). A file
has a 128-byte header, a table of up to 16 segments, and the segments'
bytes:

```
+------------------------------+  0
| header: "XVR", version 1,    |
| x86-64, entry point, kind    |
| (program / update), name,    |
| checksums ...                |
+------------------------------+  128
| segment table (32 bytes each)|  where each segment goes in memory,
+------------------------------+  readable / writable / executable
| the segments' bytes          |
+------------------------------+
```

Before running anything the kernel checks everything: the header and the
payload have CRC-32 checksums, segments must be page aligned, may not
overlap, may never be both writable and executable, and must stay in the
user area. A damaged or foreign file is refused with the reason:

```
itelx:/> /media/APPS/pc/damaged.xvr
/media/APPS/pc/damaged.xvr: not a valid XVR executable: payload checksum mismatch (damaged file)
```

## What xvc2xvr makes

`xvc2xvr` turns an XVC program into an XVR executable. The result is the XVC
interpreter of itelx with your program inside it, in one more read-only
segment at the end (see [`sdk/xvcapp`](../../sdk/xvcapp)). When it starts it
compiles that program and runs it exactly as `run program.xvc` would.

It is not compiled to machine code: it runs as fast as the `.xvc`. What you
get:

- **one file** that is a program like any other: it runs by its name,
  `install` takes it, the update of a USB stick can carry it;
- the kernel's **checks**: a copy damaged on the way is refused, not run;
- your program in the **name** of the file (`xvrinfo.py` shows it).

The interpreter adds about 62 KiB to the size of the program.

## Way 1: on itelx

`xvc2xvr` is one of the apps of this repository (`/dex/xvc2xvr.xvr` once
installed):

```
itelx:/> xvc2xvr /dex/guess2.xvc
xvc2xvr: made /dex/guess2.xvr (64279 bytes) from /dex/guess2.xvc
run it by its name: guess2
itelx:/> xvc2xvr /media/STICK/game.xvc /media/STICK/game.xvr
```

It compiles the program first. A program with a mistake is not converted:

```
itelx:/> xvc2xvr broken.xvc
xvc: broken.xvc:2: expected an expression (at ')')
xvc2xvr: broken.xvc was not converted: correct the error above first
```

When both `name.xvr` and `name.xvc` are in `/dex`, the name runs the `.xvr`.

## Way 2: on a PC

[`tools/xvc2xvr.py`](../../tools/xvc2xvr.py) does the same on Windows, Linux
or macOS with Python 3 (nothing else to install). It builds on
`usb/dex/xvc2xvr.xvr` of this repository:

```
python tools/xvc2xvr.py myapp.xvc                # makes myapp.xvr next to it
python tools/xvc2xvr.py myapp.xvc out/game.xvr   # or where you say
```

The PC has no XVC compiler, so try the program on itelx (`run myapp.xvc`)
before you convert it; an XVR made with a mistake in it reports the mistake
when it starts.

## Looking inside an XVR file

```
python tools/xvrinfo.py usb/dex/2048.xvr
python tools/xvrinfo.py --source myapp.xvr       # the XVC program inside it
```

`xvrinfo.py` makes the same checks as the kernel, so "valid" there means
itelx will accept the file. On itelx, `install` and the update of a USB
stick make those checks too.

## The other way to an XVR: C

`xvc2xvr` keeps your program in XVC. For a program that must be fast or
needs what XVC does not have (full-screen drawing from raw keys, system
calls such as the process list), write it in C and build it with the SDK:
[chapter 4](4-xvr-in-c.md).
