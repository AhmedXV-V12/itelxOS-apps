# Complex apps, XVR executables written in C

Each folder is one app: `make` builds `apps/xvr/NAME/*.c` with libitx and
[`apps/lib`](../lib) into `NAME.xvr` and copies it into `usb/dex/`. How it
works: [docs/en/4-xvr-in-c.md](../../docs/en/4-xvr-in-c.md)
([بالعربي](../../docs/ar/4-xvr-in-c.md)).

| app | use | keys |
|---|---|---|
| [fm](fm/fm.c) | `fm [folder]` | arrows choose, Enter opens (folders, programs run, text is shown), Backspace up, `c` copy, `m` move/rename, `d` delete, `n` new folder, `i` install, `e` edit, `g` go to, `q` quit |
| [edit](edit/edit.c) | `edit <file>` | `l` list, `a` add, `i` insert, `r` replace, `d` delete, `f` find, `s /old/new/`, `u` undo, `w` save, `q` quit (`h`: all) |
| [sysmon](sysmon/sysmon.c) | `sysmon`, `sysmon 1` | Ctrl+C stops |
| [sum](sum/sum.c) | `sum <files>`, `sum -crc <files>`, `sum -c SHA256SUMS` | |
| [2048](2048/2048.c) | `2048` | arrows (or w a s d), `u` undo, `r` new game, `q` quit |
| [sokoban](sokoban/sokoban.c) | `sokoban [level]` | arrows, `u` undo, `r` restart, `n`/`p` next/previous level, `q` quit |
| [mines](mines/mines.c) | `mines`, `mines 16`, `mines 30 16 99` | arrows, Space/Enter open, `f` flag, `r` new game, `q` quit |
| [life](life/life.c) | `life [random\|glider\|gun\|pulsar\|rpent\|acorn\|spaceship] [generations] [ms]` | Ctrl+C stops |

`xvc2xvr` is built from [`sdk/xvcapp`](../../sdk/xvcapp) (the XVC interpreter
with room for a program).

What the apps keep: `2048` its best score, `sokoban` the solved levels, both
in `/dex/data`. In raw-key apps the terminal passes Ctrl+C on as a key; they
stop on it like any other program.
