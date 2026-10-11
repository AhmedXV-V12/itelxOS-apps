# Simple apps, in XVC

Each app is one `.xvc` file that starts with a comment telling how to use it
(`welcome <name>` shows that comment on itelx). They are short on purpose:
read them next to [the language guide](../../docs/en/1-xvc-language.md)
([بالعربي](../../docs/ar/1-xvc-language.md)).

| app | try | shows how to |
|---|---|---|
| [welcome](welcome.xvc) | `welcome`, `welcome calc` | lists of lists, `exists`, reading a file's comment |
| [calc](calc.xvc) | `calc 1/3 + 1/6`, `calc` | a tokenizer and a recursive parser; exact fractions from ints |
| [convert](convert.xvc) | `convert 10 km mi`, `convert units` | a map of units, fractions, rounding |
| [base](base.xvc) | `base 0xff 1575` | parsing numbers in any base, `chr` |
| [primes](primes.xvc) | `primes 100`, `primes factor 360` | the sieve of Eratosthenes, factoring |
| [cal](cal.xvc) | `cal`, `cal 2 2028`, `cal 2027` | dates from `time()`, laying out columns |
| [timer](timer.xvc) | `timer 90`, `timer 1:30`, `timer` | `uptime`, `sleep`, `\r` to redraw a line |
| [todo](todo.xvc) | `todo add milk`, `todo done 1` | a list kept in a file, commands from args or typed |
| [notes](notes.xvc) | `notes add text`, `notes find word` | records with a date in a text file |
| [contacts](contacts.xvc) | `contacts add`, `contacts find omar` | asking with `input`, sorting records |
| [wc](wc.xvc) | `wc /dex/calc.xvc` | counting lines, words and characters |
| [search](search.xvc) | `search print /dex` | walking folders with recursion |
| [tree](tree.xvc) | `tree /dex 1`, `tree -d /` | drawing a tree with box characters |
| [cipher](cipher.xvc) | `cipher caesar 3 hello` | working on characters, Arabic letters too |
| [typing](typing.xvc) | `typing` | measuring time, comparing text |
| [table](table.xvc) | `table show 7`, `table 7` | a quiz loop with `input` |
| [quiz](quiz.xvc) | `quiz` | shuffling, `key()` |
| [hangman](hangman.xvc) | `hangman` | a game screen redrawn with `clear()` |
| [ttt](ttt.xvc) | `ttt`, `ttt easy` | minimax with alpha-beta: a computer that never loses |

The apps that keep data put it in `/dex/data` (`todo.txt`, `notes.txt`,
`contacts.txt`). Run on a PC with `build/host/xvc` they use `./data` instead.
