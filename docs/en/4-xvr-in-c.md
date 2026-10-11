# 4. XVR apps in C

[← Contents](README.md) · [العربية](../ar/4-xvr-in-c.md)

The complex apps of this repository (`edit`, `fm`, `sysmon`, `2048`,
`sokoban`, `mines`, `life`, `sum`) are written in C and built into XVR
executables with the SDK in [`sdk/`](../../sdk). It is the same toolchain
itelxOS builds its own programs with.

## What you need

A Linux system or **WSL** on Windows, with `gcc`, `ld` (binutils), `make`
and `python3`:

```bash
sudo apt install build-essential python3       # Debian, Ubuntu, WSL
```

No other library: a program uses **libitx** (`sdk/system/lib`), the small C
library of itelx. `make` cannot work in a folder whose path has spaces, so
clone the repository into one without.

## The pieces

| file | what it is |
|---|---|
| `sdk/system/lib/include/itx.h` | everything a program can call (below) |
| `sdk/abi/itx_abi.h` | system call numbers, error codes, structures |
| `sdk/system/lib/crt0.S`, `libitx.c` | the start of a program and the library |
| `sdk/system/lib/xvr.ld` | the link layout: from 0x400000, code R-X, constants R, data RW |
| `sdk/tools/xvrlink.c` | turns the linked ELF file into an XVR file |
| `sdk/xvr/xvr_format.h` | the XVR format |
| `apps/lib/ui.h` | full-screen frames, raw keys, a random number generator (this repository) |

## Hello, in C

```c
/* apps/xvr/hello2/hello2.c */
#include <itx.h>

int main(int argc, char **argv) {
    printf("hello from C, pid %d\n", getpid());
    for (int i = 1; i < argc; i++) printf("argument %d: %s\n", i, argv[i]);
    return 0;
}
```

Put it in `apps/xvr/hello2/` and run `make`: every folder in `apps/xvr` is one
app, built to `build/xvr/hello2.xvr` and copied into `usb/dex/`.

By hand, the same is three steps:

```bash
CFLAGS="-std=gnu11 -O2 -ffreestanding -fno-stack-protector -fno-pic -fno-pie -fno-builtin \
        -fno-tree-loop-distribute-patterns -fno-asynchronous-unwind-tables -Isdk/system/lib/include"
gcc $CFLAGS -c sdk/system/lib/crt0.S -o crt0.o
gcc $CFLAGS -c sdk/system/lib/libitx.c -o libitx.o
gcc $CFLAGS -c hello2.c -o hello2.o
ld -T sdk/system/lib/xvr.ld -nostdlib -static -z noexecstack -o hello2.elf crt0.o libitx.o hello2.o
gcc -O2 -o xvrlink sdk/tools/xvrlink.c
./xvrlink --name hello2 hello2.elf hello2.xvr
```

`xvrlink` keeps only the loadable segments and writes the header and the
checksums; `--stack` and `--heap` set the sizes (256 KiB and 64 MiB
otherwise), `--update` makes an update program (below).

## What libitx gives you

All of it is in [`itx.h`](../../sdk/system/lib/include/itx.h). Calls return a
negative error code when they fail (there is no `errno`); `strerror(r)`
gives its text.

- **output**: `printf`, `dprintf`, `snprintf`, `puts`, `putchar`, `flush`
  (output is buffered: `flush()` before waiting for input yourself)
- **memory**: `malloc`, `calloc`, `realloc`, `free`
- **strings**: `strlen`, `strcmp`, `strcpy`, `strlcpy`, `strstr`, `strchr`,
  `strdup`, `memcpy`, `memset`, `atoi`, `strtol`, UTF-8 `utf8_decode`/`utf8_encode`/`utf8_len`
- **files**: `open`, `read`, `write`, `close`, `lseek`, `stat`, `readdir`,
  `mkdir`, `remove`, `rename`, `truncate`, `sync`, `read_file`, `write_file`
- **programs**: `spawn`, `waitpid`, `kill`, `getpid`, `exit`, `proclist`
- **system**: `sysinfo`, `uptime_ms`, `unix_time`, `sleep_ms`, `mounts`,
  `devlist` (USB devices), `eject`, `xvr_check`, `reboot`, `poweroff`
- **terminal**: `tty_mode(TTY_MODE_RAW)` for keys one by one, `tty_key`,
  `tty_info` (columns, rows, keyboard language)

There is no floating point in `printf` and no C standard library beyond
this: keep numbers in ints (sizes in KiB, money in cents).

## Full-screen apps

The itelx terminal has no cursor movement: it keeps lines of text. An app
that fills the screen draws a whole frame each time: "clear the screen"
(`\x1b[2J`) and every line, in one `write`. The terminal redraws only the
cells that changed, so this does not flicker. [`apps/lib/ui.h`](../../apps/lib/ui.h)
does it for you:

```c
#include <ui.h>

int main(void) {
    int x = 10;
    ui_begin();                                   /* raw keys */
    for (;;) {
        ui_frame();
        ui_printf(C_CYAN "move the star" C_RESET "  (q quits)\n\n");
        ui_printf("%*s*\n", x, "");
        ui_show();
        int k = ui_key();
        if (k == 'q' || k == 27) break;
        if (k == KEY_LEFT && x > 0) x--;
        if (k == KEY_RIGHT && x < 60) x++;
    }
    ui_end();
    return 0;
}
```

Colours are the terminal's six: `C_RESET` (white), `C_DIM`, `C_YELLOW`,
`C_RED`, `C_GREEN`, `C_CYAN`. In raw mode the terminal hands Ctrl+C to the
program as a key instead of stopping it; `ui_key()` ends the program then,
so Ctrl+C works as everywhere else.

## Update programs

An XVR of the **update** kind (`xvrlink --update`) at the top of a USB stick,
named `update.xvr`, is offered by itelx when the stick is plugged in
("are you need update ?"). On yes it runs with the stick's folder as its
argument. [`kit/update.c`](../../kit/update.c) is the one of this
repository: it copies the stick's `dex/` folder into `/dex`, checking every
`.xvr` with `xvr_check` first. More in [chapter 5](5-usb-fat32.md).

## The apps as examples

| app | what to look at |
|---|---|
| [`sum`](../../apps/xvr/sum/sum.c) | reading files in pieces, SHA-256, CRC-32; a plain command-line tool |
| [`sysmon`](../../apps/xvr/sysmon/sysmon.c) | `sysinfo`, `proclist`, a refreshing screen |
| [`2048`](../../apps/xvr/2048/2048.c) | the smallest full-screen game: frame, keys, undo, a saved best score |
| [`sokoban`](../../apps/xvr/sokoban/sokoban.c) | levels as text, undo history, progress kept in `/dex/data` |
| [`mines`](../../apps/xvr/mines/mines.c) | a cursor on a grid, flood fill |
| [`life`](../../apps/xvr/life/life.c) | the screen size from `tty_info`, animation |
| [`edit`](../../apps/xvr/edit/edit.c) | the terminal's line editing (cooked mode), a text in memory |
| [`fm`](../../apps/xvr/fm/fm.c) | folders, copying across disks, running other programs with `spawn` |
