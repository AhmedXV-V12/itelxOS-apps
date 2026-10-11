# 2. Your first program

[← Contents](README.md) · [العربية](../ar/2-first-program.md)

## Where to write it

**On a PC** (Windows, Linux, macOS): any text editor (Notepad, VS Code...).
Save the file as **UTF-8** with a name that ends in `.xvc`, for example
`hello.xvc`. Then copy it to a USB stick ([chapter 5](5-usb-fat32.md)).

In Notepad choose the encoding *UTF-8*, not *UTF-8 with BOM*: the BOM is
three hidden bytes at the start of the file, and itelx's `run` reads them
as part of the first word (`undefined name 'print'` on line 1). `xvc2xvr`
removes them when it converts a file.

**On itelx itself**: with the `edit` app of this repository

```
edit /dex/hello.xvc
```

type `a print("hello")` and Enter, then `w` (save) and `q` (quit). Without
`edit`, the terminal's own `write` command works too: `write /dex/hello.xvc`,
type the lines, and end with a line that has only a `.`.

## Run it

```
itelx:/> run /dex/hello.xvc
hello
itelx:/> hello
hello
```

A program in `/dex` runs by its name alone. A program on a USB stick runs
from where it is: `run /media/STICK/hello.xvc`. `install /media/STICK/hello.xvc`
copies it into `/dex`.

## A real program, step by step

A small "guess the number" game. Write it as `guess2.xvc`:

```
# guess2.xvc - guess the number from 1 to 100
let secret = random(100) + 1
let tries = 0
print("I picked a number from 1 to 100.")

while true {
    let line = input("your guess: ")
    if line == nil { break }            # Ctrl+D: the end of the input
    let g = int(line)
    if g == nil {
        print("that is not a number")
        continue
    }
    tries += 1
    if g < secret { print("higher") }
    elif g > secret { print("lower") }
    else {
        print("right, in " + str(tries) + " tries")
        break
    }
}
```

What each part does:

- `random(100)` gives 0 to 99, so `+ 1` makes it 1 to 100.
- `input("your guess: ")` shows the text and waits for a line.
- `int(line)` turns the text into a number, or gives `nil` when it is not
  one: always check before using it.
- `continue` goes back to the top of the loop, `break` leaves it.
- `str(tries)` turns the number back into text, to join it with `+`.

## When something is wrong

Make a mistake on purpose: write `print("right, in " + str(tries) + " tries"`
without the last `)`. Running it gives

```
xvc: guess2.xvc:19: expected ')' after the arguments (at 'break')
[exit 2]
```

Nothing ran: the file is compiled before it starts. The line number tells
you where to look. A mistake that shows only while the program runs (here,
a list index that does not exist) looks like

```
xvc: guess2.xvc:7: runtime error: list index 3 out of range (length 2)
```

and the program stops there.

## Arguments, colours and keys

```
# hi.xvc - hi <name>
if len(args) < 2 {
    print("use: hi <name>")
    exit(1)
}
print(color("green") + "hi " + args[1] + "!" + color("reset"))
print("press a key")
print("you pressed: " + key())
```

```
itelx:/> hi Sara
hi Sara!
press a key
you pressed: x
```

## Keeping data

Programs keep their files in `/dex/data`:

```
let FILE = "/dex/data/visits.txt"
if not exists("/dex/data") { makedir("/dex/data") }
let n = 0
let old = readfile(FILE)
if old != nil { n = int(trim(old)) }
n += 1
writefile(FILE, str(n))
print("this program ran " + str(n) + " time(s)")
```

## Next

- Read the apps in [`apps/xvc`](../../apps/xvc): each one is short and
  starts with a comment that says how to use it.
- Make an XVR executable out of your program: [chapter 3](3-xvc-to-xvr.md).
- Trying programs on a PC first: `make host-xvc` builds the XVC interpreter
  for Linux, WSL or macOS (`build/host/xvc hello.xvc`). It is the same
  interpreter, but the paths are the PC's (there is no `/dex`).
