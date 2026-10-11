# 1. The XVC language

[← Contents](README.md) · [Arabic](../ar/1-xvc-language.md)

XVC is the programming language of itelxOS. A program is a text file that
ends in `.xvc`; the terminal runs it with `run name.xvc` (or just `name`
when it is in `/dex`). The XVC interpreter compiles the whole file first,
so a mistake anywhere is reported before anything runs, with its line.

```
# hello.xvc
let name = "itelx"
print("Hello from " + name)
print("مرحبا من XVC")
```

## The shape of a program

- A program runs from the top down. There is no `main`.
- One statement per line. Two statements on one line are separated by `;`.
- `#` starts a comment, up to the end of the line.
- Blocks are in `{ }`: `if x > 0 { print("yes") }`.
- A long expression may go on to the next line inside `( )`, `[ ]` or `{ }`,
  or when its line ends with an operator:

  ```
  let total = price * count +
      shipping
  let days = ["sat", "sun",
      "mon"]
  ```

  An operator at the *start* of a line begins a new statement, so put it at
  the end of the line before.

## Values

| type | examples | notes |
|---|---|---|
| int | `42`, `-7`, `0xff`, `1_000_000` | 64-bit whole numbers. There are no fractions or decimals. |
| bool | `true`, `false` | |
| nil | `nil` | "nothing": a missing map key, `input()` at the end of input, a function without `return` |
| string | `"text"`, `"سلام"` | UTF-8. `len` and indexes count characters, not bytes. Strings cannot be changed. |
| list | `[1, "two", [3]]` | grows with `push` |
| map | `{"a": 1, 2: "b"}` | keys are strings, ints or bools |
| range | `range(10)` | a sequence of ints for `for` loops |
| function | `fn` ... | functions are values too |

`type(x)` gives the name: `"int"`, `"string"`, `"list"`, ...

Escapes in strings: `\n` new line, `\t` tab, `\r`, `\0`, `\e` (escape),
`\\`, `\"` and `\u{0627}` (a Unicode character by its number).

What counts as false in `if` and `while`: `nil`, `false`, `0`, `""` and the
empty list `[]`. Everything else is true (an empty map too).

## Variables

```
let count = 0          # a new variable
count = count + 1      # a new value
count += 1             # the same; -= works too
let nothing            # nil
```

A variable made with `let` at the top level of the program (not inside a
block) is **global**: every function can read and change it. A `let` inside
a block (`if`, `while`, `for`, a function) lives until the end of that block.

Names are letters, digits and `_`, and may be Arabic: `let الاسم = "أحمد"`.

The keywords are `let fn if elif else while for in return break continue
true false nil and or not`.

## Operators

From the loosest to the tightest:

| operators | |
|---|---|
| `or` | the first true value (the rest is not evaluated) |
| `and` | the first false value (the rest is not evaluated) |
| `not` | true when the value is false. `not a == b` is `not (a == b)` |
| `==` `!=` | any two values; different types are simply not equal |
| `<` `<=` `>` `>=` | two ints or two strings (strings by their bytes) |
| `+` `-` | ints; `+` also joins strings (`"a" + 1` is `"a1"`) and lists |
| `*` `/` `%` | ints; `/` drops the fraction (`7 / 2` is `3`, `-7 / 2` is `-3`); `"ab" * 3` is `"ababab"` |
| `-x` | minus |
| `f(x)` `x[i]` | a call, an index |

Division by zero stops the program with a runtime error.

## Strings

```
let s = "itelx-os"
len(s)                 # 8
s[0]                   # "i"   (an index gives a one-character string)
s[-1]                  # "s"   (negative indexes count from the end)
substr(s, 6, 2)        # "os"  (from 6, 2 characters; without the count: to the end)
find(s, "os")          # 6     (-1 when it is not there)
upper(s)  lower(s)  trim("  x  ")
replace(s, "-", " ")   # "itelx os"
split("a,b,c", ",")    # ["a", "b", "c"]; split(s) splits on spaces
join(["a", "b"], "+")  # "a+b"
startswith(s, "ite")   endswith(s, "os")    has(s, "x-o")
str(42)                # "42"
int("42")  int("0x1f") # 42, 31; int("4x") is nil, so check: if n == nil { ... }
chr(1575)  ord("ا")    # "ا", 1575
for c in "سلام" { print(c) }   # one character at a time
```

## Lists

```
let l = [5, 3, 9]
l[0]                   # 5
l[1] = 4               # change an item
l[2] += 1
push(l, 7)             # add at the end
pop(l)                 # remove the last one and give it back
insert(l, 0, 99)       # 99 at position 0
removeat(l, 1)         # remove position 1 and give it back
len(l)  has(l, 9)
sort(l)                # ints, or strings
reverselist(l)
l + [1, 2]             # a new list with both
for x in l { print(x) }
```

A list written in the program (`[ ... ]`) holds up to 255 items; `push`
grows a list as far as memory goes.

## Maps

```
let ages = {"omar": 31, "sara": 28}
ages["mona"] = 25      # add or change
ages["nobody"]         # nil: a missing key
has(ages, "omar")      # true
keys(ages)             # a list of the keys, in no particular order
for name in ages { print(name + ": " + str(ages[name])) }
```

