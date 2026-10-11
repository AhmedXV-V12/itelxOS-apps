# Learn to make apps for itelxOS

[العربية](../ar/README.md)

itelxOS is a 64-bit operating system with its own kernel, its own file
system (ITFS) and its own terminal. It runs two kinds of programs:

| | XVC | XVR |
|---|---|---|
| what it is | a programming language: the program is a text file | an executable: a file of machine code |
| file | `name.xvc` | `name.xvr` |
| made with | any text editor (on a PC or on itelx) | `xvc2xvr` from an XVC program, or a C compiler with the SDK |
| started by | the XVC interpreter (`/system/xvc.xvr`) | the kernel itself |
| good for | simple apps: tools, games, lists, learning | complex apps: full-screen programs, speed, system calls |

These pages take you from nothing to your own app running on itelx:

1. [The XVC language](1-xvc-language.md): everything the language has, with examples
2. [Your first program](2-first-program.md): write it, run it, fix it
3. [From XVC to XVR](3-xvc-to-xvr.md): turn an XVC program into an XVR executable, on itelx or on a PC
4. [XVR apps in C](4-xvr-in-c.md): complex apps with the SDK of this repository
5. [Getting programs onto itelx with a USB stick](5-usb-fat32.md): FAT32, the update offer, install

Everything here was checked on itelxOS 1.3: the apps of this repository are
started, typed into and checked on a real itelx by `tests/e2e_itelx.py`.