To show keys in order: `let k = keys(m); sort(k)`. A key cannot be removed:
build a new map without it.

## Decisions and loops

```
if n < 0 {
    print("negative")
} elif n == 0 {
    print("zero")
} else {
    print("positive")
}

while n > 0 { n -= 1 }

for i in range(5) { }          # 0 1 2 3 4
for i in range(1, 6) { }       # 1 2 3 4 5
for i in range(10, 0, -2) { }  # 10 8 6 4 2
for item in list { }
for ch in "text" { }
for key in map { }
```

`break` leaves the loop, `continue` goes to the next turn.

## Functions

```
fn area(w, h) {
    return w * h
}
print(area(3, 4))

fn fact(n) {
    if n <= 1 { return 1 }
    return n * fact(n - 1)        # a function may call itself
}
```

- Functions are written at the top level of the program, not inside a block.
- The program runs from the top, and `fn` defines its function when it is
  reached: write the functions above the code that calls them. Functions
  may call each other in any order.
- A function must be called with exactly as many values as it has
  parameters.
- A function sees its own parameters and `let` variables, and the global
  variables. It cannot see the variables of the code that called it: pass
  them as arguments.
- Without `return`, a function gives `nil`.
- Calls may go 256 deep (recursion).

## Input and output

```
print("a", 1, true)    # values with spaces between them, then a new line
write("no new line")
let name = input("your name: ")     # a line typed by the user (nil at the end)
let k = key()          # one key, without Enter: "a", "enter", "esc", "up", "down", "left", "right", "f1"...
clear()                # clear the screen
print(color("green") + "done" + color("reset"))
```

Colours: `white`, `yellow`, `red`, `green`, `cyan`, `dim`, `reset`.

The arguments of the program are in the list `args`: `args[0]` is the
program itself, then what was typed after its name.

```
# greet.xvc - run greet.xvc Sara
if len(args) > 1 { print("hello " + args[1]) } else { print("use: greet <name>") }
```

## Files and folders

```
writefile("/dex/data/a.txt", "first\n")   # true when it worked
appendfile("/dex/data/a.txt", "second\n")
let text = readfile("/dex/data/a.txt")      # nil when it cannot be read
exists(path)  isdir(path)  filesize(path)
listdir("/dex")        # a list of names
makedir("/dex/data")   removefile(path)
```

Your programs can keep their files in `/dex/data` (make it with
`makedir("/dex/data")` when `exists` says it is missing). USB sticks are in
`/media/NAME`.

## Programs, time, randomness

```
run("other.xvc", "arg")   # runs a program (.xvc or .xvr) and waits; gives its exit status
let p = spawn("counter.xvc")  wait(p)  kill(p)   # in the background
sleep(500)                # milliseconds
time()                    # seconds since 1970 (the computer's clock)
uptime()                  # milliseconds since itelx started
pid()                     # this program's process number
random(6) + 1             # 1 to 6
exit(0)                   # end now, with an exit status
```

## Errors

There are two kinds, and both name the file and the line:

```
xvc: game.xvc:12: expected ')' after the arguments          (the program does not compile: nothing ran)
xvc: game.xvc:30: runtime error: list index 5 out of range (the program stopped there)
```

You can stop a program yourself with `error("message")`, or check what must
be true with `assert(x > 0, "x must be positive")`. A program that does not
compile ends with status 2, a runtime error with status 1.

## Every built-in function

| function | what it does |
|---|---|
| `print(...)`, `write(...)` | output, with and without a new line |
| `input([prompt])` | a line typed by the user; `nil` at the end of input |
| `key()` | one key, without Enter |
| `clear()`, `color(name)` | clear the screen; a colour code |
| `len(x)` | characters of a string, items of a list or map, length of a range |
| `str(x)`, `int(x)`, `type(x)` | convert; the type's name |
| `chr(n)`, `ord(s)` | a character from its number and back |
| `upper`, `lower`, `trim` | strings (upper/lower: English letters) |
| `substr(s, from[, count])`, `find(s, t)`, `replace(s, a, b)` | strings |
| `split(s[, sep])`, `join(list[, sep])` | strings and lists |
| `startswith(s, t)`, `endswith(s, t)` | strings |
| `push`, `pop`, `insert`, `removeat`, `sort`, `reverselist` | lists |
| `keys(map)`, `has(x, v)` | maps (has works on lists and strings too) |
| `range(n)`, `range(a, b[, step])` | ranges for `for` |
| `readfile`, `writefile`, `appendfile` | files |
| `exists`, `isdir`, `filesize`, `listdir`, `makedir`, `removefile` | files and folders |
| `run(prog, args...)`, `spawn(...)`, `wait(pid)`, `kill(pid)` | programs |
| `sleep(ms)`, `time()`, `uptime()`, `pid()`, `random(n)` | time and chance |
| `exit([code])`, `assert(cond[, msg])`, `error(msg)` | ending and errors |

## Limits

64-bit ints only (no decimals: keep cents as ints, `1250` for 12.50);
programs up to 8 MiB; strings up to 16 MiB; 256 calls deep; 64 arguments
per call; 256 local variables per function; 255 items in a list or map
written in the program. The apps in [`apps/xvc`](../../apps/xvc) show all of
this at work: `calc.xvc` even builds exact fractions out of ints.
